#pragma once
#include <Arduino.h>
#include <esp_sleep.h>
#include "config.h"
#include "shared_state.h"

/*
 * ============================================================================
 * battery.h — gestione completa della batteria
 *
 * HARDWARE CONFERMATO: Heltec WiFi LoRa 32 V2.1
 *   Pin ADC:    GPIO37 (ADC1-CH1) — identificato con Heltec_Board_Identifier
 *   Partitore: R1=R2=100k → divisore = 2.0 (V2 aveva 4.9)
 *   ADC1 non ha conflitti con WiFi → lettura semplice e affidabile
 *
 * LETTURA ADC:
 *   Attenuazione 11dB (ADC_ATTEN_DB_11), fondo scala ≈ 3.9V
 *   Con 0dB (Vref=1.1V) l'ADC saturava: Vadc=Vbat/2=1.85V > 1.1V → raw=4095 → Vbat=5.39V
 *   Con 11dB: raw ≈ 1575–2205 per Li-Ion 3.0V–4.2V → nessuna saturazione
 *
 * RILEVAMENTO CARICA:
 *   Doppio criterio:
 *   1. Delta tensione tra letture consecutive > BATTERY_CHARGING_THRESHOLD
 *   2. Vbat > BATTERY_VOLTAGE_CHARGING (4.05V): TP4056 sicuramente attivo
 * ============================================================================
 */

static unsigned long _batWarnStart  = 0;
static float         _lastValidVbat = -1.0f;
static float         _prevVbat      = -1.0f;  // Per rilevamento delta carica

// ---------------------------------------------------------------------------
// _readBatteryVoltage() — lettura ADC1 (GPIO37), semplice e affidabile
//
// ADC1 non ha conflitti con WiFi: nessun retry complesso necessario.
// Usa analogSetPinAttenuation(ADC_11db) per Vref≈3.9V, evita saturazione.
// ---------------------------------------------------------------------------
static float _readBatteryVoltage() {
    // Configura ADC1 con attenuazione 11dB (fondo scala ≈ 3.9V)
    // Necessario perché Vadc = Vbat/2 ≈ 1.85V > 1.1V (limite 0dB)
    analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);
    delay(5);  // Breve attesa per stabilizzazione pin dopo cambio attenuazione

    long sum   = 0;
    int  count = 0;

    for (int i = 0; i < BATTERY_ADC_SAMPLES; i++) {
        int raw = analogRead(BATTERY_ADC_PIN);

        // Scarta raw spuri (non dovrebbero esserci su ADC1, ma per sicurezza)
        if (raw >= BATTERY_ADC_MIN_VALID_RAW) {
            sum += raw;
            count++;
        }
        delay(BATTERY_ADC_SAMPLE_DELAY_MS);
    }

    if (count == 0) {
        Serial.println("BAT: nessun campione valido — batteria inserita?");
        return (_lastValidVbat > 0.0f) ? _lastValidVbat : -1.0f;
    }

    // Vbat = (raw_medio / 4095) * Vref_11dB * Moltiplicatore_partitore
    float vadc = ((float)sum / (float)count) * (BATTERY_ADC_VREF / 4095.0f);
    float vbat = vadc * BATTERY_VOLTAGE_MULTIPLIER;

    _lastValidVbat = vbat;
    return vbat;
}

// ---------------------------------------------------------------------------
// _voltageToPercent() — curva lineare tra EMPTY e FULL
// ---------------------------------------------------------------------------
static float _voltageToPercent(float v) {
    if (v < 0.0f) return -1.0f;
    float pct = (v - BATTERY_VOLTAGE_EMPTY) /
                (BATTERY_VOLTAGE_FULL - BATTERY_VOLTAGE_EMPTY) * 100.0f;
    if (pct < 0.0f)   pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    return pct;
}

// ---------------------------------------------------------------------------
// Ring buffer storico percentuali (per grafico web)
// ---------------------------------------------------------------------------
static void _historyPush(float percent) {
    if (percent < 0.0f) return;  // Non pushare valori invalidi
    batHistory[batHistoryHead] = percent;
    batHistoryHead = (batHistoryHead + 1) % BATTERY_HISTORY_POINTS;
    if (batHistorySize < BATTERY_HISTORY_POINTS) batHistorySize++;
}

static float _historyAt(int i) {
    int idx = (batHistoryHead - batHistorySize + i + BATTERY_HISTORY_POINTS)
              % BATTERY_HISTORY_POINTS;
    return batHistory[idx];
}

