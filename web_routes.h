#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include "config.h"
#include "shared_state.h"
#include "led_control.h"
#include "lora_handler.h"
#include "gps_handler.h"
#include "display.h"
#include "web_page.h"

/*
 * ============================================================================
 * web_routes.h — Access Point Wi-Fi + endpoint HTTP
 *
 * ENDPOINT DISPONIBILI:
 *   GET /          → Dashboard HTML (web_page.h)
 *   GET /data      → JSON sensori + batteria + LoRa (polling dashboard)
 *   GET /battery   → JSON storico batteria per grafico
 *   GET /lora      → JSON stato LoRa: RSSI, SNR, qualità, Meshtastic stub
 *   GET /oled?state=on|off → Accende/spegne display OLED
 *
 * MODIFICHE v3:
 *   - /data: aggiunto loraRssi, loraSnr, loraReady, loraLabel
 *   - /data: aggiunti touchFilt e hallFilt (valori EMA per grafici web)
 *   - /lora: nuovo endpoint dedicato con info complete LoRa + mesh stub
 *   - Buffer JSON /data aumentato da 320 a 420 byte per i nuovi campi
 * ============================================================================
 */

WebServer server(80);

static const char* _ledPatStr(LedPattern p) {
    switch (p) {
        case LED_OFF:   return "off";
        case LED_DIM:   return "dim";
        case LED_FADE:  return "fade";
        case LED_SOLID: return "full";
        case LED_PULSE: return "puls";
        default:        return "off";
    }
}

// ---------------------------------------------------------------------------
// GET / — Pagina HTML dashboard
// ---------------------------------------------------------------------------
static void _handleRoot() {
    server.send(200, "text/html; charset=UTF-8", PAGE_HTML);
}

// ---------------------------------------------------------------------------
// GET /data — JSON polling: sensori, batteria, LoRa
//
// Campi aggiunti v3:
//   touchFilt: valore EMA filtrato (float, 1 decimale)
//   hallFilt:  valore EMA filtrato (float, 1 decimale)
//   loraRssi:  RSSI ultimo pacchetto LoRa (dBm, intero)
//   loraSnr:   SNR ultimo pacchetto LoRa (dB, 1 decimale)
//   loraReady: modulo LoRa inizializzato (bool)
//   loraLabel: qualità segnale ("Buono"/"OK"/"Debole"/"N/D")
//   ledOverride: 0=auto, 1=spento, 2=acceso
// ---------------------------------------------------------------------------
static void _handleData() {
    LedPattern lp = getLedPattern();
    int eta       = batCharging ? batTimeToFull : batTimeToEmpty;

    // Buffer 420 byte: spazio sufficiente per tutti i campi v3
    char json[420];
    snprintf(json, sizeof(json),
        "{"
        "\"touch\":%d,\"touchFilt\":%.1f,"
        "\"hall\":%d,\"hallFilt\":%.1f,"
        "\"button\":%s,"
        "\"touchActive\":%s,\"magnet\":%s,"
        "\"batV\":%.2f,\"batPct\":%.1f,"
        "\"batChg\":%s,\"batWarn\":%s,\"batEst\":%s,"
        "\"batEta\":%d,"
        "\"loraRssi\":%d,\"loraSnr\":%.1f,"
        "\"loraReady\":%s,\"loraLabel\":\"%s\","
        "\"oled\":%s,\"clients\":%d,"
        "\"ledPat\":\"%s\",\"ledOverride\":%d,"
        "\"ip\":\"%s\""
        "}",
        touchValue,  touchFiltered,
        hallValue,   hallFiltered,
        buttonPressed  ? "true"  : "false",
        touchActive    ? "true"  : "false",
        magnetDetected ? "true"  : "false",
        (batVoltage >= 0.0f ? batVoltage : -1.0f),
        (batPercent >= 0.0f ? batPercent : -1.0f),
        batCharging    ? "true"  : "false",
        batLowWarning  ? "true"  : "false",
        batEstimating  ? "true"  : "false",
        eta,
        loraRssi,
        loraSnr,
        loraReady      ? "true"  : "false",
        loraRssiLabel(),
        oledEnabled    ? "true"  : "false",
        wifiClients,
        _ledPatStr(lp),
        ledOverride,
        apIpStr
    );
    server.send(200, "application/json", json);
}

// ---------------------------------------------------------------------------
// GET /battery — JSON storico batteria per grafico (invariato da v2)
// ---------------------------------------------------------------------------
static void _handleBattery() {
    float lowPct = (BATTERY_VOLTAGE_LOW  - BATTERY_VOLTAGE_EMPTY) /
                   (BATTERY_VOLTAGE_FULL - BATTERY_VOLTAGE_EMPTY) * 100.0f;
    if (lowPct < 0.0f)   lowPct = 0.0f;
    if (lowPct > 100.0f) lowPct = 100.0f;

    const int BUF_SIZE = 3200;
    char* buf = (char*)malloc(BUF_SIZE);
    if (!buf) {
        server.send(503, "application/json", "{\"error\":\"oom\"}");
        return;
    }

    int pos = snprintf(buf, BUF_SIZE,
        "{\"interval\":%d,\"estimating\":%s,\"lowPct\":%.1f,\"history\":[",
        (int)(BATTERY_READ_INTERVAL_MS / 1000UL),
        batEstimating ? "true" : "false",
        lowPct
    );

    for (int i = 0; i < batHistorySize; i++) {
        int idx = (batHistoryHead - batHistorySize + i + BATTERY_HISTORY_POINTS)
                  % BATTERY_HISTORY_POINTS;
        bool last = (i == batHistorySize - 1);
        if (pos >= BUF_SIZE - 20) break;
        pos += snprintf(buf + pos, BUF_SIZE - pos,
                        last ? "%.1f" : "%.1f,", batHistory[idx]);
    }

    snprintf(buf + pos, BUF_SIZE - pos, "]}");
    server.send(200, "application/json", buf);
    free(buf);
}

