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
 *   thermal_manager.h va incluso DOPO lora_handler.h (usa loraDisable/loraEnable)
 *   e DOPO display.h (usa setOledEnabled per le azioni CRITICAL/EMERGENCY).
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
// _readLoraTemp() — DISABILITATO (v5.3): sensore hardware non funzionante
//
// DIAGNOSI COMPLETA (v5.1 → v5.3):
//   v5.1: implementata la sequenza di trigger SLEEP→STANDBY richiesta dal
//         datasheet (Rev.7 §5.5.7). RegTemp restava comunque fisso a 0x00.
//   v5.2: aumentati i tempi di transizione da 200µs/2ms a 5ms/5ms,
//         aggiunto log diagnostico del valore raw del registro.
//
//   Risultato test sul campo: RegTemp = 0x00 costante in ogni lettura,
//   indipendentemente dal timing. Nello stesso periodo:
//     - RSSI variava normalmente (-87 a -125 dBm) → bus SPI funzionante
//     - GPS, batteria, tutti gli altri moduli SPI/I2C rispondevano correttamente
//   Questo esclude un problema di comunicazione SPI generale.
//
// CONCLUSIONE: il sensore di temperatura interno di questo specifico chip
// SX1276 (o clone) non è funzionante/calibrato in silicio. È un limite
// hardware noto su alcuni moduli LoRa economici, non risolvibile via firmware.
//
// La funzione resta presente (ritorna sempre N/D) per non rompere
// l'interfaccia pubblica di thermal_manager.h. Se in futuro si monterà
// un modulo SX1276 con sensore funzionante, basterà ripristinare il corpo
// precedente (vedi commit "fix(thermal): SX1276 temperature sensor..." in git log).
// ---------------------------------------------------------------------------
static float _readLoraTemp() {
    return -999.0f;  // Sensore hardware non disponibile su questa unità
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
// PRINCIPI:
//   - Idempotente: chiamata ogni 30s, non fa danni se già nello stato corretto
//   - Reversibile: quando la temperatura scende sotto la soglia di isteresi,
//     _updateState() ritorna allo stato inferiore e le azioni vengono annullate
//   - Non sovrascrive scelte manuali utente: non tocca ledOverride se già impostato
//     dall'utente (override != 0), non riattiva LoRa se l'utente lo ha disattivato
//   - Ogni caso gestisce ANCHE il ripristino dall'azione del caso superiore,
//     così la discesa di stato annulla automaticamente le azioni precedenti
//
// AZIONI PER STATO:
//   NORMAL/ELEVATED : ripristino completo (nel caso si scenda da stati superiori)
//   WARNING         : refresh OLED rallentato 500ms → 1000ms
//   CRITICAL        : OLED spento (setOledEnabled false)
//   EMERGENCY       : OLED spento + LoRa sleep + LED allarme
// ---------------------------------------------------------------------------
static void _applyThermalActions(ThermalState state) {
    switch (state) {

        case ThermalState::NORMAL:
        case ThermalState::ELEVATED:
            // Ripristino completo: annulla eventuali azioni degli stati superiori.
            // Chiamata ogni 30s — le funzioni controllano lo stato attuale
            // e sono no-op se già nel valore corretto (idempotenti).
            oledRefreshMs = OLED_REFRESH_NORMAL_MS;   // 500ms
            if (!oledEnabled) {
                setOledEnabled(true);
                Serial.println("THERMAL: OLED riacceso (temperatura rientrata).");
            }
            // Riattiva LoRa solo se era stato sospeso dal Thermal Manager
            // (non toccare se l'utente lo ha disattivato manualmente)
            if (loraManualDisable == false && !loraReady) {
                loraEnable();
                Serial.println("THERMAL: LoRa riattivato (temperatura rientrata).");
            }
            // LED: rimuovi allarme termico solo se non c'è un override manuale utente
            if (ledOverride == 3) {  // 3 = allarme termico (non toccare 1=off/2=on utente)
                ledOverride = 0;
                Serial.println("THERMAL: LED allarme rimosso (temperatura rientrata).");
            }
            break;

        case ThermalState::WARNING:
            // Azione leggera: rallenta il refresh OLED per ridurre carico I2C/CPU.
            // Non spegne il display — l'utente vede ancora le informazioni.
            oledRefreshMs = OLED_REFRESH_WARNING_MS;  // 1000ms
            // Ripristina OLED se era stato spento da CRITICAL (temperatura scesa)
            if (!oledEnabled) {
                setOledEnabled(true);
                Serial.println("THERMAL: OLED riacceso (rientrato da CRITICAL a WARNING).");
            }
            break;

        case ThermalState::CRITICAL:
            // Spegni il display (0xAE): risparmio ~15mA e riduzione calore OLED.
            // Il display si riaccenderà automaticamente quando la temperatura
            // scende sotto CRITICAL - HYSTERESIS (78°C) → stato WARNING → NORMAL.
            oledRefreshMs = OLED_REFRESH_WARNING_MS;  // Mantieni rallentato
            if (oledEnabled) {
                setOledEnabled(false);
                Serial.println("THERMAL [CRITICAL]: OLED spento automaticamente.");
            }
            break;

        case ThermalState::EMERGENCY:
            // Protezione massima: OLED + LoRa spenti + LED allarme.
            // LoRa in sleep: da ~10mA a ~0.2µA, riduce anche il calore del chip.
            // LED override 3 = allarme termico (distinto da 1=off/2=on utente).
            if (oledEnabled) {
                setOledEnabled(false);
                Serial.println("THERMAL [EMERGENCY]: OLED spento automaticamente.");
            }
            if (!loraManualDisable) {
                loraDisable();
                Serial.println("THERMAL [EMERGENCY]: LoRa sospeso automaticamente.");
            }
            if (ledOverride == 0) {  // Solo se nessun override utente attivo
                ledOverride = 3;    // Allarme termico: LED pulse veloce (vedi led_control.h)
                Serial.println("THERMAL [EMERGENCY]: LED allarme attivato.");
            }
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
