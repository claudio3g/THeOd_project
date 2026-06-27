#pragma once
#include <Arduino.h>
#include <SPI.h>
#include "config.h"
#include "shared_state.h"

/*
 * ============================================================================
 * lora_handler.h — Gestione modulo LoRa SX1276 onboard (Heltec LoRa 32 V2)
 *
 * FUNZIONALITÀ v3:
 *   - Inizializzazione SX1276 via SPI dedicato
 *   - Lettura RSSI e SNR dell'ultimo pacchetto ricevuto
 *   - Polling non bloccante (nessun delay nel loop)
 *   - Predisposizione stub per Meshtastic (callback RX vuota)
 *
 * NOTA ARCHITETTURALE:
 *   Questo modulo usa accesso diretto ai registri SX1276 tramite SPI,
 *   senza dipendenze da librerie esterne (LoRa.h o RadioLib), per
 *   massima compatibilità e minimo footprint.
 *   Quando si integra Meshtastic, sostituire con la sua libreria radio.
 *
 * PIN SPI (Heltec WiFi LoRa 32 V2 — cablati sul PCB):
 *   SCK=5, MISO=19, MOSI=27, NSS=18, RST=14, DIO0=26
 * ============================================================================
 */

// ---------------------------------------------------------------------------
// Registri SX1276 necessari per RSSI e SNR
// (datasheet SX1276 Rev.7, sezione 6.4)
// ---------------------------------------------------------------------------
#define SX1276_REG_VERSION       0x42  // Deve rispondere 0x12
#define SX1276_REG_OP_MODE       0x01
#define SX1276_REG_PKT_RSSI      0x1A  // RSSI dell'ultimo pacchetto
#define SX1276_REG_PKT_SNR       0x19  // SNR dell'ultimo pacchetto (signed /4)
#define SX1276_REG_RSSI_VALUE    0x1B  // RSSI corrente (canale, non pacchetto)
#define SX1276_REG_MODEM_CONFIG1 0x1D
#define SX1276_REG_MODEM_CONFIG2 0x1E
#define SX1276_REG_FREQ_MSB      0x06
#define SX1276_REG_FREQ_MID      0x07
#define SX1276_REG_FREQ_LSB      0x08
#define SX1276_REG_PA_CONFIG     0x09
#define SX1276_REG_LNA           0x0C
#define SX1276_REG_FIFO_ADDR_PTR 0x0D

// Modalità operativa LoRa (bit 7 = LoRa mode)
#define SX1276_MODE_SLEEP        0x80  // LoRa + Sleep
#define SX1276_MODE_STDBY        0x81  // LoRa + Standby
#define SX1276_MODE_RX_CONT      0x85  // LoRa + RX continuo
#define SX1276_MODE_RX_SINGLE    0x86  // LoRa + RX singolo

// SPI helper privato
static SPIClass _loraSpi(VSPI);

static uint8_t _loraReadReg(uint8_t reg) {
    digitalWrite(LORA_NSS_PIN, LOW);
    _loraSpi.transfer(reg & 0x7F);  // bit7=0 = lettura
    uint8_t val = _loraSpi.transfer(0x00);
    digitalWrite(LORA_NSS_PIN, HIGH);
    return val;
}

static void _loraWriteReg(uint8_t reg, uint8_t val) {
    digitalWrite(LORA_NSS_PIN, LOW);
    _loraSpi.transfer(reg | 0x80);  // bit7=1 = scrittura
    _loraSpi.transfer(val);
    digitalWrite(LORA_NSS_PIN, HIGH);
}

