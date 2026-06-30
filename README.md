# THeOd Project

**THeOd** (Temperature, Hall, OLED, display) è un firmware ESP32 per la scheda **Heltec WiFi LoRa 32 V2.1** che integra sensori onboard, gestione avanzata della batteria, GPS, modulo LoRa SX1276, gestione termica e una dashboard web accessibile via Wi-Fi.

> **Stato attuale:** v5.0-stable  
> **Prossimo sviluppo:** v6 — azioni automatiche Thermal Manager (riduzione carichi per stato)

---

## Hardware

### Scheda principale
**Heltec WiFi LoRa 32 V2.1**
- ESP32 dual-core Xtensa LX6 240 MHz
- Display OLED 0.96" SSD1306 128×64 integrato
- Modulo LoRa SX1276 integrato (SPI dedicato)
- LED utente onboard (GPIO25)
- Tasto BOOT/USER (GPIO0)
- Batteria Li-Ion 1S via connettore JST onboard
- Regolatore RT9013 → VEXT = 3.3V regolati (sicuro con USB, batteria o entrambi)

### Modulo GPS
**BeITain BN-220** (chipset u-blox M8030, GPS+GLONASS)
- Connettore JST-SH 1.0mm 4 pin (VCC, RX, TX, GND)
- UART 9600 baud su GPIO22/23 (UART2)
- Standby software via comando UBX-CFG-RXM (nessun pin hardware dedicato)

### Identificazione board
Questa board è stata identificata come **V2.1** (non V2.0) tramite lo sketch `Heltec_Board_Identifier.ino`. Differenze rilevanti rispetto alla V2:

| Caratteristica | V2 | V2.1 |
|---|---|---|
| Pin ADC batteria | GPIO13 (ADC2) | GPIO37 (ADC1) |
| Conflitto ADC/WiFi | Sì | No |
| Partitore resistivo | R1=100k, R2=470k (÷4.9) | calibrato empiricamente ÷3.042 |

---

## Mappa pin GPIO

| GPIO | Funzione | Tipo | Note |
|------|----------|------|------|
| 0 | Tasto BOOT/USER | Input pull-up | Antirimbalzo software 50ms |
| 2 | Touch capacitivo | Input analog | Sensore touch nativo ESP32 |
| 4 | OLED SDA | I2C | Fisso su PCB |
| 5 | LoRa SCK | SPI | Bus VSPI dedicato LoRa |
| 14 | LoRa RST | Output | Reset SX1276 |
| 15 | OLED SCL | I2C | Fisso su PCB |
| 16 | OLED RST | Output | Reset hardware SSD1306 |
| 18 | LoRa NSS | SPI CS | Chip select SX1276 |
| 19 | LoRa MISO | SPI | Bus VSPI dedicato LoRa |
| 21 | VEXT control | Output | LOW=ON → alimenta OLED + GPS + partitore |
| 22 | GPS TX | UART2 | ESP32 invia comandi UBX al BN-220 (cavo bianco) |
| 23 | GPS RX | UART2 | ESP32 riceve NMEA dal BN-220 (cavo verde) |
| 25 | LED onboard | Output PWM | LEDC canale 0, 5kHz, 8bit |
| 26 | LoRa DIO0 | Input IRQ | RX/TX done interrupt |
| 27 | LoRa MOSI | SPI | Bus VSPI dedicato LoRa |
| 34 | LoRa DIO2 | Input | Input-only |
| 35 | LoRa DIO1 | Input | Input-only |
| 37 | Batteria ADC | ADC1-CH1 | Partitore resistivo onboard |
| 32 | Riserva | I/O | Disponibile, esposto su header |
| 33 | Riserva | I/O | Disponibile, esposto su header |

---

## Struttura file

```
THeOd_project/
├── THeOd_project.ino   # Orchestratore principale: setup(), loop()
├── config.h            # Unica sorgente di verità: pin, soglie, parametri
├── shared_state.h       # Variabili condivise (extern) tra tutti i moduli
├── battery.h            # ADC1 batteria, ring buffer, stima ETA, deep sleep
├── led_control.h        # Macchina a stati LED con LEDC PWM + override web
├── lora_handler.h        # SX1276: init, RSSI, SNR, stub Meshtastic
├── gps_handler.h         # BN-220: UART2, parser NMEA, UBX PSM standby
├── thermal_manager.h     # Stato termico: 5 livelli, isteresi, trend
├── display.h             # OLED: init, layout 8 righe, toggle software
├── web_routes.h          # Access Point Wi-Fi + endpoint HTTP
└── web_page.h            # Dashboard HTML/CSS/JS (zero dipendenze esterne)
```

