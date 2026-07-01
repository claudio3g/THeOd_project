#pragma once
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"
#include "shared_state.h"
#include "led_control.h"
// gps_handler.h incluso dopo per accesso a gpsFix, gpsLat, ecc.
// Le variabili GPS sono in shared_state.h (extern)
#include "shared_state.h"
#include "led_control.h"

/*
 * ============================================================================
 * display.h — Gestione OLED SSD1306 128×64 integrato
 *
 * LAYOUT OLED v3 (8 righe × 8px = 64px):
 *   Riga 0 (y=0):  Batteria: tensione, %, stato carica
 *   Riga 1 (y=8):  Stima ETA (Piena/Scarica/Stabile/Stima...)
 *   Riga 2 (y=16): Touch: valore filtrato + stato
 *   Riga 3 (y=24): Hall: valore filtrato + magnete + pulsante
 *   Riga 4 (y=32): WiFi client + avviso batteria bassa
 *   Riga 5 (y=40): IP Access Point
 *   Riga 6 (y=48): LoRa RSSI (dBm) + qualità + SNR  ← NUOVO v3
 *   Riga 7 (y=56): LED pattern + OLED stato
 *
 * MODIFICHE v3:
 *   - Riga 6: sostituisce vecchia riga "LED+OLED" con info LoRa
 *   - Riga 7: LED pattern + OLED (spostati di una riga)
 *   - updateDisplay() ora mostra touchFiltered e hallFiltered (valori stabili)
 *     invece dei valori raw (che cambiano troppo velocemente)
 *   - In caso di batLowWarning: riga 7 mostra "!! BATTERIA BASSA !!"
 *     (invariato rispetto a v2, ma non sovrascrive più LoRa)
 * ============================================================================
 */

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RST_PIN);
bool displayOk = false;

// Invia comando direttamente all'SSD1306 via I2C (per ON/OFF display)
static void _oledCommand(uint8_t cmd) {
    Wire.beginTransmission(OLED_I2C_ADDR);
    Wire.write(0x00);  // Co=0, D/C#=0 → command byte
    Wire.write(cmd);
    Wire.endTransmission();
}

static const char* _ledPatternName(LedPattern p) {
    switch (p) {
        case LED_OFF:   return "off";
        case LED_DIM:   return "dim";
        case LED_FADE:  return "fade";
        case LED_SOLID: return "full";
        case LED_PULSE: return "puls";
        default:        return "?";
    }
}

// ---------------------------------------------------------------------------
// initDisplay() — inizializzazione OLED con reset hardware e splash screen
// ---------------------------------------------------------------------------
inline void initDisplay() {
    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);

    // Reset hardware SSD1306 (sequenza HIGH→LOW→HIGH)
    pinMode(OLED_RST_PIN, OUTPUT);
    digitalWrite(OLED_RST_PIN, HIGH); delay(10);
    digitalWrite(OLED_RST_PIN, LOW);  delay(10);
    digitalWrite(OLED_RST_PIN, HIGH); delay(10);

    displayOk = display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR);

    if (!displayOk) {
        Serial.println("OLED: non risponde.");
        return;
    }

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("THeOd Project v3");
    display.println("Heltec LoRa 32 V2");
    display.println("");
    display.println("Avvio in corso...");
    display.display();
    Serial.println("OLED: OK");
}

// ---------------------------------------------------------------------------
// setOledEnabled() — accende o spegne il display via comando I2C 0xAF/0xAE
// ---------------------------------------------------------------------------
inline void setOledEnabled(bool enabled) {
    if (!displayOk) return;
    oledEnabled = enabled;
    _oledCommand(enabled ? 0xAF : 0xAE);
    // Quando il display cambia stato, il consumo corrente varia di ~15mA.
    // Questo causa un transiente sulla tensione batteria (+0.02~0.05V a spegnimento)
    // che farebbe scattare falsamente batCharging=true nel ciclo successivo.
    // Il flag dice a batteryUpdate() di saltare UNA lettura per lasciar stabilizzare.
    batSkipNextRead = true;
    if (enabled) {
        display.clearDisplay();
        display.display();
    }
}