// ---------------------------------------------------------------------------
// _updateTimeEstimates() — stima ETA carica/scarica da trend storico
// ---------------------------------------------------------------------------
static void _updateTimeEstimates() {
    if (batHistorySize < BATTERY_TREND_SAMPLES) {
        batEstimating  = true;
        batTimeToFull  = -1;
        batTimeToEmpty = -1;
        return;
    }
    batEstimating = false;

    float oldest   = _historyAt(batHistorySize - BATTERY_TREND_SAMPLES);
    float newest   = _historyAt(batHistorySize - 1);
    float deltaPct = newest - oldest;

    if (fabsf(deltaPct) < 0.01f) {
        batTimeToFull  = -1;
        batTimeToEmpty = -1;
        return;
    }

    float samplesPerMin = 60000.0f / (float)BATTERY_READ_INTERVAL_MS;
    float pctPerMin     = (deltaPct / (float)BATTERY_TREND_SAMPLES) * samplesPerMin;

    if (pctPerMin > 0.0f) {
        float rem      = 100.0f - newest;
        batTimeToFull  = (rem > 0.0f) ? (int)(rem / pctPerMin) : 0;
        batTimeToEmpty = -1;
    } else {
        float rem      = newest;
        batTimeToEmpty = (rem > 0.0f) ? (int)(rem / (-pctPerMin)) : 0;
        batTimeToFull  = -1;
    }
}

// ---------------------------------------------------------------------------
// _detectCharging() — doppio criterio: delta tensione + soglia assoluta
// ---------------------------------------------------------------------------
static bool _detectCharging(float newV, float prevV) {
    // Criterio 1: tensione in aumento rispetto alla lettura precedente
    if (prevV >= 0.0f && (newV - prevV) > BATTERY_CHARGING_THRESHOLD) return true;
    // Criterio 2: tensione sopra la soglia di carica TP4056 attivo
    if (newV >= BATTERY_VOLTAGE_CHARGING) return true;
    return false;
}

// ---------------------------------------------------------------------------
// initBattery() — prima lettura al boot + verifica soglia critica
// ---------------------------------------------------------------------------
inline void initBattery() {
    batVoltage     = _readBatteryVoltage();
    batPercent     = _voltageToPercent(batVoltage);
    batCharging    = false;
    batLowWarning  = false;
    batShouldSleep = false;
    batTimeToFull  = -1;
    batTimeToEmpty = -1;
    batEstimating  = true;

    // Se il sistema riparte da USB dopo scarica critica:
    // forza stato carica per non rientrare subito in deep sleep
    esp_reset_reason_t reason = esp_reset_reason();
    if ((reason == ESP_RST_POWERON || reason == ESP_RST_EXT) &&
        batVoltage >= 0.0f && batVoltage <= BATTERY_VOLTAGE_CRITICAL) {
        batCharging    = true;
        batShouldSleep = false;
        Serial.println("BAT: boot con Vbat critica — forzo stato carica.");
    }

    if (!batCharging && batVoltage > 0.0f) {
        batCharging = _detectCharging(batVoltage, -1.0f);
    }

    _prevVbat = batVoltage;
    _historyPush(batPercent);

    if (batVoltage > 0.0f && batVoltage <= BATTERY_VOLTAGE_CRITICAL && !batCharging) {
        Serial.println("BAT [CRITICA] al boot — richiesta deep sleep.");
        batShouldSleep = true;
        return;
    }

    if (batVoltage < 0.0f) {
        Serial.println("BAT: N/D al boot — batteria inserita?");
    } else {
        Serial.print("BAT init: ");
        Serial.print(batVoltage, 2);
        Serial.print(" V  ");
        Serial.print((int)batPercent);
        Serial.print("%  ");
        Serial.println(batCharging ? "[CARICA]" : "[SCARICA]");
    }
}

// ---------------------------------------------------------------------------
// batteryUpdate() — aggiornamento periodico (chiamata ogni 30 s dal loop)
// ---------------------------------------------------------------------------
inline void batteryUpdate() {
    float newV = _readBatteryVoltage();

    if (newV < 0.0f) {
        Serial.println("BAT: lettura fallita — valore precedente mantenuto.");
        return;
    }

    batCharging = _detectCharging(newV, _prevVbat);
    _prevVbat   = newV;
    batVoltage  = newV;
    batPercent  = _voltageToPercent(batVoltage);

    _historyPush(batPercent);   // Alimenta il grafico web
    _updateTimeEstimates();

    // Gestione soglie critiche
    if (batVoltage <= BATTERY_VOLTAGE_CRITICAL) {
        batLowWarning = true;
        if (!batCharging) {
            Serial.println("BAT [CRITICA] — richiesta deep sleep.");
            batShouldSleep = true;
        } else {
            Serial.println("BAT [CRITICA] ma in carica — attendo risalita.");
        }
        return;
    }

    if (batVoltage <= BATTERY_VOLTAGE_LOW && !batCharging) {
        if (_batWarnStart == 0) {
            _batWarnStart = millis();
            batLowWarning = true;
            Serial.println("BAT [BASSA] — deep sleep tra 10 minuti.");
        }
        if (millis() - _batWarnStart >= BATTERY_LOW_WARNING_MS) {
            Serial.println("BAT [BASSA] countdown scaduto — richiesta deep sleep.");
            batShouldSleep = true;
        }
    } else {
        if (_batWarnStart != 0) Serial.println("BAT: tensione risalita — preavviso annullato.");
        _batWarnStart = 0;
        batLowWarning = false;
    }

    Serial.print("BAT: ");
    Serial.print(batVoltage, 2);
    Serial.print(" V  ");
    Serial.print((int)batPercent);
    Serial.print("%  ");
    Serial.println(batCharging ? "[CARICA]" : "[SCARICA]");
}
