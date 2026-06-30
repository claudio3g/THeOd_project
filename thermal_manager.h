#pragma once
#include <Arduino.h>
#include "config.h"
#include "shared_state.h"

/*
 * ============================================================================
 * thermal_manager.h — Gestione termica del sistema
 *
 * FILOSOFIA:
 *   Il firmware non chiede "Quanto è la temperatura?" ma
 *   "Qual è lo stato termico del sistema?".
 *   Tutti i moduli leggono thermalState, mai la temperatura diretta.
 *
 * SORGENTI (zero hardware aggiuntivo):
 *   ESP32:  temperatureRead() — die temperature, EMA filtrata
 *   SX1276: registro 0x3C    — die temperature LoRa, lettura sicura via STANDBY
 *
 * STATI TERMICI (5 livelli):
 *   NORMAL    < 60°C  — funzionamento normale
 *   ELEVATED  60-70°C — monitoraggio, nessuna azione hardware
 *   WARNING   70-80°C — riduzione refresh OLED (500ms → 1000ms)
 *   CRITICAL  80-90°C — OLED spento, riduzione log seriale
 *   EMERGENCY > 90°C  — protezione massima, LED allarme
 *
 * ISTERESI:
 *   Ogni stato si attiva a T_soglia ma si disattiva solo a T_soglia - 2°C.
 *   Evita oscillazioni quando la temperatura è vicina alla soglia.
 *
 * FILTRO EMA:
 *   espTempFiltered = 0.15 * raw + 0.85 * prev
 *   Costante di tempo ~3 minuti — immune a spike di breve durata.
 *
 * STATISTICHE:
 *   Min/max sessione aggiornati ad ogni lettura.
 *   Trend calcolato su finestra scorrevole TEMP_TREND_SAMPLES (6 campioni = 3 min).
 *
 * AZIONI AUTOMATICHE (implementate progressivamente):
 *   v5: solo notifica (display, web, seriale)
 *   v6+: azioni hardware (riduzione refresh, spegnimento OLED, LED allarme)
 *
 * INCLUDE ORDER:
 *   thermal_manager.h va incluso DOPO lora_handler.h (usa _loraReadReg/_loraWriteReg)
 *   e PRIMA di display.h (display legge thermalState per adattare refresh).
 * ============================================================================
 */

// ---------------------------------------------------------------------------
// ThermalState — enum centrale del sistema termico
// Tutti i moduli usano questo tipo, MAI la temperatura diretta.
// ---------------------------------------------------------------------------
enum class ThermalState : uint8_t {
    NORMAL    = 0,
    ELEVATED  = 1,
    WARNING   = 2,
    CRITICAL  = 3,
    EMERGENCY = 4
};

// ---------------------------------------------------------------------------
// Buffer scorrevole per il calcolo del trend
// ---------------------------------------------------------------------------
static float _tempHistory[TEMP_TREND_SAMPLES] = {};
static int   _tempHistHead = 0;
static int   _tempHistSize = 0;
static bool  _tempFirstRead = true;

// ---------------------------------------------------------------------------
// _readLoraTemp() — legge temperatura SX1276 in modo sicuro
//
// BUG RISOLTO (v5.1): il sensore di temperatura SX1276 NON si aggiorna
// automaticamente in Standby. Il datasheet (Rev.7 §5.5.7) specifica che
// va "triggerato" con una transizione esplicita SLEEP → STANDBY:
//   "The temperature sensor must be triggered... by setting the device
//    in Sleep mode then to Standby mode."
// La versione precedente faceva RX_CONT → STANDBY direttamente, senza
// passare per SLEEP: il registro RegTemp restava fissato al valore letto
// durante initLora() (SLEEP→STANDBY al boot, chip ancora freddo) → 15°C
// costante per tutta la sessione, mai più aggiornato.
//
// Sequenza corretta: RX_CONT → SLEEP → STANDBY (trigger) → leggi → RX_CONT
// Costo aggiuntivo: solo qualche µs in più per il passaggio SLEEP, trascurabile.
//
// Salta la lettura se c'è un pacchetto in arrivo (IRQ RX_DONE attivo)
// per non interrompere la ricezione.
// Formula datasheet SX1276 Rev.7 §5.5.7: T(°C) = 15 - (int8_t)RegTemp
// ---------------------------------------------------------------------------
static float _readLoraTemp() {
    if (!loraReady) return -999.0f;

    // Non interrompere se c'è un pacchetto in arrivo
    if (_loraReadReg(SX1276_REG_IRQ_FLAGS) & SX1276_IRQ_RX_DONE) return -999.0f;

    // Trigger del sensore: SLEEP poi STANDBY (richiesto dal datasheet)
    _loraWriteReg(SX1276_REG_OP_MODE, SX1276_MODE_SLEEP);
    delayMicroseconds(200);  // Breve pausa per la transizione di modalità
    _loraWriteReg(SX1276_REG_OP_MODE, SX1276_MODE_STDBY);
    delay(2);  // Tempo di conversione del sensore (~1-2ms da datasheet)

    float temp = 15.0f - (float)(int8_t)_loraReadReg(0x3C);

    _loraWriteReg(SX1276_REG_OP_MODE, SX1276_MODE_RX_CONT);

    return (temp >= -40.0f && temp <= 125.0f) ? temp : -999.0f;
}

