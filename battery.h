#pragma once
#include <Arduino.h>
#include "driver/adc.h"    
#include <esp_sleep.h>     
#include "config.h"
#include "shared_state.h"

/*
 * ============================================================================
 * battery.h — gestione completa della batteria
 * ============================================================================
 */

static unsigned long _batWarnStart = 0;

// ---------------------------------------------------------------------------
// LETTURA ADC — media su BATTERY_ADC_SAMPLES campioni
// ---------------------------------------------------------------------------
static float _readBatteryVoltage() {
    long sum   = 0;
    int  count = 0;

#ifdef BATTERY_ADC_USE_ADC2
    // --- Percorso V2: GPIO13, ADC2 ---
    // Configura attenuazione 0 dB ogni volta.
    adc2_config_channel_atten(
        (adc2_channel_t)BATTERY_ADC2_CH_RAW,
        ADC_ATTEN_DB_0
    );

    for (int s = 0; s < BATTERY_ADC_SAMPLES; s++) {
        int  raw = 0;
        bool ok  = false;
        for (int t = 0; t < BATTERY_ADC_RETRY_MAX; t++) {
            esp_err_t r = adc2_get_raw(
                (adc2_channel_t)BATTERY_ADC2_CH_RAW,
                ADC_WIDTH_BIT_12,
                &raw
            );
            if (r == ESP_OK) { ok = true; break; }
            delayMicroseconds(300);  
        }
        if (ok) { sum += raw; count++; }
        delay(1);  
    }
#else
    // --- Percorso V2.1+: GPIO37, ADC1 ---
    // CORREZIONE APPLICATA: Inserito il casting esplicito (adc_attenuation_t) per compilazione su core vecchio 0.0.7
    analogSetPinAttenuation(BATTERY_ADC_PIN, (adc_attenuation_t)ADC_ATTEN_DB_0);
    for (int s = 0; s < BATTERY_ADC_SAMPLES; s++) {
        sum += analogRead(BATTERY_ADC_PIN);
        delay(1);
    }
    count = BATTERY_ADC_SAMPLES;
#endif

    if (count == 0) return -1.0f;  

    float vadc = ((float)sum / (float)count) * (BATTERY_ADC_VREF / 4095.0f);
    return vadc * BATTERY_VOLTAGE_MULTIPLIER;
}

// ---------------------------------------------------------------------------
// CONVERSIONE tensione → percentuale
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
// RING BUFFER — Gestione Storico
// ---------------------------------------------------------------------------
static void _historyPush(float percent) {
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
// STIMA ADATTIVA tempo rimanente
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
// INIZIALIZZAZIONE — chiamata in setup()
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

    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_POWERON || reason == ESP_RST_EXT) {
        if (batVoltage >= 0.0f && batVoltage <= BATTERY_VOLTAGE_CRITICAL) {
            batCharging = true;      
            batShouldSleep = false;
            Serial.println("USB collegato: forzo stato carica per evitare deep sleep.");
        }
    }

    if (batPercent >= 0.0f) _historyPush(batPercent);

    if (batVoltage > 0.0f && batVoltage <= BATTERY_VOLTAGE_CRITICAL && !batCharging) {
        Serial.println("BAT [CRITICA] al boot — richiesta deep sleep.");
        batShouldSleep = true;
        return;
    }

    if (batVoltage < 0.0f) {
        Serial.println("BAT: N/D (lettura ADC fallita al boot)");
    } else {
        Serial.print("BAT: ");
        Serial.print(batVoltage, 2);
        Serial.print(" V  ");
        Serial.print((int)batPercent);
        Serial.println("%");
    }
}

// ---------------------------------------------------------------------------
// AGGIORNAMENTO PERIODICO — chiamata in loop()
// ---------------------------------------------------------------------------
inline void batteryUpdate() {
    float newV = _readBatteryVoltage();

    if (newV < 0.0f) {
        Serial.println("BAT: lettura ADC fallita — valore precedente mantenuto.");
        return;
    }

    if (batVoltage >= 0.0f) {
        float delta  = newV - batVoltage;
        batCharging  = (delta > BATTERY_CHARGING_THRESHOLD);
    }

    batVoltage = newV;
    batPercent = _voltageToPercent(batVoltage);
    _historyPush(batPercent);
    _updateTimeEstimates();

    if (batVoltage <= BATTERY_VOLTAGE_CRITICAL) {
        if (batCharging) {
            batLowWarning = true;
            Serial.println("BAT [CRITICA] ma in carica — attendo che salga.");
        } else {
            Serial.println("BAT [CRITICA] — richiesta deep sleep immediato.");
            batLowWarning = true;
            batShouldSleep = true;
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
        if (_batWarnStart != 0) {
            Serial.println("BAT: tensione risalita — preavviso annullato.");
        }
        _batWarnStart  = 0;
        batLowWarning  = false;
    }

    Serial.print("BAT: ");
    Serial.print(batVoltage, 2);
    Serial.print(" V ");
    Serial.print((int)batPercent);
    Serial.print("% ");
    Serial.println(batCharging ? "[CARICA]" : "[SCARICA]");
}

inline int getBatteryHistorySorted(float* out) {
    for (int i = 0; i < batHistorySize; i++) {
        out[i] = _historyAt(i);
    }
    return batHistorySize;
}
