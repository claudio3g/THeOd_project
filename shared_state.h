#pragma once
#include <Arduino.h>
#include "config.h"

/*
 * ============================================================================
 * shared_state.h — dichiarazioni 'extern' di tutte le variabili condivise.
 *
 * MODIFICHE v3:
 *   - Aggiunte variabili filtro EMA per Touch e Hall (touchFiltered, hallFiltered)
 *   - Aggiunte variabili LoRa: loraRssi, loraSnr, loraReady
 *   - Predisposto stub Meshtastic: meshNodeCount, meshLastRx
 * ============================================================================
 */

// --- Sensori: valori raw e filtrati ---
extern int   touchValue;       // Valore raw touchRead() — per debug
extern int   hallValue;        // Valore raw hallRead()  — per debug
extern float touchFiltered;    // Valore EMA filtrato (float per precisione filtro)
extern float hallFiltered;     // Valore EMA filtrato (float per precisione filtro)
extern bool  touchActive;      // true = dito rilevato (con isteresi)
extern bool  magnetDetected;   // true = magnete vicino (con isteresi)
extern bool  buttonPressed;    // true = tasto BOOT premuto (dopo debounce)

// --- Batteria ---
extern float batVoltage;       // Tensione batteria in V (-1 = N/D)
extern float batPercent;       // Percentuale 0–100 (-1 = N/D)
extern bool  batCharging;      // true = batteria in carica
extern bool  batLowWarning;    // true = avviso batteria bassa attivo
extern int   batTimeToFull;    // Minuti stimati a piena carica (-1 = N/D)
extern int   batTimeToEmpty;   // Minuti stimati a scarica (-1 = N/D)
extern bool  batEstimating;    // true = stima ETA non ancora disponibile
extern bool  batShouldSleep;   // true = richiesta deep sleep per batteria

extern float batHistory[BATTERY_HISTORY_POINTS];
extern int   batHistoryHead;   // Testa del ring buffer
extern int   batHistorySize;   // Elementi validi nel buffer

// --- LoRa / Meshtastic ---
extern int   loraRssi;         // RSSI ultimo pacchetto ricevuto (dBm), 0 = N/D
extern float loraSnr;          // SNR ultimo pacchetto (dB), 0.0 = N/D
extern bool  loraReady;        // true = modulo LoRa inizializzato OK

// Stub Meshtastic (variabili pronte, non usate finché MESHTASTIC_ENABLED)
extern int   meshNodeCount;    // Nodi vicini rilevati nella mesh
extern unsigned long meshLastRx; // Timestamp ultimo pacchetto mesh ricevuto

// --- Sistema ---
extern bool  oledEnabled;      // true = display attivo
extern bool  batSkipNextRead;  // true = salta prossima lettura ADC (transiente post-toggle OLED)
extern int   ledOverride;      // 0=auto (logica normale), 1=forza spento, 2=forza acceso
extern int   wifiClients;      // Client connessi all'AP
extern char  apIpStr[16];      // IP AP in formato "192.168.4.1"
