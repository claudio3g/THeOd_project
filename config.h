#pragma once
/*
 * ============================================================================
 * config.h — Unica sorgente di verità per pin, soglie e parametri.
 *
 * HARDWARE TARGET: Heltec WiFi LoRa 32 V2
 *   • ESP32 classico (Xtensa LX6 dual-core)
 *   • OLED 0.96" SSD1306 128×64 integrato (cablato sul PCB)
 *   • LED utente su GPIO25 (onboard)
 *   • Tasto BOOT/USER su GPIO0 (onboard)
 *   • Batteria Li-Ion 1S tramite connettore JST onboard
 *   • Modulo LoRa SX1276 integrato (SPI)
 *
 * MODIFICHE v3:
 *   - Aggiunto filtro EMA (Exponential Moving Average) per Touch e Hall
 *   - Aggiunto modulo LoRa RSSI (segnale ricevuto in dBm)
 *   - Predisposto stub per integrazione Meshtastic futura
 *   - Soglie aggiornate per sensori stabilizzati
 * ============================================================================
 */

// ---------------------------------------------------------------------------
// VERSIONE FIRMWARE — unico punto di verità, usato da .ino, display e web
// Aggiornare solo qui — il resto del codice referenzia questa costante.
// ---------------------------------------------------------------------------
#define FIRMWARE_VERSION "5.1"

// ---------------------------------------------------------------------------
// DISPLAY OLED — pin fissi, cablati internamente sul PCB Heltec LoRa 32 V2
// ---------------------------------------------------------------------------
#define OLED_SDA_PIN    4
#define OLED_SCL_PIN   15
#define OLED_RST_PIN   16    // Reset hardware SSD1306
#define OLED_I2C_ADDR  0x3C
#define OLED_WIDTH     128
#define OLED_HEIGHT     64
#define OLED_DEFAULT_ON true

// ---------------------------------------------------------------------------
// SENSORI ONBOARD
// ---------------------------------------------------------------------------
#define TOUCH_PIN    2    // GPIO2: touch capacitivo (isolato da SCL su GPIO15)
#define BUTTON_PIN   0    // GPIO0: tasto BOOT/USER (pull-up interno)

// ---------------------------------------------------------------------------
// LED ONBOARD — GPIO25
// ---------------------------------------------------------------------------
#define LED_BUILTIN_PIN      25

// LEDC — periferica PWM hardware dell'ESP32
#define LED_LEDC_CHANNEL      0
#define LED_LEDC_FREQ      5000   // Hz
#define LED_LEDC_RESOLUTION   8   // bit → duty 0–255

// Luminosità e tempistiche LED
#define LED_DIM_BRIGHTNESS        5   // ~2% luminosità (batteria bassa)
#define LED_FADE_DURATION_MS   2000   // ms per ciclo fade completo
#define LED_CHARGING_ON_MS      100   // ms impulso ON durante ricarica
#define LED_CHARGING_OFF_MAX   3000   // ms pausa OFF a 0% carica
#define LED_CHARGING_OFF_MIN    300   // ms pausa OFF a 99% carica

// ---------------------------------------------------------------------------
// FILTRI SENSORI — EMA (Exponential Moving Average)
//
// Formula: filtered = alpha * raw + (1-alpha) * filtered_prev
// Alpha vicino a 1.0 = risposta veloce (poco filtro)
// Alpha vicino a 0.0 = risposta lenta (molto filtro, molto stabile)
//
// Touch: alpha=0.08 → costante di tempo ~12 campioni @ 100ms = ~1.2 s
// Hall:  alpha=0.05 → costante di tempo ~20 campioni @ 100ms = ~2.0 s
// ---------------------------------------------------------------------------
#define TOUCH_EMA_ALPHA   0.08f   // Filtro aggressivo: touch ha molto rumore
#define HALL_EMA_ALPHA    0.05f   // Filtro molto aggressivo: Hall è instabile

// Soglia isteresi per evitare toggle rapidi vicino alla soglia
// Il sensore deve superare THRESHOLD - HYST per attivarsi
// e scendere sotto THRESHOLD + HYST per disattivarsi
#define TOUCH_HYSTERESIS   3      // ± unità raw intorno alla soglia touch
#define HALL_HYSTERESIS    1      // ± unità raw intorno alla soglia hall