// ---------------------------------------------------------------------------
// initLora() — inizializzazione SX1276, ritorna true se chip risponde.
//
// Sequenza:
//   1. Reset hardware (RST LOW 10ms → HIGH)
//   2. SPI begin su bus VSPI con pin dedicati
//   3. Verifica firma 0x12 nel registro VERSION
//   4. Imposta modalità LoRa Sleep → configura SF/BW/CR/freq → RX continuo
// ---------------------------------------------------------------------------
inline bool initLora() {
    loraReady    = false;
    loraRssi     = 0;
    loraSnr      = 0.0f;
    meshNodeCount = 0;
    meshLastRx    = 0;

    // Configura pin SPI e NSS
    pinMode(LORA_NSS_PIN, OUTPUT);
    digitalWrite(LORA_NSS_PIN, HIGH);
    pinMode(LORA_RST_PIN, OUTPUT);
    pinMode(LORA_DIO0_PIN, INPUT);

    // Reset hardware SX1276
    digitalWrite(LORA_RST_PIN, LOW);
    delay(10);
    digitalWrite(LORA_RST_PIN, HIGH);
    delay(10);

    // Inizializza bus SPI su pin dedicati LoRa
    _loraSpi.begin(LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN, LORA_NSS_PIN);
    _loraSpi.setFrequency(1000000);  // 1 MHz: sicuro per SX1276

    // Verifica firma del chip
    uint8_t version = _loraReadReg(SX1276_REG_VERSION);
    if (version != 0x12) {
        Serial.print("LoRa: chip non risponde (VERSION=0x");
        Serial.print(version, HEX);
        Serial.println(" atteso 0x12)");
        return false;
    }

    // Entra in Sleep LoRa per poter modificare i registri
    _loraWriteReg(SX1276_REG_OP_MODE, SX1276_MODE_SLEEP);
    delay(10);

    // Imposta frequenza: fstep = 32MHz/2^19 = 61.03515625 Hz
    // FRF = freq / fstep
    uint32_t frf = (uint32_t)((double)LORA_FREQ_HZ / 61.03515625);
    _loraWriteReg(SX1276_REG_FREQ_MSB, (uint8_t)(frf >> 16));
    _loraWriteReg(SX1276_REG_FREQ_MID, (uint8_t)(frf >>  8));
    _loraWriteReg(SX1276_REG_FREQ_LSB, (uint8_t)(frf >>  0));

    // Modem Config 1: BW=125kHz(0x7), CR=4/5(0x1), implicit header off
    // Bits [7:4]=BW, [3:1]=CR, [0]=ImplicitHeaderModeOn
    _loraWriteReg(SX1276_REG_MODEM_CONFIG1, 0x72);  // BW=125k, CR=4/5, explicit

    // Modem Config 2: SF=9(0x9), TX cont off, CRC on
    // Bits [7:4]=SF, [3]=TxContMode, [2]=RxPayloadCrcOn, [1:0]=SymbTimeout MSB
    _loraWriteReg(SX1276_REG_MODEM_CONFIG2, 0x94);  // SF9, CRC on

    // PA: boost pin (GPIO PA_BOOST su Heltec), potenza = 17 dBm
    // Bits [7]=PaSelect(1=PA_BOOST), [6:4]=MaxPower, [3:0]=OutputPower
    // OutputPower = Pout - 2 per PA_BOOST → 17-2=15=0x0F
    _loraWriteReg(SX1276_REG_PA_CONFIG, 0x8F);

    // LNA: gain massimo, LnaBoostHf ON (migliora NF)
    _loraWriteReg(SX1276_REG_LNA, 0x23);

    // Standby prima di RX
    _loraWriteReg(SX1276_REG_OP_MODE, SX1276_MODE_STDBY);
    delay(5);

    // RX continuo: il modulo rimane in ascolto
    _loraWriteReg(SX1276_REG_OP_MODE, SX1276_MODE_RX_CONT);

    loraReady = true;
    Serial.print("LoRa: OK — ");
    Serial.print(LORA_FREQ_HZ / 1000000.0f, 1);
    Serial.println(" MHz, RX continuo attivo");
    return true;
}

