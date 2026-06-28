#pragma once
#include <Arduino.h>
#include <HardwareSerial.h>
#include "config.h"
#include "shared_state.h"

/*
 * ============================================================================
 * gps_handler.h — Gestione GPS BeITain BN-220 (u-blox M8030)
 *
 * HARDWARE:
 *   Connettore JST-SH 1.0mm 4 pin → GPIO22 (RX), GPIO23 (TX), VEXT, GND
 *   Nessun pin STANDBY hardware esposto → standby via UBX-CFG-RXM
 *
 * PARSING NMEA:
 *   Parser nativo leggero, zero librerie esterne.
 *   Frasi supportate:
 *     $GPRMC — lat, lon, velocità, data, ora, stato fix
 *     $GPGGA — lat, lon, altitudine, satelliti, HDOP
 *   Frasi ignorate: $GPGSV, $GPGSA, $GPVTG, $GNGLL, ecc.
 *
 * STANDBY GPS (PSM — Power Save Mode):
 *   Il BN-220 non ha pin STANDBY. Si usa il comando UBX-CFG-RXM:
 *     0x06 0x11: imposta modalità ricevitore
 *     payload lpMode=1 → Continuous mode (attivo, default)
 *     payload lpMode=4 → Power Save Mode (~5-10mA vs ~20mA)
 *   gpsSetEnabled(false) → invia UBX PSM ON
 *   gpsSetEnabled(true)  → invia UBX PSM OFF (torna a continuous)
 *
 * DEEP SLEEP:
 *   Non serve fare nulla di speciale: VEXT OFF spegne il GPS completamente.
 *   doDeepSleep() in THeOd_project.ino gestisce già VEXT.
 * ============================================================================
 */

HardwareSerial gpsSerial(GPS_UART_NUM);  // UART2

// Buffer NMEA interno
static char  _nmea[GPS_SENTENCE_MAX + 1];
static int   _nmea_pos = 0;

// ---------------------------------------------------------------------------
// Comandi UBX per Power Save Mode
// Struttura pacchetto UBX: 0xB5 0x62 | class | id | len(2) | payload | ck_a ck_b
// UBX-CFG-RXM (class=0x06, id=0x11, len=2): reserved(1) + lpMode(1)
//   lpMode: 0=Continuous, 4=Power Save Mode (PSM)
// ---------------------------------------------------------------------------
static void _sendUBX(const uint8_t* msg, size_t len) {
    gpsSerial.write(msg, len);
    gpsSerial.flush();
}

static void _gpsSetPSM(bool enable) {
    // UBX-CFG-RXM: payload [reserved=0x08, lpMode]
    uint8_t lpMode = enable ? 0x04 : 0x00;  // 4=PSM, 0=Continuous
    uint8_t payload[2] = { 0x08, lpMode };

    // Calcola checksum Fletcher su class+id+len+payload
    uint8_t ck_a = 0, ck_b = 0;
    uint8_t data[] = { 0x06, 0x11, 0x02, 0x00, payload[0], payload[1] };
    for (uint8_t b : data) { ck_a += b; ck_b += ck_a; }

    uint8_t pkt[] = { 0xB5, 0x62,
                      0x06, 0x11,
                      0x02, 0x00,
                      payload[0], payload[1],
                      ck_a, ck_b };
    _sendUBX(pkt, sizeof(pkt));
}

// ---------------------------------------------------------------------------
// _parseFloat() — converte stringa NMEA in float, ritorna 0.0 se vuota
// ---------------------------------------------------------------------------
static float _parseFloat(const char* s) {
    if (!s || !*s) return 0.0f;
    return atof(s);
}

// ---------------------------------------------------------------------------
// _nmeaField() — estrae il campo N (0-based) da una stringa CSV NMEA
// Ritorna puntatore al campo dentro buf (terminato da \0 temporaneo)
// Usa un buffer statico interno — copiare il risultato se necessario
// ---------------------------------------------------------------------------
static char _fieldBuf[32];
static const char* _nmeaField(const char* sentence, int n) {
    int field = 0, i = 0, fi = 0;
    _fieldBuf[0] = '\0';
    while (sentence[i] && sentence[i] != '*') {
        if (sentence[i] == ',') {
            if (field == n) { _fieldBuf[fi] = '\0'; return _fieldBuf; }
            field++; fi = 0;
        } else if (field == n) {
            if (fi < (int)sizeof(_fieldBuf) - 1) _fieldBuf[fi++] = sentence[i];
        }
        i++;
    }
    if (field == n) { _fieldBuf[fi] = '\0'; return _fieldBuf; }
    return "";
}