// ---------------------------------------------------------------------------
// _updateState() — macchina a stati con isteresi bidirezionale
//
// Regola: si sale di stato quando T >= soglia_ingresso
//         si scende di stato quando T < soglia_ingresso - HYSTERESIS
// Questo garantisce che oscillazioni di ±1°C non causino toggle di stato.
// ---------------------------------------------------------------------------
static void _updateState(float t) {
    ThermalState current = (ThermalState)thermalState;

    // Soglie di ingresso (scalata crescente)
    const float enter[4] = {
        TEMP_ELEVATED_C,
        TEMP_WARNING_C,
        TEMP_CRITICAL_C,
        TEMP_EMERGENCY_C
    };

    // Determina lo stato corretto dall'alto verso il basso
    ThermalState newState = ThermalState::NORMAL;
    for (int i = 3; i >= 0; i--) {
        if (t >= enter[i]) {
            newState = (ThermalState)(i + 1);
            break;
        }
    }

    // Applica isteresi in discesa: non scendere se ancora sopra (soglia - hyst)
    if (newState < current) {
        int ci = (int)current;
        if (ci > 0) {
            float exitThreshold = enter[ci - 1] - TEMP_HYSTERESIS_C;
            if (t >= exitThreshold) {
                newState = current;  // Mantieni stato corrente
            }
        }
    }

    if (newState != current) {
        Serial.print("THERMAL: stato ");
        Serial.print(thermalState);
        Serial.print(" -> ");
        Serial.println((int)newState);
    }

    thermalState = (int)newState;
}

// ---------------------------------------------------------------------------
// _updateTrend() — calcola RISING/STABLE/FALLING su finestra scorrevole
//
// Divide la finestra in due metà:
//   trend = media(campioni recenti) - media(campioni precedenti)
//   > +THRESHOLD → RISING  (+1)
//   < -THRESHOLD → FALLING (-1)
//   altrimenti   → STABLE  ( 0)
// ---------------------------------------------------------------------------
static void _updateTrend(float newTemp) {
    // Inserisci nel buffer circolare
    _tempHistory[_tempHistHead] = newTemp;
    _tempHistHead = (_tempHistHead + 1) % TEMP_TREND_SAMPLES;
    if (_tempHistSize < TEMP_TREND_SAMPLES) _tempHistSize++;

    if (_tempHistSize < TEMP_TREND_SAMPLES) {
        thermalTrend = 0;  // Non abbastanza campioni ancora
        return;
    }

    // Metà recente vs metà precedente
    int half = TEMP_TREND_SAMPLES / 2;
    float sumRecent = 0, sumOld = 0;

    for (int i = 0; i < half; i++) {
        // Campioni recenti: gli ultimi 'half' inseriti
        int idxRecent = (_tempHistHead - 1 - i + TEMP_TREND_SAMPLES) % TEMP_TREND_SAMPLES;
        sumRecent += _tempHistory[idxRecent];

        // Campioni vecchi: i 'half' prima dei recenti
        int idxOld = (_tempHistHead - 1 - half - i + TEMP_TREND_SAMPLES) % TEMP_TREND_SAMPLES;
        sumOld += _tempHistory[idxOld];
    }

    float delta = (sumRecent - sumOld) / (float)half;
    if      (delta >  TEMP_TREND_THRESHOLD) thermalTrend =  1;  // RISING
    else if (delta < -TEMP_TREND_THRESHOLD) thermalTrend = -1;  // FALLING
    else                                    thermalTrend =  0;  // STABLE
}

// ---------------------------------------------------------------------------
// _applyThermalActions() — azioni automatiche per stato termico
//
// v5: solo log seriale, nessuna azione hardware.
//     Le azioni hardware (OLED, LED, refresh) saranno aggiunte in v6
//     senza modificare questa interfaccia — basta aggiungere casi qui.
// ---------------------------------------------------------------------------
static void _applyThermalActions(ThermalState state) {
    switch (state) {
        case ThermalState::NORMAL:
        case ThermalState::ELEVATED:
            // Nessuna azione — funzionamento normale
            break;

        case ThermalState::WARNING:
            // v6: ridurre refresh OLED, ridurre LED brightness
            // Per ora: solo segnalazione
            break;

        case ThermalState::CRITICAL:
            // v6: spegnere OLED, ridurre polling LoRa
            break;

        case ThermalState::EMERGENCY:
            // v6: OLED spento, LED allarme, riduzione massima carichi
            break;
    }
}