// ---------------------------------------------------------------------------
// loraUpdate() — chiamata periodica dal loop() ogni LORA_RSSI_INTERVAL_MS
//
// Legge RSSI corrente del canale (non richiede pacchetto ricevuto).
// Legge anche RSSI/SNR dell'ultimo pacchetto se DIO0 lo segnala.
//
// NOTA: DIO0 = IRQ_RX_DONE. Qui usiamo polling per semplicità;
// il flag viene controllato e resettato leggendo REG_IRQ_FLAGS.
// ---------------------------------------------------------------------------
#define SX1276_REG_IRQ_FLAGS  0x12
#define SX1276_IRQ_RX_DONE    0x40  // bit6

inline void loraUpdate() {
    if (!loraReady) return;

    // Leggi RSSI istantaneo del canale (disponibile sempre in RX_CONT)
    // Formula: RSSI = -157 + RegRssiValue  (per HF, >779 MHz)
    uint8_t rawRssi = _loraReadReg(SX1276_REG_RSSI_VALUE);
    int channelRssi = -157 + (int)rawRssi;

    // Controlla se c'è un nuovo pacchetto ricevuto (IRQ RX_DONE)
    uint8_t irqFlags = _loraReadReg(SX1276_REG_IRQ_FLAGS);
    if (irqFlags & SX1276_IRQ_RX_DONE) {
        // Nuovo pacchetto: leggi RSSI e SNR del pacchetto
        uint8_t rawPktRssi = _loraReadReg(SX1276_REG_PKT_RSSI);
        int8_t  rawSnr     = (int8_t)_loraReadReg(SX1276_REG_PKT_SNR);

        // Formula RSSI pacchetto (HF): RSSI = -157 + RegPktRssi
        // Correzione SNR se negativo (bassa potenza)
        int pktRssi = -157 + (int)rawPktRssi;
        if (rawSnr < 0) {
            // SNR < 0: RSSI stimato meno accurato
            pktRssi += (int)(rawSnr / 4);  // correzione SNR
        }

        loraRssi = pktRssi;
        loraSnr  = (float)rawSnr / 4.0f;   // SNR in dB (LSB = 0.25 dB)
        meshLastRx = millis();

        // Resetta flag IRQ (scrittura 1 = clear)
        _loraWriteReg(SX1276_REG_IRQ_FLAGS, SX1276_IRQ_RX_DONE);

        Serial.print("LoRa RX: RSSI=");
        Serial.print(loraRssi);
        Serial.print("dBm SNR=");
        Serial.print(loraSnr, 1);
        Serial.println("dB");

        // --- STUB MESHTASTIC ---
        // Quando MESHTASTIC_ENABLED sarà attivo, qui si chiamerà:
        //   mesh.handleRxPacket(fifoBuffer, packetLength);
        // Per ora incrementiamo solo un contatore di prova
        meshNodeCount++;  // placeholder: sarà rimosso con l'integrazione reale
    } else {
        // Nessun pacchetto: usa RSSI canale come indicatore di presenza segnale
        // Aggiorna solo se non abbiamo un RSSI pacchetto recente (< 10 s)
        if (meshLastRx == 0 || (millis() - meshLastRx > 10000)) {
            loraRssi = channelRssi;
            loraSnr  = 0.0f;
        }
    }
}

// ---------------------------------------------------------------------------
// loraRssiLabel() — restituisce stringa qualità segnale per display/web
// ---------------------------------------------------------------------------
inline const char* loraRssiLabel() {
    if (!loraReady) return "N/D";
    if (loraRssi == 0) return "---";
    if (loraRssi >= LORA_RSSI_GOOD) return "Buono";
    if (loraRssi >= LORA_RSSI_FAIR) return "OK";
    return "Debole";
}

// ---------------------------------------------------------------------------
// loraSleep() — mette SX1276 in modalità sleep per deep sleep ESP32
// Da chiamare in doDeepSleep() prima di esp_deep_sleep_start()
// ---------------------------------------------------------------------------
inline void loraSleep() {
    if (!loraReady) return;
    // Usa il bus SPI privato _loraSpi (già inizializzato da initLora)
    _loraWriteReg(SX1276_REG_OP_MODE, SX1276_MODE_SLEEP);
    Serial.println("LoRa: modalita' sleep.");
}
