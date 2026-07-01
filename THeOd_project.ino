/*
 * ============================================================================
 * THeOd_project_v3.ino — Orchestratore principale del progetto
 *
 * HARDWARE TARGET: Heltec WiFi LoRa 32 V2
 *   • ESP32 classico (Xtensa LX6 dual-core)
 *   • OLED 0.96" SSD1306 integrato (cablato sul PCB)
 *   • LED utente su GPIO25 (onboard)
 *   • Tasto BOOT/USER su GPIO0 (onboard)
 *   • Batteria Li-Ion 1S tramite connettore JST onboard
 *   • Modulo LoRa SX1276 integrato (SPI bus dedicato)
 *
 * MODIFICHE v3 rispetto a v2:
 *   1. FILTRO EMA per Touch e Hall
 *      - touchFiltered / hallFiltered: media mobile esponenziale
 *      - touchActive / magnetDetected: logica con isteresi (no più flicker)
 *      - Alpha configurabile in config.h (TOUCH_EMA_ALPHA, HALL_EMA_ALPHA)
 *
 *   2. LOOP_DELAY ridotto da 100ms a 50ms
 *      - Il filtro EMA necessita di campionamento più frequente per funzionare bene
 *      - La batteria e l'OLED hanno ancora i propri timer interni (invariati)
 *
 *   3. MODULO LORA SX1276 (lora_handler.h)
 *      - Inizializzazione del modulo LoRa onboard senza librerie esterne
 *      - Lettura RSSI (dBm) e SNR (dB) dell'ultimo pacchetto ricevuto
 *      - Polling non bloccante ogni LORA_RSSI_INTERVAL_MS (2 s)
 *      - loraRssi e loraSnr esportati in shared_state per display e web
 *
 *   4. PREDISPOSIZIONE MESHTASTIC
 *      - meshNodeCount e meshLastRx in shared_state.h
 *      - Stub commentato in lora_handler.h (cerca "STUB MESHTASTIC")
 *      - Config: MESHTASTIC_NODE_NAME, LORA_FREQ_HZ, LORA_SF, LORA_BW
 *      - Per attivare: decommentare MESHTASTIC_ENABLED in config.h
 *
 *   5. PIN CHECK: tutti i pin verificati su schematico Heltec LoRa 32 V2
 *      - Touch: GPIO2 ✓ (capacitivo nativo ESP32, lontano da SCL su GPIO15)
 *      - LoRa SPI: GPIO5/19/27/18/14/26 ✓ (bus VSPI dedicato, non condiviso)
 *      - Battery ADC: GPIO13 ADC2-CH4 ✓ (partitore onboard)
 *      - VEXT: GPIO21 ✓ (LOW=ON per display e partitore)
 *
 * STRUTTURA DEL PROGETTO (9 file):
 *   config.h          → costanti: pin, soglie, parametri EMA e LoRa
 *   shared_state.h    → variabili condivise (extern)
 *   battery.h         → lettura ADC batteria, ring buffer, stima ETA
 *   led_control.h     → macchina a stati LED con LEDC PWM
 *   display.h         → OLED: init, layout aggiornato con RSSI LoRa
 *   lora_handler.h    → SX1276: init, RSSI, SNR, stub Meshtastic
 *   web_page.h        → HTML/CSS/JS dashboard (zero risorse esterne)
 *   web_routes.h      → Access Point + endpoint HTTP (JSON aggiornato)
 *   THeOd_project_v3.ino → questo file
 *
 * ORDINE INCLUDE (rispetta dipendenze):
 *   config → shared_state → battery → led_control → lora_handler
 *   → display → web_routes (include web_page.h)
 * ============================================================================
 */

#include <esp_sleep.h>
#include "config.h"
#include "shared_state.h"
#include "battery.h"
#include "led_control.h"
#include "lora_handler.h"
#include "thermal_manager.h"
#include "gps_handler.h"
#include "display.h"
#include "web_routes.h"

// ============================================================================
// DEFINIZIONI REALI delle variabili dichiarate 'extern' in shared_state.h
// La memoria viene allocata UNA SOLA VOLTA qui.
// ============================================================================

// --- Sensori ---
int   touchValue     = 0;
int   hallValue      = 0;
float touchFiltered  = 0.0f;   // EMA: inizializzato alla prima lettura reale
float hallFiltered   = 0.0f;   // EMA: inizializzato alla prima lettura reale
bool  touchActive    = false;
bool  magnetDetected = false;
bool  buttonPressed  = false;

// --- Batteria ---
float batVoltage     = -1.0f;
float batPercent     = -1.0f;
bool  batCharging    = false;
bool  batLowWarning  = false;
int   batTimeToFull  = -1;
int   batTimeToEmpty = -1;
bool  batEstimating  = true;
bool  batShouldSleep = false;