// Soglie originali (ora applicate al segnale filtrato)
#define TOUCH_THRESHOLD   20      // touchRead filtrato < 20 → dito rilevato
#define HALL_THRESHOLD     2      // hallRead filtrato  >  2 → magnete vicino

// ---------------------------------------------------------------------------
// ANTIRIMBALZO E LOOP
// ---------------------------------------------------------------------------
#define DEBOUNCE_DELAY    50      // ms antirimbalzo tasto BOOT
#define LOOP_DELAY        50      // ms pausa loop (ridotto da 100→50 per EMA)

// ---------------------------------------------------------------------------
// LORA SX1276 — pin SPI cablati su Heltec WiFi LoRa 32 V2
//
// SPI bus dedicato (non condiviso con altri dispositivi):
//   SCK  → GPIO5
//   MISO → GPIO19
//   MOSI → GPIO27
//   NSS  → GPIO18  (Chip Select)
//   RST  → GPIO14
//   DIO0 → GPIO26  (interrupt RX/TX done)
//   DIO1 → GPIO35
//   DIO2 → GPIO34
// ---------------------------------------------------------------------------
#define LORA_SCK_PIN    5
#define LORA_MISO_PIN  19
#define LORA_MOSI_PIN  27
#define LORA_NSS_PIN   18
#define LORA_RST_PIN   14
#define LORA_DIO0_PIN  26
#define LORA_DIO1_PIN  35
#define LORA_DIO2_PIN  34

// Frequenza LoRa (EU868 standard Meshtastic)
#define LORA_FREQ_HZ   868100000UL  // 868.1 MHz — canale 0 EU868

// Banda e SF predefiniti (compatibili Meshtastic LongFast)
#define LORA_BW_KHZ    125    // Bandwidth 125 kHz
#define LORA_SF          9    // Spreading Factor 9
#define LORA_CR          5    // Coding Rate 4/5
#define LORA_TX_POWER   17    // dBm (max legale EU = 14 dBm ERP, hardware max 20)

// Soglia qualità RSSI per display
#define LORA_RSSI_GOOD   -90  // dBm: >= -90 = segnale buono
#define LORA_RSSI_FAIR  -110  // dBm: >= -110 = segnale accettabile
                               //      < -110 = segnale debole / assente

// Intervallo aggiornamento RSSI in ms (non bloccante)
#define LORA_RSSI_INTERVAL_MS  2000UL

// ---------------------------------------------------------------------------
// MESHTASTIC — stub predisposto per integrazione futura
//
// Quando si integrerà la libreria Meshtastic (o il firmware nativo):
//   1. Abilitare #define MESHTASTIC_ENABLED
//   2. Collegare i pin DIO1/DIO2 se richiesto dal firmware
//   3. Il modulo mesh_state.h gestirà nodeId, channelName, snr, rxCount
// ---------------------------------------------------------------------------
// #define MESHTASTIC_ENABLED     // Decommentare quando pronto
#define MESHTASTIC_NODE_NAME  "THeOd-01"  // Nome nodo nella rete mesh

// ---------------------------------------------------------------------------
// ACCESS POINT WI-FI
// ---------------------------------------------------------------------------
#define AP_SSID       "THeOd-LoRa"       // Aggiornato a riflettere hw LoRa
#define AP_PASSWORD   "12345678"
#define WIFI_TX_POWER_RAW  44             // WIFI_POWER_11dBm (potenza ridotta)

// Buffer JSON endpoint /data — dimensione unica dichiarata qui,
// usata sia per l'allocazione in web_routes.h sia per snprintf().
// Aumentare se si aggiungono campi al JSON di /data.
constexpr size_t JSON_DATA_BUFFER_SIZE = 512;

