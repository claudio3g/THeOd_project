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

// ---------------------------------------------------------------------------
// BATTERIA — ADC2 su GPIO13 (mappatura nativa Heltec WiFi Kit 32 / LoRa 32)
// ---------------------------------------------------------------------------
#define BATTERY_ADC_USE_ADC2

#ifdef BATTERY_ADC_USE_ADC2
  #define BATTERY_ADC_PIN        13   // GPIO13 — partitore resistivo onboard
  #define BATTERY_ADC2_CH_RAW     4   // ADC2 canale 4
#else
  #define BATTERY_ADC_PIN        37   // fallback ADC1 (solo per board V2.1+)
#endif

// Calcolo tensione dalla lettura ADC
// Vbat reale = (raw / 4095) * Vref * MULTIPLIER
// Il partitore onboard Heltec divide per ~4.9
#define BATTERY_VOLTAGE_MULTIPLIER  4.9f
#define BATTERY_ADC_VREF            1.1f   // Vref con attenuazione 0 dB
#define BATTERY_ADC_SAMPLES         16     // Campioni per media ADC
#define BATTERY_ADC_RETRY_MAX       60     // Retry max per ADC2 (condiviso WiFi)

// ---------------------------------------------------------------------------
// SOGLIE TENSIONE BATTERIA Li-Ion 1S
// ---------------------------------------------------------------------------
#define BATTERY_VOLTAGE_FULL      4.10f   // V → 100% (carica completa)
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