float batHistory[BATTERY_HISTORY_POINTS] = {};
int   batHistoryHead = 0;
int   batHistorySize = 0;

// --- LoRa / Meshtastic ---
int          loraRssi     = 0;
float        loraSnr      = 0.0f;
bool         loraReady    = false;
bool         loraManualDisable = false;  // Default: LoRa sempre attivo al boot
int          meshNodeCount = 0;
unsigned long meshLastRx  = 0;

// --- Sistema ---
bool  oledEnabled     = OLED_DEFAULT_ON;
bool  batSkipNextRead = false;
int   ledOverride     = 0;     // 0=auto, 1=forza spento, 2=forza acceso
int   wifiClients     = 0;
char  apIpStr[16]     = "";

// --- GPS (BeITain BN-220) ---
bool  gpsEnabled      = false;  // Inizializzato da initGps()
bool  gpsFix          = false;
float gpsLat          = 0.0f;
float gpsLon          = 0.0f;
float gpsAlt          = 0.0f;
float gpsSpeed        = 0.0f;
float gpsHdop         = 99.9f;
int   gpsSats         = 0;
char  gpsTime[10]     = "";
char  gpsDate[11]     = "";
unsigned long gpsLastFixMs = 0;

// --- Thermal Manager ---
int   thermalState    = 0;       // 0=NORMAL (cast a ThermalState in thermal_manager.h)
float espTempRaw       = 0.0f;
float espTempFiltered  = 0.0f;
float espTempMin       = -1.0f;
float espTempMax       = -1.0f;
float loraTemp         = -999.0f;
int   thermalTrend     = 0;

// ============================================================================
// VARIABILI PRIVATE — non condivise con altri moduli
// ============================================================================
static int           _btnState     = HIGH;
static int           _btnLastRead  = HIGH;
static unsigned long _btnDebounce  = 0;

static unsigned long _lastBatMs    = 0;
static unsigned long _lastOledMs   = 0;
static unsigned long _lastLoraMs   = 0;
static unsigned long _lastGpsMs    = 0;
static unsigned long _lastThermMs  = 0;
static unsigned long _lastLogMs    = 0;

// Flag: prima lettura sensori effettuata (per inizializzare EMA correttamente)
static bool          _sensorsInit  = false;

// ============================================================================
// readSensors() — lettura raw + filtro EMA per Touch e Hall
//
// Filtro EMA: filtered = alpha * raw + (1 - alpha) * filtered_prev
//   - Alla prima chiamata inizializza con il valore raw (no transiente)
//   - touchActive e magnetDetected usano isteresi per evitare toggle rapidi:
//       attivazione: filtrato < (THRESHOLD - HYSTERESIS)
//       disattivazione: filtrato > (THRESHOLD + HYSTERESIS)
// ============================================================================
static void readSensors() {
    // --- Touch ---
    touchValue = touchRead(TOUCH_PIN);

    // Inizializza EMA al primo ciclo con il valore attuale (no transiente)
    if (!_sensorsInit) {
        touchFiltered = (float)touchValue;
    } else {
        touchFiltered = TOUCH_EMA_ALPHA * (float)touchValue
                      + (1.0f - TOUCH_EMA_ALPHA) * touchFiltered;
    }

    // Isteresi Touch: evita flicker intorno alla soglia
    if (!touchActive && touchFiltered < (float)(TOUCH_THRESHOLD - TOUCH_HYSTERESIS)) {
        touchActive = true;
    } else if (touchActive && touchFiltered > (float)(TOUCH_THRESHOLD + TOUCH_HYSTERESIS)) {
        touchActive = false;
    }

    // --- Hall ---
    hallValue = hallRead();

    if (!_sensorsInit) {
        hallFiltered = (float)hallValue;
    } else {
        hallFiltered = HALL_EMA_ALPHA * (float)hallValue
                     + (1.0f - HALL_EMA_ALPHA) * hallFiltered;
    }

    // Isteresi Hall
    if (!magnetDetected && hallFiltered > (float)(HALL_THRESHOLD + HALL_HYSTERESIS)) {
        magnetDetected = true;
    } else if (magnetDetected && hallFiltered < (float)(HALL_THRESHOLD - HALL_HYSTERESIS)) {
        magnetDetected = false;
    }

    _sensorsInit = true;
}