// ---------------------------------------------------------------------------
// BATTERIA — HELTEC WIFI LORA 32 V2.1 CONFERMATA
//
// Identificazione eseguita con Heltec_Board_Identifier.ino:
//   GPIO13 (ADC2): non risponde mai → non collegato su questa board
//   GPIO37 (ADC1): legge stabile con WiFi OFF e ON → partitore qui collegato
//
// Partitore resistivo V2.1: R1=100k (da Vbat), R2=100k (a GND)
//   → divisore = 2.0  (diverso da V2 che aveva R2=470k → divisore ≈ 4.9)
//
// ADC1 (GPIO37) usa analogRead() standard, attenuazione 11dB:
//   Fondo scala effettivo ≈ 3.9V
//   Con Vbat=3.7V → Vadc=1.85V → raw≈1942 (nel range, no saturazione)
//   Con 0dB (Vref=1.1V) l'ADC saturava (Vadc=1.85V > 1.1V) → leggeva sempre 5.39V
//
// ADC1 non ha NESSUN conflitto con il driver WiFi → no retry complessi
// ---------------------------------------------------------------------------
#define BATTERY_ADC_PIN             37    // GPIO37 — ADC1-CH1, V2.1 onboard
#define BATTERY_VOLTAGE_MULTIPLIER   3.042f // Calibrato su misura reale: multimetro=4.243V, sketch=2.79V
                                             // Partitore reale stimato R1=100k, R2≈150k (÷3.0, non ÷2.0 da schematico)
#define BATTERY_ADC_VREF             3.9f // Vref con attenuazione 11dB (ADC_ATTEN_DB_11)
#define BATTERY_ADC_SAMPLES          16   // Campioni per media (ADC1 stabile, 16 sufficienti)
#define BATTERY_ADC_SAMPLE_DELAY_MS   2   // ms tra campioni (ADC1 non ha conflitti WiFi)

// Raw valido per Li-Ion 1S (3.0V–4.2V) con 11dB e divisore 2.0:
//   raw_min = (3.0V / 2.0 / 3.9V) * 4095 ≈ 1575
//   raw_max = (4.2V / 2.0 / 3.9V) * 4095 ≈ 2205
// Soglia conservativa a 1400 per non scartare batterie molto scariche
#define BATTERY_ADC_MIN_VALID_RAW    950  // Sotto = lettura spuria. raw per Vbat=3.0V è ~1035, margine a 950

// Soglia deviazione standard per rilevare partitore floating (batteria assente).
// Una batteria reale dà letture stabili (~0.03V di rumore tipico tra campioni).
// Un partitore floating (USB senza batteria) dà letture instabili (~0.29V tipico).
// 0.10V è una soglia conservativa: ben sopra il rumore normale, ben sotto
// il rumore tipico da floating — vedi battery.h: _readBatteryVoltage()
#define BATTERY_ADC_MAX_STDDEV_V     0.10f

// ---------------------------------------------------------------------------
// SOGLIE TENSIONE BATTERIA Li-Ion 1S
// ---------------------------------------------------------------------------
#define BATTERY_VOLTAGE_FULL      4.10f   // V → 100% (carica completa)
                                           //     (il TP4056/MCP73831 porta la cella a 4.2V costante)
#define BATTERY_VOLTAGE_LOW       3.50f   // V → avvio preavviso scarica
#define BATTERY_VOLTAGE_CRITICAL  3.20f   // V → deep sleep immediato
#define BATTERY_VOLTAGE_EMPTY     3.00f   // V → riferimento 0% scala

// ---------------------------------------------------------------------------
// TEMPISTICHE GESTIONE BATTERIA
// ---------------------------------------------------------------------------
#define BATTERY_READ_INTERVAL_MS   30000UL  // Lettura ogni 30 s
#define BATTERY_HISTORY_POINTS       480    // 480 × 30 s = 4 ore di storico
#define BATTERY_LOW_WARNING_MS    600000UL  // 10 min avviso prima sleep
#define BATTERY_TREND_SAMPLES         10    // Campioni per stima dV/dt
#define BATTERY_CHARGING_THRESHOLD  0.003f  // V: delta minimo per batCharging

// Pin Vext: LOW = alimentazione esterna ON (display + partitore)
#define VEXT_PIN  21

// ---------------------------------------------------------------------------
// GPS — BeITain BN-220 (u-blox M8030)
//
// Connettore: JST-SH 1.0mm 4 pin
// Cablaggio:
//   Rosso  (VCC) → VEXT rail (3.3V regolati, sicuro con USB o batteria)
//   Bianco (RX)  → GPIO23   (UART2 TX: ESP32 invia comandi UBX al GPS)
//   Verde  (TX)  → GPIO22   (UART2 RX: ESP32 riceve NMEA dal GPS)
//   Nero   (GND) → GND
//
// Nessun pin STANDBY hardware esposto sul BN-220.
// Standby via comando UBX-CFG-RXM (Power Save Mode) — vedi gps_handler.h
// Spegnimento completo solo in deep sleep tramite VEXT OFF (già implementato)
// ---------------------------------------------------------------------------
#define GPS_RX_PIN       23    // GPIO23 — UART2 RX (cavo verde = TX BN-220, invertito rispetto alla prima stesura)
#define GPS_TX_PIN       22    // GPIO22 — UART2 TX (cavo bianco = RX BN-220)
#define GPS_BAUD       9600    // Baud rate default u-blox M8030
#define GPS_UART_NUM      2    // HardwareSerial UART2