File diagnostici standalone (non fanno parte del firmware principale):
```
Heltec_Board_Identifier.ino   # Identifica V2 vs V2.1 (usato una sola volta)
GPS_Diagnostic.ino             # Diagnostica BN-220 con output OLED, no PC
```

---

## Funzionalità implementate

### Sensori onboard
- **Touch capacitivo** (GPIO2): filtro EMA α=0.08, isteresi ±3 unità, risposta ~1.2s
- **Sensore Hall** (interno ESP32): filtro EMA α=0.05, isteresi ±1 unità, risposta ~2s
- **Tasto BOOT** (GPIO0): antirimbalzo software 50ms

### Gestione batteria
- **ADC:** GPIO37 (ADC1-CH1), attenuazione 11dB (Vref≈3.9V), 16 campioni, 2ms delay
- **Partitore calibrato:** moltiplicatore 3.042 (verificato con multimetro su Vbat=4.243V)
- **Storico:** ring buffer 480 campioni × 30s = 4 ore di storia
- **Rilevamento carica:** streak di 2 delta consecutivi positivi/negativi
- **Fix spike OLED:** `batSkipNextRead` — salta una lettura ADC dopo ogni toggle display
- **Protezione deep sleep:** avviso a 3.50V, deep sleep immediato a 3.20V, sospeso se in carica

### LED onboard (GPIO25)
Pattern automatici: `off`/`dim`/`pulse`/`solid`/`fade`. Override manuale dalla dashboard web: Auto / Spento / Acceso.

### Display OLED — layout 8 righe (128×64)
```
Riga 0: Batteria: tensione, %, stato carica
Riga 1: ETA (Piena in Xh Ym / Scarica in Xh Ym / Stabile)
Riga 2: Touch: valore filtrato + stato
Riga 3: Hall: valore filtrato + magnete + pulsante
Riga 4: WiFi client + temperatura (o stato termico/batteria se in allarme)
Riga 5: IP Access Point
Riga 6: LoRa RSSI (dBm) + qualità
Riga 7: GPS coordinate (con fix) / satelliti in ricerca / "!! BATTERIA BASSA !!"
```

### Modulo LoRa SX1276
- **Frequenza:** 868.1 MHz (EU868, compatibile Meshtastic LongFast)
- **Parametri:** SF9, BW125kHz, CR4/5, PA_BOOST 17dBm
- **RSSI:** lettura polling non bloccante ogni 2s
- **Predisposizione Meshtastic:** stub pronto (decommentare `MESHTASTIC_ENABLED`)

### GPS BeITain BN-220
- **Parser NMEA nativo:** zero librerie esterne, supporta `$GPRMC`/`$GNRMC` e `$GPGGA`/`$GNGGA`
- **Verifica checksum XOR** su ogni frase ricevuta
- **Dati estratti:** lat, lon, altitudine, velocità (km/h), satelliti, HDOP, ora UTC, data
- **Standby software:** comando UBX-CFG-RXM, ~5-10mA in Power Save Mode vs ~20mA attivo
- **Confermato funzionante sul campo** (v5.0): fix GPS acquisito, coordinate corrette

### Thermal Manager
Il sistema non legge mai la temperatura raw nei punti di decisione — interroga uno **stato termico** a 5 livelli:

| Stato | Soglia ingresso | Soglia uscita (isteresi -2°C) |
|---|---|---|
| NORMAL | — | — |
| ELEVATED | 60°C | 58°C |
| WARNING | 70°C | 68°C |
| CRITICAL | 80°C | 78°C |
| EMERGENCY | 90°C | 88°C |

- **Sorgente attiva:** ESP32 die (`temperatureRead()`, EMA α=0.15, τ≈3min) — confermata su campo: 47.2-47.4°C, dati stabili e plausibili
- **Sorgente SX1276:** **disabilitata** — sensore di temperatura interno del chip non funzionante su questa unità (vedi Problemi noti risolti). `loraTemp` resta sempre N/D, gli endpoint e i log omettono il campo correttamente
- **Trend:** finestra scorrevole 6 campioni (3 minuti), soglia 1°C → RISING/STABLE/FALLING
- **Statistiche:** min/max sessione
- **v5 scope:** solo notifica (log seriale, OLED, dashboard web). Le azioni automatiche per stato (riduzione refresh, spegnimento OLED, throttling) sono riservate a v6 — l'architettura è già pronta, bastano i case in `_applyThermalActions()`

### Access Point Wi-Fi + Dashboard web

**SSID:** `THeOd-LoRa` | **Password:** `12345678` | **URL:** `http://192.168.4.1`