// ---------------------------------------------------------------------------
// _nmeaDegMin() — converte formato NMEA ddmm.mmmm in gradi decimali
// Es: "4508.1234" N → 45 + 8.1234/60 = 45.13539°
// ---------------------------------------------------------------------------
static float _nmeaDegMin(const char* s, char hemi) {
    if (!s || !*s) return 0.0f;
    float raw = atof(s);
    int deg   = (int)(raw / 100);
    float min = raw - (deg * 100.0f);
    float dec = deg + min / 60.0f;
    if (hemi == 'S' || hemi == 'W') dec = -dec;
    return dec;
}

// ---------------------------------------------------------------------------
// _verifyChecksum() — verifica checksum XOR NMEA ($...*XX)
// ---------------------------------------------------------------------------
static bool _verifyChecksum(const char* sentence) {
    if (sentence[0] != '$') return false;
    const char* star = strchr(sentence, '*');
    if (!star || strlen(star) < 3) return false;
    uint8_t calc = 0;
    for (const char* p = sentence + 1; p < star; p++) calc ^= (uint8_t)*p;
    uint8_t recv = (uint8_t)strtol(star + 1, nullptr, 16);
    return calc == recv;
}

// ---------------------------------------------------------------------------
// _parseGPRMC() — $GPRMC: ora, stato, lat, lon, velocità, data
// Formato: $GPRMC,hhmmss.ss,A,ddmm.mm,N,dddmm.mm,E,spd,cog,ddmmyy,,,*xx
// ---------------------------------------------------------------------------
static void _parseGPRMC(const char* s) {
    // Campo 2: stato (A=valid, V=invalid)
    const char* status = _nmeaField(s, 2);
    if (status[0] != 'A') return;  // Nessun fix valido

    // Ora UTC — campo 1: hhmmss.ss
    const char* timeStr = _nmeaField(s, 1);
    if (strlen(timeStr) >= 6) {
        snprintf(gpsTime, sizeof(gpsTime), "%c%c:%c%c:%c%c",
                 timeStr[0], timeStr[1], timeStr[2],
                 timeStr[3], timeStr[4], timeStr[5]);
    }

    // Latitudine — campo 3+4
    float lat = _nmeaDegMin(_nmeaField(s, 3), _nmeaField(s, 4)[0]);
    if (lat != 0.0f) gpsLat = lat;

    // Longitudine — campo 5+6
    float lon = _nmeaDegMin(_nmeaField(s, 5), _nmeaField(s, 6)[0]);
    if (lon != 0.0f) gpsLon = lon;

    // Velocità — campo 7: nodi → km/h
    float spd = _parseFloat(_nmeaField(s, 7));
    gpsSpeed = spd * 1.852f;

    // Data — campo 9: ddmmyy
    const char* dateStr = _nmeaField(s, 9);
    if (strlen(dateStr) >= 6) {
        snprintf(gpsDate, sizeof(gpsDate), "%c%c/%c%c/20%c%c",
                 dateStr[0], dateStr[1], dateStr[2],
                 dateStr[3], dateStr[4], dateStr[5]);
    }
}

// ---------------------------------------------------------------------------
// _parseGPGGA() — $GPGGA: lat, lon, qualità, satelliti, HDOP, altitudine
// Formato: $GPGGA,hhmmss.ss,ddmm.mm,N,dddmm.mm,E,q,nn,hdop,alt,M,...*xx
// ---------------------------------------------------------------------------
static void _parseGPGGA(const char* s) {
    // Campo 6: qualità fix (0=no fix, 1=GPS, 2=DGPS)
    int quality = atoi(_nmeaField(s, 6));
    if (quality == 0) {
        gpsFix = false;
        return;
    }

    // Latitudine — campo 2+3
    float lat = _nmeaDegMin(_nmeaField(s, 2), _nmeaField(s, 3)[0]);
    if (lat != 0.0f) gpsLat = lat;

    // Longitudine — campo 4+5
    float lon = _nmeaDegMin(_nmeaField(s, 4), _nmeaField(s, 5)[0]);
    if (lon != 0.0f) gpsLon = lon;

    // Satelliti — campo 7
    gpsSats = atoi(_nmeaField(s, 7));

    // HDOP — campo 8
    gpsHdop = _parseFloat(_nmeaField(s, 8));
    if (gpsHdop == 0.0f) gpsHdop = 99.9f;

    // Altitudine — campo 9 (in metri MSL)
    gpsAlt = _parseFloat(_nmeaField(s, 9));

    // Fix valido se abbastanza satelliti e HDOP accettabile
    gpsFix = (gpsSats >= GPS_MIN_SATELLITES && gpsHdop <= GPS_MAX_HDOP);
    if (gpsFix) gpsLastFixMs = millis();
}