// ---------------------------------------------------------------------------
// GET /lora — JSON stato LoRa completo + stub Meshtastic
//
// Risposta esempio:
//   {"ready":true,"rssi":-95,"snr":7.5,"label":"Buono",
//    "freq":868100000,"sf":9,"bw":125,"txPower":17,
//    "meshNode":"THeOd-01","meshNodes":0,"meshLastRx":0}
// ---------------------------------------------------------------------------
static void _handleLora() {
    char json[320];
    snprintf(json, sizeof(json),
        "{"
        "\"ready\":%s,"
        "\"rssi\":%d,"
        "\"snr\":%.1f,"
        "\"label\":\"%s\","
        "\"freq\":%lu,"
        "\"sf\":%d,"
        "\"bw\":%d,"
        "\"txPower\":%d,"
        "\"meshNode\":\"%s\","
        "\"meshNodes\":%d,"
        "\"meshLastRx\":%lu"
        "}",
        loraReady  ? "true" : "false",
        loraRssi,
        loraSnr,
        loraRssiLabel(),
        (unsigned long)LORA_FREQ_HZ,
        LORA_SF,
        LORA_BW_KHZ,
        LORA_TX_POWER,
        MESHTASTIC_NODE_NAME,
        meshNodeCount,
        meshLastRx
    );
    server.send(200, "application/json", json);
}

// ---------------------------------------------------------------------------
// GET /oled?state=on|off — Controllo display OLED
// ---------------------------------------------------------------------------
static void _handleOled() {
    if (!server.hasArg("state")) {
        server.send(400, "text/plain", "Parametro 'state' mancante");
        return;
    }
    String s = server.arg("state");
    if      (s == "on")  { setOledEnabled(true);  server.send(200, "text/plain", "OK"); }
    else if (s == "off") { setOledEnabled(false); server.send(200, "text/plain", "OK"); }
    else { server.send(400, "text/plain", "Valore non valido (usa on|off)"); }
}

// ---------------------------------------------------------------------------
// GET /led?state=auto|off|on — Override manuale LED onboard
//   auto → torna alla logica automatica (pattern per stato batteria/WiFi)
//   off  → forza LED spento
//   on   → forza LED acceso (solid)
// ---------------------------------------------------------------------------
static void _handleLed() {
    if (!server.hasArg("state")) {
        server.send(400, "text/plain", "Parametro 'state' mancante");
        return;
    }
    String s = server.arg("state");
    if      (s == "auto") { ledOverride = 0; server.send(200, "text/plain", "OK"); }
    else if (s == "off")  { ledOverride = 1; server.send(200, "text/plain", "OK"); }
    else if (s == "on")   { ledOverride = 2; server.send(200, "text/plain", "OK"); }
    else { server.send(400, "text/plain", "Valore non valido (usa auto|off|on)"); }
}

// ---------------------------------------------------------------------------
// GET /gps — JSON stato GPS completo
// ---------------------------------------------------------------------------
static void _handleGps() {
    char json[256];
    snprintf(json, sizeof(json),
        "{"
        "\"enabled\":%s,"
        "\"fix\":%s,"
        "\"lat\":%.6f,"
        "\"lon\":%.6f,"
        "\"alt\":%.1f,"
        "\"speed\":%.1f,"
        "\"sats\":%d,"
        "\"hdop\":%.1f,"
        "\"time\":\"%s\","
        "\"date\":\"%s\","
        "\"age\":%d"
        "}",
        gpsEnabled    ? "true" : "false",
        gpsFix        ? "true" : "false",
        gpsLat, gpsLon, gpsAlt, gpsSpeed,
        gpsSats, gpsHdop,
        gpsTime, gpsDate,
        gpsFixAge()
    );
    server.send(200, "application/json", json);
}

// ---------------------------------------------------------------------------
// GET /gps?state=on|off — Attiva o mette in standby il GPS (UBX PSM)
// ---------------------------------------------------------------------------
static void _handleGpsToggle() {
    if (!server.hasArg("state")) {
        server.send(400, "text/plain", "Parametro 'state' mancante");
        return;
    }
    String s = server.arg("state");
    if      (s == "on")  { gpsSetEnabled(true);  server.send(200, "text/plain", "OK"); }
    else if (s == "off") { gpsSetEnabled(false); server.send(200, "text/plain", "OK"); }
    else { server.send(400, "text/plain", "Valore non valido (usa on|off)"); }
}

static void _handleNotFound() {
    server.send(404, "text/plain", "404 - Non trovata");
}

// ---------------------------------------------------------------------------
// startWebServer() — configura AP Wi-Fi e avvia server HTTP
// ---------------------------------------------------------------------------
inline void startWebServer() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    WiFi.setTxPower((wifi_power_t)WIFI_TX_POWER_RAW);
    btStop();  // Bluetooth off: risparmio ~10 mA

    server.on("/",        _handleRoot);
    server.on("/data",    _handleData);
    server.on("/battery", _handleBattery);
    server.on("/lora",    _handleLora);
    server.on("/oled",    _handleOled);
    server.on("/led",     _handleLed);
    server.on("/gps",     _handleGps);        // JSON stato GPS
    server.on("/gpstog",  _handleGpsToggle);  // on|off GPS PSM
    server.onNotFound(_handleNotFound);
    server.begin();

    Serial.println("WebServer: OK");
    Serial.print("AP SSID: "); Serial.println(AP_SSID);
}