// ---------------------------------------------------------------------------
// updateDisplay() — aggiorna il layout OLED (chiamata ogni 500 ms dal loop)
//
// Mostra touchFiltered e hallFiltered (stabili via EMA) invece dei raw.
// Aggiunge riga LoRa RSSI con qualità segnale.
// ---------------------------------------------------------------------------
inline void updateDisplay(LedPattern ledPat) {
    if (!displayOk || !oledEnabled) return;

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // --- Riga 0 (y=0): Batteria tensione/percentuale/stato ---
    display.setCursor(0, 0);
    if (batVoltage < 0.0f) {
        display.print("BAT: N/D");
    } else {
        display.print(batVoltage, 2);
        display.print("V ");
        if (batPercent >= 0.0f) display.print((int)batPercent);
        else display.print("?");
        display.print("% ");
        display.print(batCharging ? "[CAR]" : "[SCA]");
    }

    // --- Riga 1 (y=8): Stima ETA ---
    display.setCursor(0, 8);
    if (batEstimating) {
        display.print("Stima in corso...");
    } else if (batCharging && batTimeToFull > 0) {
        display.print("Piena:");
        if (batTimeToFull >= 60) {
            display.print(batTimeToFull / 60); display.print("h");
            display.print(batTimeToFull % 60); display.print("m");
        } else {
            display.print(batTimeToFull); display.print("min");
        }
    } else if (!batCharging && batTimeToEmpty > 0) {
        display.print("Scarica:");
        if (batTimeToEmpty >= 60) {
            display.print(batTimeToEmpty / 60); display.print("h");
            display.print(batTimeToEmpty % 60); display.print("m");
        } else {
            display.print(batTimeToEmpty); display.print("min");
        }
    } else {
        display.print("Stabile");
    }

    // --- Riga 2 (y=16): Touch — valore FILTRATO (stabile) ---
    display.setCursor(0, 16);
    display.print("T:");
    display.print((int)touchFiltered);   // EMA filtrato: non salta più
    display.print(touchActive ? " [ATTIVO]" : " [off]  ");

    // --- Riga 3 (y=24): Hall — valore FILTRATO + magnete + pulsante ---
    display.setCursor(0, 24);
    display.print("H:");
    display.print((int)hallFiltered);    // EMA filtrato: non salta più
    display.print(magnetDetected ? "[M] " : "    ");
    display.print("Btn:");
    display.print(buttonPressed ? "PRE" : "RIL");

    // --- Riga 4 (y=32): WiFi client + temperatura + avviso batteria bassa ---
    display.setCursor(0, 32);
    display.print("WiFi:");
    display.print(wifiClients);
    display.print("cli ");
    // Temperatura: mostrata solo se NORMAL/ELEVATED (spazio permettendo)
    // Da WARNING in su, l'avviso prende priorità (vedi sotto)
    if (!batLowWarning && thermalState < 2) {  // < WARNING
        display.print((int)espTempFiltered);
        display.print((char)0xF8);  // simbolo grado SSD1306 (0xF8 = °)
        display.print("C");
    } else if (batLowWarning) {
        display.print("[!BAT]");
    } else {
        // WARNING o superiore: priorità all'avviso termico
        display.print("[T:");
        display.print(thermalStateStr());
        display.print("]");
    }

    // --- Riga 5 (y=40): IP Access Point ---
    display.setCursor(0, 40);
    display.print(apIpStr);

    // --- Riga 6 (y=48): LoRa RSSI + qualità (o stato OFF) ---
    display.setCursor(0, 48);
    if (!loraReady) {
        display.print("LoRa:N/D");
    } else if (loraManualDisable) {
        display.print("LoRa:OFF");
    } else if (loraRssi == 0) {
        display.print("LoRa:ascolto");
    } else {
        display.print("LoRa:");
        display.print(loraRssi);
        display.print("dBm ");
        display.print(loraRssiLabel());
    }

    // --- Riga 7 (y=56): GPS stato / avviso batteria bassa ---
    display.setCursor(0, 56);
    if (batLowWarning) {
        display.print("!! BATTERIA BASSA !!");
    } else if (gpsFix) {
        // Fix valido: mostra coordinate compatte
        display.print(gpsLat, 4);
        display.print(" ");
        display.print(gpsLon, 4);
    } else {
        // No fix: mostra stato ricerca
        display.print("GPS:");
        display.print(gpsSats);
        display.print("sat ");
        display.print(gpsEnabled ? "cerca..." : "PSM");
    }

    display.display();
}
