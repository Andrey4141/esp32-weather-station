# 🌤️ ESP32 Weather Station

[![PlatformIO](https://img.shields.io/badge/PlatformIO-Support-ff69b4)](https://platformio.org)
[![ESP32](https://img.shields.io/badge/MCU-ESP32-blue)](https://www.espressif.com)
[![License](https://img.shields.io/badge/License-MIT-green)](LICENSE)

Multi-sensor weather station on ESP32 with web dashboard, Telegram alerts, lightning detection, air quality monitoring, and rain probability prediction.

---

## 📋 Features

| Category | Sensors / Capabilities |
|----------|----------------------|
| 🌡️ Temperature | DHT22 + BME280 + DS18B20 (3 redundant sensors) |
| 💧 Humidity | DHT22 + BME280 |
| 🔵 Pressure | BME280 → mmHg |
| ☀️ Light | BH1750 (lux) |
| 🌪️ Particulate Matter | Sensirion SPS30 (PM1.0, PM2.5, PM4.0, PM10.0) |
| ⚡ Lightning | AS3935 (distance in km, energy) |
| 🌧️ Rain Probability | Algorithm: pressure trend + humidity + dew point + lightning + light |
| 🌐 Web Dashboard | Real-time data, map, rain bar, serial console |
| 📱 Telegram Bot | Hourly reports + lightning alerts |
| 📡 Narodmon.ru | Publishes all sensor data |
| 🔄 OTA Updates | Wireless firmware upload |

---

## 🧰 Hardware

### Bill of Materials

| Component | Quantity | Purpose | Power | Price (approx.) |
|-----------|----------|---------|-------|----------------|
| ESP32 Dev Board | 1 | Main controller | 5V USB | $5–10 |
| DHT22 | 1 | Temperature & humidity | 3.3V | $3–5 |
| BME280 | 1 | Temp, humidity, pressure | 3.3V | $3–5 |
| BH1750 | 1 | Light intensity (lux) | 3.3V | $2–3 |
| DS18B20 | 1 | External temperature | 3.3V | $2–4 |
| AS3935 (MOD-1016) | 1 | Lightning detector | 3.3V | $15–25 |
| Sensirion SPS30 | 1 | PM sensor | **5V** | $25–35 |
| Breadboard + wires | — | Connections | — | $3–5 |

### Wiring Diagram

```
                    ┌──────────────────────┐
                    │       ESP32          │
                    │                      │
                    │  GPIO21 (SDA) ───────┼──── BH1750 SDA
                    │                      │    BME280 SDA
                    │                      │    AS3935 SDA
                    │                      │    SPS30 SDA ⚡
                    │                      │
                    │  GPIO22 (SCL) ───────┼──── BH1750 SCL
                    │                      │    BME280 SCL
                    │                      │    AS3935 SCL
                    │                      │    SPS30 SCL ⚡
                    │                      │
                    │  GPIO4  ─────────────┼──── DHT22 DATA
                    │  GPIO16 ─────────────┼──── DS18B20 DATA (4.7kΩ → 3.3V)
                    │  GPIO2  ─────────────┼──── AS3935 IRQ (INPUT_PULLUP)
                    │                       │
                    │  3.3V ───────────────┼──── DHT22, BME280, BH1750, AS3935, DS18B20 VCC
                    │  5V   ───────────────┼──── SPS30 VCC ⚠️
                    │  GND  ───────────────┼──── All sensors GND
                    └──────────────────────┘

⚡ SPS30 must be powered by 5V (not 3.3V!)
⚡ SPS30 SEL pin → GND (enables I2C mode)
⚡ DS18B20 needs 4.7kΩ pull-up resistor on DATA line
```

### I2C Addresses

| Sensor | Address |
|--------|---------|
| BH1750 | `0x23` |
| BME280 | `0x76` |
| AS3935 | `0x03` |
| SPS30 | `0x69` |

### Enclosure

- Location: balcony / outdoor under cover
- Power: 5V USB (phone charger or power bank)
- Connectivity: Wi-Fi 2.4 GHz

---

## 📁 Project Structure

```
esp32-weather-station/
├── src/
│   └── main.cpp              # Main firmware (~1100 lines)
├── meteostation.ino           # Same firmware for Arduino IDE
├── platformio.ini             # Build configuration
├── .gitignore
└── README.md
```

---

## 🔧 Configuration

### 1. Set your secrets

Edit `src/main.cpp` or pass via build flags. Search for `YOUR_` and replace:

| Placeholder | Description |
|-------------|-------------|
| `YOUR_WIFI_SSID` | Your Wi-Fi network name |
| `YOUR_WIFI_PASSWORD` | Your Wi-Fi password |
| `YOUR_TELEGRAM_BOT_TOKEN` | Telegram bot token from @BotFather |
| `YOUR_TELEGRAM_CHAT_ID` | Your Telegram chat ID |
| `YOUR_DEVICE_MAC` | ESP32 MAC address (for Narodmon) |
| `YOUR_OTA_PASSWORD` | Password for OTA updates |

### 2. Using build flags (recommended)

Add to `platformio.ini`:

```ini
build_flags =
    -D WIFI_SSID='"MyHomeWiFi"'
    -D WIFI_PASSWORD='"MySecretPass123"'
    -D TELEGRAM_BOT_TOKEN='"1234567890:ABCdefGHIjklMNOpqrsTUVwxyz"'
    -D TELEGRAM_CHAT_ID_VALUE='"123456789"'
```

### 3. Optional: static IP

Edit the `IPAddress` values in `src/main.cpp` if your network requires static IP.

---

## 🚀 Build & Upload

### Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or VS Code extension)
- Python 3.7+
- USB driver for ESP32 (CP2102 / CH340)

### First flash via USB

```powershell
pio run --target upload --upload-port COM5
```

### OTA (wireless updates)

```powershell
pio run --target upload
```

OTA hostname: `esp32-weather-station`, port: `3232`.

### Monitor serial output

```powershell
pio device monitor --port COM5 --baud 115200
```

---

## 🌐 Web Interface

Open `http://<esp32-ip>` in any browser.

```
┌──────────────────────────────────────────┐
│          🌤️ Метеостанция                 │
├──────────────────────┬───────────────────┤
│ ⏰ 14:30:00          │   🗺️ Map         │
│                      │                   │
│ 🌡️ 24.5 °C (DHT22)  │   [OSM Map with  │
│ 💧 55.2% (DHT22)    │    station pin]   │
│ ☀️ 1234 lx           │                   │
│ 🌡️ 24.1 °C (BME280) │                   │
│ 💧 53.1% (BME280)    │                   │
│ 🔵 745.1 mmHg        │                   │
│ 🌡️ 23.8 °C (DS18B20)│                   │
│                      │                   │
│ 🌪️ PM2.5: 12.5 µg/m³│                   │
│ 🌪️ PM10:  15.3 µg/m³│                   │
│                      │                   │
│ 🌧️ [████████░░] 5%   │                   │
│                      │                   │
│ ⚡ Спокойно          │                   │
│                      │                   │
│ [📨 Telegram] [🗑️]  │                   │
├──────────────────────┴───────────────────┤
│ 📋 Console                               │
│ [Wi-Fi connected]                        │
│ [NTP synced: 14:30:00]                   │
│ [All sensors OK]                         │
└──────────────────────────────────────────┘
```

- Auto-refresh: every 3 seconds (data), every 2 seconds (console)
- Rain probability: green (<30%), orange (30–60%), red (>60%)
- Map with OpenStreetMap tiles

---

## 📡 API

### `GET /data`

Returns full sensor readings as JSON:

```json
{
  "tempDHT": 24.5,
  "humidityDHT": 55.2,
  "lux": 1234.5,
  "tempBME": 24.1,
  "humidityBME": 53.1,
  "pressureBME": 745.2,
  "tempDS": 23.8,
  "sps30pm1p0": 5.2,
  "sps30pm2p5": 12.5,
  "sps30pm4p0": 13.8,
  "sps30pm10p0": 15.3,
  "time": "14:30:00",
  "lightningDistance": 0,
  "lightningEnergy": 0,
  "lightningDetected": false,
  "rainProbability": 5,
  "wifiConnected": true,
  "freeHeap": 204036,
  "uptimeSec": 3600,
  "dhtValid": true,
  "bmeValid": true,
  "dsValid": true,
  "sps30Valid": true
}
```

### `GET /send-telegram`

Triggers a manual Telegram message with current sensor data.

### `GET /reset`

Resets lightning detection history.

### `GET /logs`

Returns ring-buffer serial console log as HTML.

---

## 🌧️ Rain Probability Algorithm

Predicts rain likelihood (0–100%) for the next 1–2 hours using multiple factors:

| Factor | Measurement | Max Score |
|--------|-------------|-----------|
| Humidity | BME280 humidity > 60% | 28 pts |
| Dew point proximity | Temperature within 1°C of dew point | 25 pts |
| Pressure trend | Linear regression over 12 samples (~1h) | 35 pts |
| Lightning | Any strike within last 30 min | 30 pts |
| Light level | Lux < 100 (dark/overcast) | 5 pts |

**Total clamped to 0–100%.** Displayed as color-coded bar in web UI.

---

## 📤 Data Publishing

### Telegram
- **Hourly report**: full sensor summary
- **Lightning alert**: instant notification on strike detection
- **Manual trigger**: `GET /send-telegram`

### Narodmon.ru
All sensor data published every 5 minutes to `narodmon.ru`:
- `mac01`–`mac13`: temperature, humidity, pressure, light, PM, lightning

---

## 📦 Dependencies

Managed automatically by PlatformIO:

| Library | Version |
|---------|---------|
| DHT sensor library | latest |
| BH1750 | latest |
| Adafruit BME280 Library | latest |
| DallasTemperature | latest |
| AsyncTCP | 3.3.2 |
| ESPAsyncWebServer | 3.6.0 |
| NTPClient | latest |
| ArduinoJson | 6.21.5 |
| Sensirion I2C SPS30 | latest |

---

## 🛠️ Troubleshooting

| Problem | Solution |
|---------|----------|
| SPS30 not found | Check SEL pin → GND, power → 5V (not 3.3V) |
| OTA fails | Retry 3–5 times; check Wi-Fi signal; increase `--timeout` |
| AS3935 not detected | Verify I2C address (`0x03`); check IRQ pin wiring |
| Time shows `--:--:--` | Wait for NTP sync (requires Wi-Fi) |
| Rain probability always 0 | Check BME280 is working (pressure history needed) |

---

## 📜 License

MIT License — feel free to use, modify, and share.

---

## 🙏 Acknowledgments

- [PlatformIO](https://platformio.org/) for the build system
- [OpenStreetMap](https://www.openstreetmap.org/) for map tiles
- [Narodmon.ru](https://narodmon.ru/) for data visualization
- All sensor library authors (Adafruit, Sensirion, SparkFun, etc.)
