# THeOd Project

**THeOd** (Temperature, Hall, OLED, display) è un firmware ESP32 per la scheda **Heltec WiFi LoRa 32 V2.1** che integra sensori onboard, gestione avanzata della batteria, modulo LoRa SX1276 e una dashboard web accessibile via Wi-Fi.

> **Stato attuale:** v4.0-stable  
> **Prossimo sviluppo:** v5 — integrazione GPS BeITain BN-220

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

### Identificazione board
Questa board è stata identificata come **V2.1** (non V2.0) tramite lo sketch `Heltec_Board_Identifier.ino` incluso nel progetto. Le differenze rilevanti rispetto alla V2:

| Caratteristica | V2 | V2.1 |
|---|---|---|
| Pin ADC batteria | GPIO13 (ADC2) | GPIO37 (ADC1) |
| Conflitto ADC/WiFi | Sì | No |
| Partitore resistivo | R1=100k, R2=470k (÷4.9) | R1=100k, R2≈150k (÷3.042) |

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
| 21 | VEXT control | Output | LOW=ON → alimenta OLED + partitore |
| 25 | LED onboard | Output PWM | LEDC canale 0, 5kHz, 8bit |
| 26 | LoRa DIO0 | Input IRQ | RX/TX done interrupt |
| 27 | LoRa MOSI | SPI | Bus VSPI dedicato LoRa |
| 34 | LoRa DIO2 | Input | Input-only |
| 35 | LoRa DIO1 | Input | Input-only |
| 37 | Batteria ADC | ADC1-CH1 | Partitore resistivo onboard |
| **22** | **GPS RX** | UART2 | **Riservato v5 — BeITain BN-220** |
| **23** | **GPS TX** | UART2 | **Riservato v5 — BeITain BN-220** |
| **32** | **Riserva** | I/O | Disponibile, esposto su header |
| **33** | **Riserva** | I/O | Disponibile, esposto su header |

---

## Struttura file

```
THeOd_project/
├── THeOd_project.ino   # Orchestratore principale: setup(), loop(), EMA sensori
├── config.h            # Unica sorgente di verità: pin, soglie, parametri
├── shared_state.h      # Variabili condivise (extern) tra tutti i moduli
├── battery.h           # ADC1 batteria, ring buffer, stima ETA, deep sleep
├── led_control.h       # Macchina a stati LED con LEDC PWM + override web
├── lora_handler.h      # SX1276: init, RSSI, SNR, stub Meshtastic
├── display.h           # OLED: init, layout 8 righe, toggle software
├── web_routes.h        # Access Point Wi-Fi + endpoint HTTP
└── web_page.h          # Dashboard HTML/CSS/JS (zero dipendenze esterne)
```

---

## Funzionalità implementate

### Sensori onboard
- **Touch capacitivo** (GPIO2): filtro EMA α=0.08, isteresi ±3 unità, risposta ~1.2s
- **Sensore Hall** (interno ESP32): filtro EMA α=0.05, isteresi ±1 unità, risposta ~2s
- **Tasto BOOT** (GPIO0): antirimbalzo software 50ms

Il filtro EMA (Exponential Moving Average) elimina i toggle rapidi che affliggevano le versioni precedenti. La dashboard mostra il valore **filtrato** in grande e il valore raw in piccolo per debug.

### Gestione batteria
- **ADC:** GPIO37 (ADC1-CH1), attenuazione 11dB (Vref≈3.9V), 16 campioni, 2ms delay
- **Partitore calibrato:** moltiplicatore 3.042 (verificato con multimetro su Vbat=4.243V)
- **Storico:** ring buffer 480 campioni × 30s = 4 ore di storia
- **Rilevamento carica:** streak di 2 delta consecutivi positivi/negativi (evita falsi toggle)
- **Fix spike OLED:** `batSkipNextRead` — salta una lettura ADC dopo ogni toggle display
- **Protezione deep sleep:**
  - Vbat ≤ 3.50V: avviso 10 minuti poi deep sleep
  - Vbat ≤ 3.20V: deep sleep immediato
  - Se in carica: deep sleep sospeso
- **Wakeup:** tasto BOOT (ext0) + timer 15s per rilevare USB

### LED onboard (GPIO25)
Pattern automatici gestiti da `getLedPattern()`:

| Pattern | Significato | Animazione web |
|---------|-------------|----------------|
| `off` | Nessun evento | Grigio |
| `dim` | Batteria bassa | Verde scuro fisso |
| `pulse` | In carica | Verde pulsante |
| `solid` | Carica completa | Verde fisso con glow |
| `fade` | Client Wi-Fi connessi | Verde fade |

Override manuale dalla dashboard web: **Auto / Spento / Acceso**.

### Display OLED
Layout 8 righe su SSD1306 128×64:

```
Riga 0: Batteria: tensione, %, stato carica
Riga 1: ETA (Piena in Xh Ym / Scarica in Xh Ym / Stabile)
Riga 2: Touch: valore filtrato + stato
Riga 3: Hall: valore filtrato + magnete + pulsante
Riga 4: WiFi client + avviso batteria bassa
Riga 5: IP Access Point
Riga 6: LoRa RSSI (dBm) + qualità + SNR
Riga 7: LED pattern + OLED stato / "!! BATTERIA BASSA !!"
```

Controllo: `0xAE` (software off, ~0.5mA) / `0xAF` (on). Toggle dalla dashboard.  
**Fix spike:** il toggle OLED imposta `batSkipNextRead=true` per evitare falsi `[CARICA]`.

### Modulo LoRa SX1276
- **Frequenza:** 868.1 MHz (EU868, canale 0 — compatibile Meshtastic LongFast)
- **Parametri:** SF9, BW125kHz, CR4/5, PA_BOOST 17dBm
- **Accesso:** diretto ai registri SX1276 via SPI (no librerie esterne)
- **RSSI:** lettura polling non bloccante ogni 2s
- **Sleep:** `loraSleep()` prima del deep sleep (~0.2µA vs ~10mA attivo)
- **Meshtastic:** stub predisposto (decommentare `MESHTASTIC_ENABLED` in `config.h`)

### Access Point Wi-Fi + Dashboard web

**SSID:** `THeOd-LoRa` | **Password:** `12345678`  
**URL:** `http://192.168.4.1`

Endpoint disponibili:

| Endpoint | Metodo | Descrizione |
|----------|--------|-------------|
| `/` | GET | Dashboard HTML |
| `/data` | GET | JSON polling sensori + batteria + LoRa (ogni 2s) |
| `/battery` | GET | JSON storico % batteria per grafico (ogni 30s) |
| `/lora` | GET | JSON stato LoRa completo (ogni 5s) |
| `/oled?state=on\|off` | GET | Toggle display OLED |
| `/led?state=auto\|off\|on` | GET | Override LED onboard |

La dashboard è completamente **self-hosted** (zero CDN, zero dipendenze esterne), funziona offline via AP. Supporta dark mode automatica (CSS `prefers-color-scheme`). Contrasto WCAG AA verificato.

**Layout v4 (attuale):**
```
┌─────────┬─────────┬──────────────────────┐
│  Touch  │  Hall   │  ◉ LED + Tasto BOOT  │  ← Sensori
│ filtrato│filtrato │  [Auto] [Off] [On]   │
├─────────┴─────────┴──────────────────────┤
│  Batteria: gauge + V + % + ETA           │  ← Batteria
├──────────────────────────────────────────┤
│  Grafico storico 4h                      │  ← Storico
├──────────────────────────────────────────┤
│  LoRa: ▌▌▌ RSSI + qualità + SNR         │  ← LoRa/Mesh
├──────────────────────────────────────────┤
│  OLED toggle │ Client Wi-Fi              │  ← Sistema
└──────────────────────────────────────────┘
```

---

## Calibrazione batteria

Il moltiplicatore ADC è stato calibrato empiricamente su questa specifica unità:

```
Misurazione reale:  multimetro = 4.243V
Lettura firmware:   sketch     = 2.790V  (con mult=2.0)
Moltiplicatore:     3.042  (= 4.243 / ((raw/4095) × 3.9V))
```

Se si cambia la board, eseguire la calibrazione:
1. Caricare la batteria completamente
2. Misurare con multimetro ai capi JST batteria
3. Leggere il valore dal Monitor Seriale (`BAT: X.XXV`)
4. Nuovo moltiplicatore = `Vmultimetro / Vsketch × 3.042`
5. Aggiornare `BATTERY_VOLTAGE_MULTIPLIER` in `config.h`

---

## GPS BeITain BN-220 — Predisposizione v5

### Specifiche modulo
- **Chipset:** u-blox M8030
- **GNSS:** GPS + GLONASS simultanei
- **Interfaccia:** UART 9600 baud (default)
- **Connettore:** JST-SH 1.0mm **4 pin** (VCC, RX, TX, GND)
- **Alimentazione:** 3.3V–5V (regolatore interno)
- **Consumo:** ~25mA acquisizione, ~20mA tracking
- **Fix time:** Cold ~26s, Hot ~1s (con VBACKUP)
- **Standby:** via comando UBX-CFG-RXM (no pin hardware esposto)

### Cablaggio BN-220 → Heltec WiFi LoRa 32 V2.1

| Pin BN-220 | Colore cavo | Funzione | GPIO V2.1 | Note |
|---|---|---|---|---|
| 1 | 🔴 Rosso | VCC | VEXT rail | 3.3V regolati — sicuro con USB, batteria o entrambi |
| 2 | ⚪ Bianco | RX (GPS riceve) | **GPIO23** | UART2 TX — ESP32 invia comandi UBX |
| 3 | 🟢 Verde | TX (GPS trasmette) | **GPIO22** | UART2 RX — ESP32 riceve NMEA |
| 4 | ⚫ Nero | GND | GND | |