| Endpoint | Metodo | Descrizione |
|----------|--------|-------------|
| `/` | GET | Dashboard HTML |
| `/data` | GET | JSON sensori + batteria + LoRa + LED (ogni 2s) |
| `/battery` | GET | JSON storico % batteria (ogni 30s) |
| `/lora` | GET | JSON stato LoRa completo (ogni 5s) |
| `/gps` | GET | JSON stato GPS: fix, coordinate, satelliti, HDOP (ogni 3s) |
| `/gpstog?state=on\|off` | GET | Toggle standby GPS (UBX PSM) |
| `/thermal` | GET | JSON stato termico: temp, min/max, trend (ogni 5s) |
| `/oled?state=on\|off` | GET | Toggle display OLED |
| `/led?state=auto\|off\|on` | GET | Override LED onboard |

Layout dashboard (v5.0):
```
┌─────────┬─────────┬──────────────────────┐
│  Touch  │  Hall   │  ◉ LED + Tasto BOOT  │
├─────────┴─────────┴──────────────────────┤
│  Batteria: gauge + V + % + ETA           │
├──────────────────────────────────────────┤
│  Grafico storico 4h                      │
├──────────────────────────────────────────┤
│  LoRa: ▌▌▌ RSSI + qualità                │
├──────────────────────────────────────────┤
│  GPS: coordinate + satelliti + standby    │
├──────────────────────────────────────────┤
│  Temperatura: stato + min/max + trend     │
├──────────────────────────────────────────┤
│  OLED toggle │ Client Wi-Fi               │
└──────────────────────────────────────────┘
```

Dark mode automatica (CSS `prefers-color-scheme`), contrasto WCAG AA verificato, zero dipendenze esterne (CDN, librerie JS).

---

## Calibrazione batteria

```
Misurazione reale:  multimetro = 4.243V
Lettura firmware:   sketch     = 2.790V  (con mult=2.0)
Moltiplicatore:     3.042  (= 4.243 / ((raw/4095) × 3.9V))
```

Procedura per nuova board: caricare completamente, misurare con multimetro ai capi JST batteria, leggere `BAT: X.XXV` dal Monitor Seriale, calcolare `nuovo_mult = Vmultimetro / Vsketch × 3.042`, aggiornare `BATTERY_VOLTAGE_MULTIPLIER` in `config.h`.

---

## GPS BeITain BN-220 — cablaggio confermato

| Pin BN-220 | Colore cavo | Funzione | GPIO V2.1 |
|---|---|---|---|
| 1 | 🔴 Rosso | VCC | VEXT rail |
| 2 | ⚪ Bianco | RX (GPS riceve) | **GPIO22** |
| 3 | 🟢 Verde | TX (GPS trasmette) | **GPIO23** |
| 4 | ⚫ Nero | GND | GND |

**Nota storica:** la prima stesura aveva i pin invertiti (GPIO22/23 scambiati). Identificato e corretto con `GPS_Diagnostic.ino` testando sul campo — vedi Changelog v5.0.

---

## Dipendenze Arduino IDE

| Libreria | Versione testata | Uso |
|----------|-----------------|-----|
| `Adafruit GFX Library` | 1.11.x | Grafica OLED |
| `Adafruit SSD1306` | 2.5.x | Driver display |
| `ESP32 Arduino Core` (Heltec) | 0.0.7 | Board support |

**Board Manager URL Heltec:**
```
https://github.com/Heltec-Aaron-Lee/WiFi_Kit_series/releases/download/0.0.5/package_heltec_esp32_index.json
```

**Impostazioni Arduino IDE:** Board `WiFi LoRa 32(V2)`, CPU `240MHz (WiFi/BT)`, Upload Speed `921600`, Flash Frequency `80MHz`, Partition Scheme `Default`.

---

## Changelog

### v5.0-stable *(corrente)*
- **GPS BeITain BN-220 confermato funzionante sul campo**
  - Fix bug cablaggio: pin TX/RX invertiti (GPIO22↔23)
  - Fix bug critico parser NMEA: buffer statico `_nmeaField()` sovrascritto tra chiamate consecutive → lat/lon sempre 0.0, solo altitudine funzionava
  - `GPS_Diagnostic.ino`: sketch standalone con output OLED completo, nessun PC necessario sul campo
- **Thermal Manager** — nuovo modulo `thermal_manager.h`
  - Macchina a stati a 5 livelli con isteresi bidirezionale
  - Filtro EMA, trend detection, statistiche min/max
  - Sorgente attiva: ESP32 die (confermata su campo, 47°C plausibili)
  - Sorgente SX1276 implementata, testata e infine **disabilitata**: sensore di temperatura del chip non funzionante in silicio su questa unità (3 commit di indagine: trigger SLEEP→STANDBY, timing aumentato, conferma hardware limit — storico completo in git log)
  - Integrato su OLED (riutilizzo riga 4) e dashboard web (nuova card)
  - v5: solo notifica — azioni automatiche riservate a v6