// ============================================================================
// readButton() — lettura pulsante BOOT con antirimbalzo software
//
// Campiona GPIO0, accetta la variazione solo se stabile per DEBOUNCE_DELAY ms.
// LOW = premuto (pull-up interno attivo).
// ============================================================================
static void readButton() {
    int reading = digitalRead(BUTTON_PIN);

    if (reading != _btnLastRead) {
        _btnDebounce = millis();  // Rilevato cambio: resetta timer
    }

    if ((millis() - _btnDebounce) > DEBOUNCE_DELAY) {
        if (reading != _btnState) {
            _btnState     = reading;
            buttonPressed = (_btnState == LOW);
        }
    }

    _btnLastRead = reading;
}

// ============================================================================
// doDeepSleep() — sequenza di shutdown ordinata, non ritorna mai.
//
// Ordine di shutdown:
//   1. LED spento
//   2. OLED: messaggio finale 3 s, poi spento (0xAE)
//   3. LoRa: rimesso in Sleep per ridurre consumo (~0.2 µA vs ~10 mA)
//   4. Vext OFF (alimentazione esterna / partitore)
//   5. WiFi disconnesso
//   6. Wakeup: ext0 su GPIO0 (tasto BOOT, LOW) + timer 15 s per USB
//   7. Flush seriale + esp_deep_sleep_start()
// ============================================================================
static void doDeepSleep() {
    Serial.println("\n=== DEEP SLEEP ===");
    Serial.flush();

    // 1. LED spento
    ledcWrite(LED_LEDC_CHANNEL, 0);

    // 2. OLED: messaggio finale
    if (displayOk) {
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);
        display.println("!! BATTERIA BASSA !!");
        display.println("");
        display.println("Collega USB per");
        display.println("ricaricare.");
        display.println("");
        if (batVoltage >= 0.0f) {
            display.print("Tensione: ");
            display.print(batVoltage, 2);
            display.println(" V");
        }
        display.println("");
        display.println("Deep sleep...");
        display.display();
        delay(3000);
        setOledEnabled(false);  // 0xAE: display spento, nessun consumo OLED
    }

    // 3. LoRa in Sleep mode per ridurre consumo da ~10 mA a ~0.2 µA
    // Usa loraSleep() da lora_handler.h (accede al bus SPI privato già inizializzato)
    loraSleep();

    // 4. Vext OFF
    digitalWrite(VEXT_PIN, HIGH);

    // 5. WiFi disconnesso
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(100);

    // 6. Wakeup: tasto BOOT (GPIO0 LOW) OPPURE timer 15 s per rilevare USB
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_PIN, 0);
    esp_sleep_enable_timer_wakeup(15000000ULL);  // 15 secondi

    Serial.println("Deep sleep. Premi BOOT o collega USB per risvegliare.");
    Serial.flush();
    delay(50);

    esp_deep_sleep_start();
    // Non si raggiunge mai questo punto
}

// ============================================================================
// setup() — inizializzazione completa nell'ordine corretto
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(150);

    Serial.println("\n========================================");
    Serial.println("  THeOd Project v3 — Heltec WiFi LoRa 32 V2");
    Serial.println("  Build: " __DATE__ " " __TIME__);
    Serial.println("========================================");

    // --- Pin hardware base ---
    // VEXT: LOW = ON (alimenta display e partitore batteria)
    pinMode(VEXT_PIN, OUTPUT);
    digitalWrite(VEXT_PIN, LOW);

    // BOOT: pull-up interno (senza: GPIO0 flottante → rischio boot in flash mode)
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    // --- LED ---
    initLed();
    Serial.println("LED: OK");

    // --- Display OLED ---
    initDisplay();
    delay(1800);  // Splash screen leggibile

    // --- Batteria: prima lettura + verifica soglia critica ---
    initBattery();

    // Se batteria critica (o risveglio da timer senza USB): ritorna a dormire
    if (batShouldSleep) {
        doDeepSleep();  // Non ritorna
    }

    // --- LoRa SX1276 ---
    if (!initLora()) {
        Serial.println("ATTENZIONE: LoRa non disponibile — continuo senza.");
    }

    // --- Thermal Manager (richiede LoRa per lettura temp SX1276) ---
    initThermal();

    // --- GPS BeITain BN-220 ---
    initGps();

    // --- Access Point + Web Server ---
    startWebServer();
    WiFi.softAPIP().toString().toCharArray(apIpStr, sizeof(apIpStr));

    // --- Wakeup predefinito per eventuali deep sleep futuri ---
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_PIN, 0);

    // --- Timestamp iniziali ---
    _lastBatMs  = millis();
    _lastOledMs = millis();
    _lastLoraMs = millis();
    _lastGpsMs  = millis();
    _lastThermMs = millis();
    _lastLogMs  = millis();

    // --- Schermata di pronto ---
    if (displayOk) {
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);
        display.println("Sistema pronto! v3");
        display.println("");
        display.print("Touch: GPIO "); display.println(TOUCH_PIN);
        display.println("Tasto: BOOT (GPIO0)");
        display.println("Hall: interno");
        display.print("LoRa: "); display.println(loraReady ? "OK" : "N/D");
        display.print("AP: "); display.println(AP_SSID);
        display.print("IP: "); display.println(apIpStr);
        display.display();
        delay(2000);
    }

    Serial.println("Setup completato.");
    Serial.print("Dashboard: http://"); Serial.println(apIpStr);
    Serial.print("LoRa: "); Serial.println(loraReady ? "OK" : "N/D");
    Serial.println("========================================\n");
}