// ---------------------------------------------------------------------------
// _processNMEA() — instrada la frase NMEA al parser corretto
// ---------------------------------------------------------------------------
static void _processNMEA(const char* sentence) {
    if (!_verifyChecksum(sentence)) return;

    // Supporta sia prefisso GP (GPS solo) che GN (multi-GNSS: GPS+GLONASS)
    if (strncmp(sentence, "$GPRMC", 6) == 0 ||
        strncmp(sentence, "$GNRMC", 6) == 0) {
        _parseGPRMC(sentence);
    } else if (strncmp(sentence, "$GPGGA", 6) == 0 ||
               strncmp(sentence, "$GNGGA", 6) == 0) {
        _parseGPGGA(sentence);
    }
    // Tutte le altre frasi ($GPGSV, $GPGSA, $GPVTG, ecc.) vengono ignorate
}

// ---------------------------------------------------------------------------
// initGps() — inizializzazione UART2 + reset variabili
// ---------------------------------------------------------------------------
inline void initGps() {
    gpsEnabled   = true;
    gpsFix       = false;
    gpsLat       = 0.0f;
    gpsLon       = 0.0f;
    gpsAlt       = 0.0f;
    gpsSpeed     = 0.0f;
    gpsHdop      = 99.9f;
    gpsSats      = 0;
    gpsTime[0]   = '\0';
    gpsDate[0]   = '\0';
    gpsLastFixMs = 0;
    _nmea_pos    = 0;

    // UART2 su pin custom tramite GPIO matrix dell'ESP32
    gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    delay(100);

    // Svuota buffer iniziale (il GPS invia subito frasi NMEA all'accensione)
    while (gpsSerial.available()) gpsSerial.read();

    Serial.println("GPS: OK — UART2 attivo (9600 baud, GPIO22/23)");
}

// ---------------------------------------------------------------------------
// gpsUpdate() — chiamata ogni GPS_UPDATE_MS (1s) nel loop principale
//
// Legge tutti i byte disponibili su UART2, assembla le frasi NMEA
// carattere per carattere e le invia al parser quando complete ($...\n).
// Non bloccante: processa solo i byte già nel buffer UART.
// ---------------------------------------------------------------------------
inline void gpsUpdate() {
    if (!gpsEnabled) return;

    while (gpsSerial.available()) {
        char c = (char)gpsSerial.read();

        if (c == '$') {
            // Inizio nuova frase: resetta buffer
            _nmea_pos    = 0;
            _nmea[0]     = '$';
            _nmea_pos    = 1;
        } else if (_nmea_pos > 0) {
            if (c == '\n' || c == '\r') {
                // Fine frase: termina stringa e processa
                if (_nmea_pos > 6) {
                    _nmea[_nmea_pos] = '\0';
                    _processNMEA(_nmea);
                }
                _nmea_pos = 0;
            } else if (_nmea_pos < GPS_SENTENCE_MAX) {
                _nmea[_nmea_pos++] = c;
            } else {
                // Overflow: frase troppo lunga, scarta
                _nmea_pos = 0;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// gpsSetEnabled() — attiva o mette in standby il GPS via UBX-CFG-RXM
//
// true  → Continuous mode (piena ricezione, ~20mA)
// false → Power Save Mode (riduzione duty cycle, ~5-10mA)
//
// Nota: il BN-220 non ha pin STANDBY hardware. Il PSM è l'unico modo
// software per ridurre il consumo senza VEXT OFF.
// ---------------------------------------------------------------------------
inline void gpsSetEnabled(bool enable) {
    if (gpsEnabled == enable) return;
    gpsEnabled = enable;
    _gpsSetPSM(!enable);  // PSM ON quando enable=false, PSM OFF quando enable=true
    if (!enable) {
        gpsFix = false;   // Fix non più affidabile in PSM
        Serial.println("GPS: Power Save Mode attivo.");
    } else {
        Serial.println("GPS: Continuous mode attivo.");
    }
}

// ---------------------------------------------------------------------------
// gpsFixAge() — secondi dall'ultimo fix valido (-1 se mai avuto)
// ---------------------------------------------------------------------------
inline int gpsFixAge() {
    if (gpsLastFixMs == 0) return -1;
    return (int)((millis() - gpsLastFixMs) / 1000UL);
}
