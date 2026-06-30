/*
 * ============================================================================
 * GPS_Diagnostic.ino — Diagnostica BeITain BN-220 su Heltec WiFi LoRa 32 V2.1
 *
 * STANDALONE — nessun PC necessario, tutto su OLED + LED onboard.
 *
 * OLED mostra:
 *   Riga 0: Baud rate trovato / stato ricerca
 *   Riga 1: Frasi NMEA ricevute (raw count)
 *   Riga 2: Satelliti agganciati
 *   Riga 3: HDOP (qualità segnale)
 *   Riga 4: Latitudine
 *   Riga 5: Longitudine
 *   Riga 6: Altitudine + velocità
 *   Riga 7: Ora UTC + stato fix
 *
 * LED onboard (GPIO25):
 *   Lampeggio lento (1s)  = ricerca baud / no NMEA
 *   Lampeggio medio (0.5s) = NMEA ok, no fix
 *   Lampeggio veloce (0.1s) = FIX ACQUISITO
 *   Acceso fisso           = fix stabile (>10 fix consecutivi)
 *
 * CABLAGGIO CORRETTO (verificato):
 *   BN-220 Verde  (TX GPS) → GPIO23  (ESP32 UART2 RX)
 *   BN-220 Bianco (RX GPS) → GPIO22  (ESP32 UART2 TX)
 *   BN-220 Rosso  (VCC)    → VEXT / 3.3V
 *   BN-220 Nero   (GND)    → GND
 * ============================================================================
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <HardwareSerial.h>

// --- Pin ---
#define GPS_RX    23   // GPIO23: ESP32 riceve (verde = TX BN-220)
#define GPS_TX    22   // GPIO22: ESP32 invia   (bianco = RX BN-220)
#define VEXT_PIN  21
#define LED_PIN   25
#define OLED_SDA   4
#define OLED_SCL  15
#define OLED_RST  16

HardwareSerial      gpsSerial(2);
Adafruit_SSD1306    disp(128, 64, &Wire, OLED_RST);

// --- Stato GPS ---
long   foundBaud    = 0;
int    nmeaCount    = 0;   // Frasi NMEA totali ricevute
int    fixCount     = 0;   // Fix GGA validi consecutivi
int    sats         = 0;
float  hdop         = 99.9f;
float  lat          = 0.0f;
float  lon          = 0.0f;
float  alt          = 0.0f;
float  speed_kmh    = 0.0f;
char   utcTime[10]  = "--:--:--";
bool   hasFix       = false;

// --- Blink LED non bloccante ---
unsigned long _ledToggleMs = 0;
unsigned long _ledInterval = 1000;
bool          _ledState    = false;

void ledTick() {
    if (millis() - _ledToggleMs >= _ledInterval) {
        _ledToggleMs = millis();
        _ledState = !_ledState;
        digitalWrite(LED_PIN, _ledState ? HIGH : LOW);
    }
}

// --- OLED helpers ---
void oledClear() {
    disp.clearDisplay();
    disp.setTextSize(1);
    disp.setTextColor(SSD1306_WHITE);
}

void oledRow(int row, const char* label, const char* value) {
    disp.setCursor(0, row * 8);
    disp.print(label);
    disp.print(value);
}

void oledRowF(int row, const char* label, float value, int dec) {
    disp.setCursor(0, row * 8);
    disp.print(label);
    disp.print(value, dec);
}

// --- NMEA helpers ---
const char* nmeaField(const char* s, int n) {
    static char buf[20];
    int f = 0, i = 0, fi = 0;
    buf[0] = '\0';
    while (s[i] && s[i] != '*') {
        if (s[i] == ',') {
            if (f == n) { buf[fi] = '\0'; return buf; }
            f++; fi = 0;
        } else if (f == n) {
            if (fi < 19) buf[fi++] = s[i];
        }
        i++;
    }
    if (f == n) { buf[fi] = '\0'; return buf; }
    return "";
}

float degMin(const char* s, char h) {
    if (!s || !*s) return 0.0f;
    float raw = atof(s);
    int   deg = (int)(raw / 100);
    float min = raw - (deg * 100.0f);
    float dec = deg + min / 60.0f;
    if (h == 'S' || h == 'W') dec = -dec;
    return dec;
}

bool verifyChecksum(const char* s) {
    if (s[0] != '$') return false;
    const char* star = strchr(s, '*');
    if (!star || strlen(star) < 3) return false;
    uint8_t calc = 0;
    for (const char* p = s + 1; p < star; p++) calc ^= (uint8_t)*p;
    return calc == (uint8_t)strtol(star + 1, nullptr, 16);
}

void parseGGA(const char* s) {
    int q = atoi(nmeaField(s, 6));
    if (q == 0) { hasFix = false; fixCount = 0; return; }

    // Copia separata per evitare sovrascrittura buffer statico
    char latS[16], latH[4], lonS[16], lonH[4];
    strncpy(latS, nmeaField(s, 2), 15); latS[15] = '\0';
    strncpy(latH, nmeaField(s, 3), 3);  latH[3]  = '\0';
    strncpy(lonS, nmeaField(s, 4), 15); lonS[15] = '\0';
    strncpy(lonH, nmeaField(s, 5), 3);  lonH[3]  = '\0';

    float la = degMin(latS, latH[0]);
    float lo = degMin(lonS, lonH[0]);
    if (la != 0.0f) lat = la;
    if (lo != 0.0f) lon = lo;

    sats = atoi(nmeaField(s, 7));
    hdop = atof(nmeaField(s, 8));
    alt  = atof(nmeaField(s, 9));

    hasFix = (sats >= 4 && hdop <= 5.0f);
    if (hasFix) fixCount++;
}

void parseRMC(const char* s) {
    if (nmeaField(s, 2)[0] != 'A') return;
    const char* t = nmeaField(s, 1);
    if (strlen(t) >= 6)
        snprintf(utcTime, sizeof(utcTime), "%c%c:%c%c:%c%c",
                 t[0],t[1],t[2],t[3],t[4],t[5]);

    char latS[16],latH[4],lonS[16],lonH[4];
    strncpy(latS, nmeaField(s,3),15); latS[15]='\0';
    strncpy(latH, nmeaField(s,4),3);  latH[3]='\0';
    strncpy(lonS, nmeaField(s,5),15); lonS[15]='\0';
    strncpy(lonH, nmeaField(s,6),3);  lonH[3]='\0';
    float la = degMin(latS,latH[0]);
    float lo = degMin(lonS,lonH[0]);
    if (la != 0.0f) lat = la;
    if (lo != 0.0f) lon = lo;
    speed_kmh = atof(nmeaField(s,7)) * 1.852f;
}

void processNMEA(const char* s) {
    if (!verifyChecksum(s)) return;
    nmeaCount++;
    if      (strncmp(s,"$GNGGA",6)==0||strncmp(s,"$GPGGA",6)==0) parseGGA(s);
    else if (strncmp(s,"$GNRMC",6)==0||strncmp(s,"$GPRMC",6)==0) parseRMC(s);
}

// --- Fase 1: auto-detect baud ---
long detectBaud() {
    const long bauds[] = { 9600, 38400, 57600, 115200 };
    long best = 0; int bestCnt = 0;

    for (int b = 0; b < 4; b++) {
        gpsSerial.end(); delay(100);
        gpsSerial.begin(bauds[b], SERIAL_8N1, GPS_RX, GPS_TX);
        delay(200);
        while (gpsSerial.available()) gpsSerial.read();

        // Aggiorna OLED durante la ricerca
        oledClear();
        char msg[32]; snprintf(msg, sizeof(msg), "Test %ld baud...", bauds[b]);
        oledRow(0, "", msg);
        oledRow(1, "Porta il GPS", "");
        oledRow(2, "vicino a una", "");
        oledRow(3, "finestra.", "");
        disp.display();

        int    cnt = 0;
        char   buf[128]; int pos = 0;
        unsigned long t = millis();
        while (millis() - t < 5000) {
            while (gpsSerial.available()) {
                char c = gpsSerial.read();
                if (c == '$') { pos = 0; buf[0]='$'; pos=1; }
                else if (pos > 0) {
                    if (c=='\n'||c=='\r') {
                        if (pos>5) { buf[pos]='\0'; cnt++; }
                        pos=0;
                    } else if (pos<127) buf[pos++]=c;
                }
            }
            ledTick(); delay(10);
        }
        Serial.print("Baud "); Serial.print(bauds[b]);
        Serial.print(": "); Serial.print(cnt); Serial.println(" frasi");
        if (cnt > bestCnt) { bestCnt = cnt; best = bauds[b]; }
    }
    return (bestCnt > 0) ? best : 0;
}

// ============================================================================
void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    pinMode(VEXT_PIN, OUTPUT);
    digitalWrite(VEXT_PIN, LOW);  // VEXT ON
    delay(500);

    Wire.begin(OLED_SDA, OLED_SCL);
    pinMode(OLED_RST, OUTPUT);
    digitalWrite(OLED_RST, HIGH); delay(5);
    digitalWrite(OLED_RST, LOW);  delay(5);
    digitalWrite(OLED_RST, HIGH); delay(5);
    disp.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    disp.clearDisplay();
    disp.setTextSize(1); disp.setTextColor(SSD1306_WHITE);
    disp.setCursor(0,0); disp.println("GPS Diagnostic");
    disp.println("Ricerca baud...");
    disp.display();

    // Fase 1: trova baud rate
    foundBaud = detectBaud();

    if (foundBaud == 0) {
        oledClear();
        oledRow(0, "ERRORE:", "");
        oledRow(1, "Nessuna frase", "");
        oledRow(2, "NMEA ricevuta", "");
        oledRow(4, "Controlla:", "");
        oledRow(5, "TX/RX cavi", "");
        oledRow(6, "VCC = 3.3V?", "");
        disp.display();
        // LED lampeggio SOS
        while (true) {
            for (int i=0;i<3;i++){digitalWrite(LED_PIN,HIGH);delay(200);digitalWrite(LED_PIN,LOW);delay(200);}
            delay(600);
        }
    }

    // Configura UART con baud trovato
    gpsSerial.end(); delay(100);
    gpsSerial.begin(foundBaud, SERIAL_8N1, GPS_RX, GPS_TX);
    delay(200);
    while (gpsSerial.available()) gpsSerial.read();

    Serial.print("Baud trovato: "); Serial.println(foundBaud);
    _ledInterval = 500;  // NMEA ok: lampeggio medio
}

// ============================================================================
char   _nmea[128]; int _npos = 0;
unsigned long _lastOledMs = 0;

void loop() {
    // Leggi UART GPS
    while (gpsSerial.available()) {
        char c = gpsSerial.read();
        if (c == '$') { _npos=0; _nmea[0]='$'; _npos=1; }
        else if (_npos > 0) {
            if (c=='\n'||c=='\r') {
                if (_npos>5) { _nmea[_npos]='\0'; processNMEA(_nmea); }
                _npos=0;
            } else if (_npos<127) _nmea[_npos++]=c;
        }
    }

    // Aggiorna velocità LED
    if      (fixCount >= 10) { digitalWrite(LED_PIN, HIGH); }  // Fix stabile: fisso
    else if (hasFix)         { _ledInterval = 100; ledTick(); } // Fix: veloce
    else if (nmeaCount > 0)  { _ledInterval = 500; ledTick(); } // NMEA: medio
    else                     { _ledInterval = 1000; ledTick(); } // Nulla: lento

    // Aggiorna OLED ogni 500ms
    if (millis() - _lastOledMs >= 500) {
        _lastOledMs = millis();
        oledClear();

        // Riga 0: baud + count frasi
        char r0[22]; snprintf(r0,sizeof(r0),"%ld bd %d fr", foundBaud, nmeaCount);
        oledRow(0, "", r0);

        // Riga 1: satelliti + HDOP
        char r1[22]; snprintf(r1,sizeof(r1),"Sat:%d HDOP:%.1f", sats, hdop);
        oledRow(1, "", r1);

        // Riga 2: latitudine
        if (lat != 0.0f) {
            char r2[22]; snprintf(r2,sizeof(r2),"Lat:%.5f", lat);
            oledRow(2, "", r2);
        } else {
            oledRow(2, "Lat: --", "");
        }

        // Riga 3: longitudine
        if (lon != 0.0f) {
            char r3[22]; snprintf(r3,sizeof(r3),"Lon:%.5f", lon);
            oledRow(3, "", r3);
        } else {
            oledRow(3, "Lon: --", "");
        }

        // Riga 4: altitudine
        char r4[22]; snprintf(r4,sizeof(r4),"Alt:%.0fm Spd:%.1f", alt, speed_kmh);
        oledRow(4, "", r4);

        // Riga 5: ora UTC
        char r5[22]; snprintf(r5,sizeof(r5),"UTC: %s", utcTime);
        oledRow(5, "", r5);

        // Riga 6: stato fix
        if (fixCount >= 10) {
            oledRow(6, "*** FIX STABILE ***", "");
        } else if (hasFix) {
            char r6[22]; snprintf(r6,sizeof(r6),"FIX ok (%d)", fixCount);
            oledRow(6, "", r6);
        } else if (nmeaCount > 0) {
            oledRow(6, "NMEA ok, no fix", "");
        } else {
            oledRow(6, "Nessun dato GPS", "");
        }

        // Riga 7: istruzione situazionale
        if (fixCount >= 10) {
            oledRow(7, "GPS OK! Torna al", "");
        } else if (hasFix) {
            oledRow(7, "Aspetta fix stab.", "");
        } else if (nmeaCount > 0) {
            oledRow(7, "All'aperto/finest", "");
        } else {
            oledRow(7, "Check cavi TX/RX", "");
        }

        disp.display();
    }

    delay(10);
}