// ---------------------------------------------------------------------------
// API PUBBLICA
// ---------------------------------------------------------------------------

// thermalStateStr() — nome testuale dello stato per display e web
inline const char* thermalStateStr() {
    switch ((ThermalState)thermalState) {
        case ThermalState::NORMAL:    return "NORMAL";
        case ThermalState::ELEVATED:  return "ELEVATED";
        case ThermalState::WARNING:   return "WARNING";
        case ThermalState::CRITICAL:  return "CRITICAL";
        case ThermalState::EMERGENCY: return "EMERGENCY";
        default:                      return "?";
    }
}

// thermalTrendChar() — carattere Unicode trend per display
inline const char* thermalTrendChar() {
    if (thermalTrend >  0) return "\xE2\x96\xB2";  // UTF-8: ▲
    if (thermalTrend <  0) return "\xE2\x96\xBC";  // UTF-8: ▼
    return "\xE2\x80\x94";                          // UTF-8: —
}

// Comodi predicati — i moduli li usano invece di leggere thermalState direttamente
inline bool thermalIsNormal()    { return thermalState == (int)ThermalState::NORMAL; }
inline bool thermalIsElevated()  { return thermalState >= (int)ThermalState::ELEVATED; }
inline bool thermalIsWarning()   { return thermalState >= (int)ThermalState::WARNING; }
inline bool thermalIsCritical()  { return thermalState >= (int)ThermalState::CRITICAL; }
inline bool thermalIsEmergency() { return thermalState == (int)ThermalState::EMERGENCY; }

// ---------------------------------------------------------------------------
// initThermal() — inizializzazione variabili al boot
// ---------------------------------------------------------------------------
inline void initThermal() {
    thermalState    = (int)ThermalState::NORMAL;
    espTempRaw      = 0.0f;
    espTempFiltered = 0.0f;
    espTempMin      = -1.0f;   // -1 = non ancora letto
    espTempMax      = -1.0f;
    loraTemp        = -999.0f;
    thermalTrend    = 0;
    _tempFirstRead  = true;
    _tempHistSize   = 0;
    _tempHistHead   = 0;
    Serial.println("Thermal: OK");
}

// ---------------------------------------------------------------------------
// thermalUpdate() — lettura e aggiornamento completo (ogni TEMP_READ_INTERVAL_MS)
//
// Sequenza:
//   1. Leggi ESP32 raw → applica offset → applica EMA
//   2. Aggiorna min/max
//   3. Leggi SX1276 (con switch STANDBY sicuro)
//   4. Aggiorna trend (buffer scorrevole)
//   5. Aggiorna stato con isteresi
//   6. Applica azioni per stato
// ---------------------------------------------------------------------------
inline void thermalUpdate() {
    // 1. Temperatura ESP32
    espTempRaw = temperatureRead() + TEMP_ESP_OFFSET_C;

    if (_tempFirstRead) {
        // Prima lettura: inizializza EMA con il valore reale (no transiente)
        espTempFiltered = espTempRaw;
        espTempMin      = espTempRaw;
        espTempMax      = espTempRaw;
        _tempFirstRead  = false;
    } else {
        // EMA: filtro la temperatura per evitare reazioni a spike brevi
        espTempFiltered = TEMP_EMA_ALPHA * espTempRaw
                        + (1.0f - TEMP_EMA_ALPHA) * espTempFiltered;
    }

    // 2. Aggiorna min/max sessione
    if (espTempFiltered < espTempMin || espTempMin < 0.0f) espTempMin = espTempFiltered;
    if (espTempFiltered > espTempMax)                      espTempMax = espTempFiltered;

    // 3. Temperatura SX1276
    float lt = _readLoraTemp();
    if (lt > -999.0f) loraTemp = lt;

    // 4. Trend su temperatura filtrata
    _updateTrend(espTempFiltered);

    // 5. Stato termico con isteresi
    _updateState(espTempFiltered);

    // 6. Azioni automatiche
    _applyThermalActions((ThermalState)thermalState);

    // Log seriale compatto
    Serial.print("THERMAL: ESP=");
    Serial.print(espTempFiltered, 1);
    Serial.print("°C(raw=");
    Serial.print(espTempRaw, 1);
    Serial.print(") min=");
    Serial.print(espTempMin, 1);
    Serial.print(" max=");
    Serial.print(espTempMax, 1);
    Serial.print(" trend=");
    Serial.print(thermalTrendChar());
    Serial.print(" stato=");
    Serial.print(thermalStateStr());
    if (loraTemp > -999.0f) {
        Serial.print(" LoRa=");
        Serial.print(loraTemp, 1);
        Serial.print("°C");
    }
    Serial.println();
}