// ============================================================================
// loop() — ciclo principale cooperativo e non bloccante
//
// Priorità azioni:
//   1. Web (massima responsività client)
//   2. Sensori raw + EMA (ogni ciclo, LOOP_DELAY=50ms)
//   3. Debounce pulsante (ogni ciclo)
//   4. Batteria (ogni 30 s, timer interno)
//   5. Deep sleep check (ogni ciclo, subito dopo batteria)
//   6. LoRa RSSI polling (ogni 2 s)
//   7. LED (ogni ciclo per fluidità fade)
//   8. OLED (ogni 500 ms per limitare I2C)
//   9. Log seriale (ogni 5 s)
// ============================================================================
void loop() {
    // 1. Gestione richieste web
    server.handleClient();

    // 2. Client AP connessi
    wifiClients = (int)WiFi.softAPgetStationNum();

    // 3. Lettura sensori con filtro EMA e isteresi
    readSensors();

    // 4. Debounce pulsante BOOT
    readButton();

    // 5. Aggiornamento batteria ogni 30 s
    if (millis() - _lastBatMs >= BATTERY_READ_INTERVAL_MS) {
        _lastBatMs = millis();
        batteryUpdate();
    }

    // 5b. Aggiornamento temperature ogni 30 s
    // Timer indipendente (sfasato) per non sovrapporsi a letture batteria
    if (millis() - _lastThermMs >= TEMP_READ_INTERVAL_MS) {
        _lastThermMs = millis();
        thermalUpdate();
    }

    // 6. Verifica deep sleep (impostato da batteryUpdate)
    if (batShouldSleep) {
        doDeepSleep();  // Non ritorna
    }

    // 7. Polling LoRa RSSI ogni 2 s (non bloccante)
    if (millis() - _lastLoraMs >= LORA_RSSI_INTERVAL_MS) {
        _lastLoraMs = millis();
        loraUpdate();
    }

    // 8. GPS: leggi byte UART ogni GPS_UPDATE_MS (non bloccante)
    if (millis() - _lastGpsMs >= GPS_UPDATE_MS) {
        _lastGpsMs = millis();
        gpsUpdate();
    }

    // 8. Pattern LED (ogni ciclo per fade fluido)
    LedPattern ledPat = getLedPattern();
    ledPatternUpdate(ledPat);

    // 9. Aggiornamento OLED ogni 500 ms
    if (millis() - _lastOledMs >= 500) {
        _lastOledMs = millis();
        updateDisplay(ledPat);
    }

    // 10. Log seriale ogni 5 s
    if (millis() - _lastLogMs >= 5000) {
        _lastLogMs = millis();
        Serial.print("T:");      Serial.print(touchValue);
        Serial.print("(");       Serial.print(touchFiltered, 1);
        Serial.print(")");       Serial.print(touchActive ? "[ON] " : "[off] ");
        Serial.print("H:");      Serial.print(hallValue);
        Serial.print("(");       Serial.print(hallFiltered, 1);
        Serial.print(")");       Serial.print(magnetDetected ? "[M] " : " ");
        Serial.print("Btn:");    Serial.print(buttonPressed ? "PRE" : "RIL");
        Serial.print(" BAT:");   Serial.print(batVoltage, 2);
        Serial.print("V ");      Serial.print((int)batPercent);
        Serial.print("%");       Serial.print(batCharging ? "[CAR]" : "[SCA]");
        Serial.print(" LoRa:");  Serial.print(loraRssi);
        Serial.print("dBm(");    Serial.print(loraRssiLabel());
        Serial.print(") GPS:");  Serial.print(gpsFix ? "FIX" : "no-fix");
        Serial.print(" sat:");   Serial.print(gpsSats);
        if (gpsFix) {
            Serial.print(" lat:"); Serial.print(gpsLat, 5);
            Serial.print(" lon:"); Serial.print(gpsLon, 5);
        }
        Serial.print(" WiFi:"); Serial.print(wifiClients);
        Serial.print("cli LED:"); Serial.println((int)ledPat);
    }

    // 11. Pausa cooperativa (50 ms → EMA più efficace)
    delay(LOOP_DELAY);
}