- `JSON_DATA_BUFFER_SIZE` centralizzato in `config.h`

### v4.0-stable
- Widget LED animato nella dashboard web (CSS keyframes)
- Widget Pulsante BOOT integrato nel widget LED
- Override LED dalla web: Auto / Spento / Acceso
- Sezione Sistema semplificata

### v3.0-stable
- Identificazione board V2.1 confermata
- Migrazione ADC2 GPIO13 → ADC1 GPIO37, calibrazione moltiplicatore 3.042
- Fix saturazione ADC (attenuazione 0dB → 11dB)
- Rilevamento carica via delta streak — elimina falso `[CARICA]`
- Fix spike tensione al toggle OLED
- Fix contrasto dark mode CSS (WCAG AA)
- Modulo LoRa SX1276 + predisposizione Meshtastic
- Filtro EMA Touch/Hall con isteresi

### v2 (iniziale)
- Struttura multi-file, gestione batteria, OLED, AP Wi-Fi, deep sleep, LED pattern

---

## Problemi noti risolti

| Problema | Causa | Soluzione |
|----------|-------|-----------|
| Vbat = 0.54V | ADC2 bloccato da WiFi (board era V2 ma è V2.1) | Migrazione a ADC1 GPIO37 |
| Vbat = 5.39V | ADC saturato (attenuazione 0dB, Vadc=1.85V > 1.1V) | Attenuazione 11dB |
| Vbat = 2.79V | Moltiplicatore errato (schematico ≠ board reale) | Calibrazione empirica → 3.042 |
| `[CARICA]` falso dopo toggle OLED | Spike tensione da variazione carico ~15mA | Flag `batSkipNextRead` |
| `[CARICA]` falso con batteria piena | Soglia assoluta 4.05V → sempre vera per ore | Rilevamento via delta streak |
| Touch/Hall instabili | Nessun filtro, lettura raw diretta | Filtro EMA + isteresi |
| GPS: nessun satellite agganciato | Cavi TX/RX invertiti nel cablaggio | Scambio GPIO22↔23 in config.h |
| GPS: altitudine OK ma coordinate sempre 0.0 | Buffer statico `_nmeaField()` sovrascritto tra due chiamate consecutive nella stessa espressione | Copia dei campi in variabili locali separate prima di ogni chiamata successiva |
| LoRa: temperatura SX1276 fissa a 15.0°C | Indagine in due fasi: (1) sensore richiede trigger esplicito SLEEP→STANDBY per datasheet, implementato ma valore restava fisso; (2) timing aumentato a 5ms, valore ancora fisso a `RegTemp=0x00` mentre RSSI variava normalmente nello stesso periodo → comunicazione SPI funzionante, sensore di temperatura del chip non funzionante in silicio (limite hardware noto su alcuni cloni SX1276 economici) | `_readLoraTemp()` disabilitata, ritorna sempre N/D senza accesso SPI. Thermal Manager si basa solo su ESP32 (funzionante, 47°C plausibili) |

---

## Filosofia del progetto

- **Conservativo:** nessuna modifica a ciò che funziona senza motivazione tecnica precisa
- **Incrementale:** un cambiamento alla volta, testato prima di procedere
- **Documentato:** ogni modifica spiegata nei commenti del codice
- **Minimo hardware:** massimo risultato con il minimo di componenti aggiuntivi
- **Stato, non valori raw:** i moduli interrogano stati (es. `thermalIsWarning()`), mai soglie sparse nel codice

### Architetture valutate e posticipate consapevolmente

**Component Registry / Module Registry:** proposta valida per disaccoppiare i moduli dal Thermal Manager (ogni componente dichiara le proprie capacità — rallentabile, spegnibile, ecc. — e riceve notifiche di stato invece di comandi diretti). Valutata e **posticipata**: con 7-8 moduli noti e ben definiti, l'astrazione aggiungerebbe complessità senza beneficio immediato. Da riconsiderare quando arriverà Meshtastic o quando il numero di componenti renderà la gestione diretta difficile da mantenere.

---

## Prossimi sviluppi (v6)

- [ ] Azioni automatiche Thermal Manager: riduzione refresh OLED in WARNING, spegnimento OLED in CRITICAL, LED allarme in EMERGENCY
- [ ] Calibrazione `TEMP_ESP_OFFSET_C` su dati di campo
- [ ] Fix deep sleep con USB senza batteria (ADC partitore a vuoto)
- [ ] Integrazione Meshtastic (decommentare `MESHTASTIC_ENABLED`)
- [ ] Valutare Component Registry se la complessità lo giustifica

---

## Licenza

Progetto privato — tutti i diritti riservati.