**Nota VEXT:** su Heltec V2.1 VEXT è l'uscita del regolatore RT9013 — sempre 3.3V stabili indipendentemente dalla sorgente (USB o batteria). Non è tensione grezza batteria. Sicuro in tutti gli scenari.

**Nota standby:** il BN-220 non espone pin STANDBY. Il risparmio energetico avviene via comando UBX software. Lo spegnimento completo avviene in deep sleep tramite VEXT OFF (già implementato).

**Attenzione USB senza batteria:** con solo USB il partitore ADC batteria legge tensione bassa → rischio deep sleep indesiderato. Gestito in v5 con check al boot.

### GPIO riservati per v5

| GPIO | Funzione v5 |
|------|-------------|
| GPIO22 | GPS UART2 RX (riceve NMEA dal BN-220) |
| GPIO23 | GPS UART2 TX (invia comandi UBX) |
| GPIO32 | Disponibile (riserva) |
| GPIO33 | Disponibile (riserva) |

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

**Impostazioni Arduino IDE:**
- Board: `WiFi LoRa 32(V2)`
- CPU Frequency: `240MHz (WiFi/BT)`
- Upload Speed: `921600`
- Flash Frequency: `80MHz`
- Partition Scheme: `Default`

---

## Changelog

### v4.0-stable *(corrente)*
- Widget LED animato nella dashboard web (CSS keyframes)
- Widget Pulsante BOOT integrato nel widget LED (griglia sensori)
- Override LED dalla web: Auto / Spento / Acceso
- Sezione Sistema semplificata (solo OLED + WiFi)

### v3.0
- Identificazione board V2.1 confermata (`Heltec_Board_Identifier.ino`)
- Migrazione ADC2 GPIO13 → ADC1 GPIO37
- Calibrazione moltiplicatore partitore: 4.9 → 3.042
- Attenuazione ADC 0dB → 11dB (fix saturazione 5.39V)
- Rilevamento carica via delta consecutivi (streak) — elimina falso `[CARICA]`
- Fix spike tensione al toggle OLED (`batSkipNextRead`)
- Fix contrasto dark mode CSS (WCAG AA)
- Modulo LoRa SX1276: RSSI, SNR, pannello OLED e web
- Predisposizione Meshtastic (stub, freq EU868, parametri LongFast)
- Filtro EMA per Touch e Hall con isteresi
- Loop_delay ridotto 100ms → 50ms per EMA più efficace

### v2 (iniziale)
- Struttura multi-file (9 header + 1 .ino)
- Gestione batteria con ring buffer e stima ETA
- Display OLED layout 8 righe
- Access Point Wi-Fi + dashboard web responsive
- Deep sleep con protezione batteria
- LED pattern automatici (dim/fade/pulse/solid)

---

## Note di sviluppo

### Filosofia del progetto
- **Conservativo:** nessuna modifica a ciò che funziona senza motivazione tecnica
- **Incrementale:** un cambiamento alla volta, testato prima di procedere
- **Documentato:** ogni modifica spiegata nei commenti del codice
- **Minimo hardware:** massimo risultato con il minimo di componenti aggiuntivi

### Problemi noti risolti
| Problema | Causa | Soluzione |
|----------|-------|-----------|
| Vbat = 0.54V | ADC2 bloccato da WiFi (board era V2 ma è V2.1) | Migrazione a ADC1 GPIO37 |
| Vbat = 5.39V | ADC saturato (attenuazione 0dB, Vadc=1.85V > 1.1V) | Attenuazione 11dB |
| Vbat = 2.79V | Moltiplicatore errato (schematico ≠ board reale) | Calibrazione empirica → 3.042 |
| `[CARICA]` falso dopo toggle OLED | Spike tensione da variazione carico ~15mA | Flag `batSkipNextRead` |
| `[CARICA]` falso con batteria piena | Soglia assoluta 4.05V → sempre vera per ore | Rilevamento via delta streak |
| Touch/Hall instabili | Nessun filtro, lettura raw diretta | Filtro EMA + isteresi |

### Prossimi sviluppi (v5)
- [ ] Integrazione GPS BeITain BN-220 (UART2 GPIO22/23)
- [ ] Parsing NMEA: lat, lon, altitudine, velocità, satelliti, fix
- [ ] Standby GPS via comando UBX-CFG-RXM
- [ ] Schermata GPS su OLED
- [ ] Sezione GPS nella dashboard web
- [ ] Fix deep sleep con USB senza batteria (ADC a vuoto)
- [ ] Integrazione Meshtastic (decommentare `MESHTASTIC_ENABLED`)

---

## Licenza

Progetto privato — tutti i diritti riservati.