// Timeout e intervalli
#define GPS_FIX_TIMEOUT_MS    120000UL  // 2 min: se nessun fix → segnala no-fix
#define GPS_UPDATE_MS           1000UL  // Leggi UART GPS ogni 1 s nel loop
#define GPS_SENTENCE_MAX         100    // Lunghezza max stringa NMEA da parsare

// Soglie qualità fix
#define GPS_MIN_SATELLITES  4    // Minimo satelliti per fix considerato valido
#define GPS_MAX_HDOP      5.0f   // HDOP massimo accettabile (< 2 = ottimo, < 5 = ok)

// ---------------------------------------------------------------------------
// THERMAL MANAGER — gestione termica sistema
//
// Sorgenti temperatura (zero hardware aggiuntivo):
//   ESP32:  temperatureRead() — die temperature, offset tipico +20-30°C vs ambiente
//   SX1276: registro 0x3C    — die temperature chip LoRa, ±2°C
//
// NOTA: tutte le temperature misurano il SILICIO (die), non l'aria.
// Valori normali con WiFi + LoRa attivi: ESP32 50-65°C, SX1276 35-50°C.
//
// STATI TERMICI (5 livelli con isteresi bidirezionale):
//   NORMAL    < TEMP_ELEVATED_C         — funzionamento normale
//   ELEVATED  TEMP_ELEVATED_C .. WARN   — monitoraggio, nessuna azione
//   WARNING   TEMP_WARNING_C  .. CRIT   — riduzione refresh display
//   CRITICAL  TEMP_CRITICAL_C .. EMERG  — spegnimento OLED, riduzione log
//   EMERGENCY > TEMP_EMERGENCY_C        — protezione massima
//
// Isteresi: si entra a T_soglia, si esce solo a T_soglia - TEMP_HYSTERESIS_C
// Questo evita oscillazioni di stato con temperature vicine alla soglia.
// ---------------------------------------------------------------------------
#define TEMP_READ_INTERVAL_MS   30000UL  // Lettura ogni 30s

// Soglie di ingresso stato termico (°C, temperatura die ESP32)
#define TEMP_ELEVATED_C         60.0f
#define TEMP_WARNING_C          70.0f
#define TEMP_CRITICAL_C         80.0f
#define TEMP_EMERGENCY_C        90.0f

// Isteresi uscita stato: si torna allo stato inferiore solo a (soglia - HYST)
#define TEMP_HYSTERESIS_C        2.0f

// Filtro EMA temperatura ESP32
// alpha=0.15 → costante di tempo ~6 letture @ 30s = ~3 minuti
// Più lento di Touch/Hall: la temperatura cambia lentamente
#define TEMP_EMA_ALPHA          0.15f

// Offset correzione ESP32 (calibrabile per unità specifica)
// Impostare ~-20.0f per avvicinarsi alla temperatura ambiente reale
#define TEMP_ESP_OFFSET_C        0.0f

// Statistiche trend: campioni usati per calcolare RISING/STABLE/FALLING
// trend = media(ultimi N/2 campioni) - media(precedenti N/2 campioni)
#define TEMP_TREND_SAMPLES       6      // finestra totale (deve essere pari)
#define TEMP_TREND_THRESHOLD     1.0f   // °C: delta minimo per dichiarare trend

// Soglie SX1276 (più accurate, ±2°C)
#define TEMP_LORA_WARN_C        75.0f   // °C: avviso LoRa
#define TEMP_LORA_CRIT_C        83.0f   // °C: critico (max spec = 85°C)

// Intervalli refresh OLED per stato termico
// In NORMAL/ELEVATED: refresh normale 500ms
// In WARNING: rallentato a 1000ms (riduce carico CPU/I2C)
// In CRITICAL/EMERGENCY: OLED spento (setOledEnabled gestisce direttamente)
#define OLED_REFRESH_NORMAL_MS    500UL
#define OLED_REFRESH_WARNING_MS  1000UL
