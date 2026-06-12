#include <WiFi.h>
#include <Wire.h>
#include <DHT.h>
#include <BH1750.h>
#include <Adafruit_BME280.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#define ARDUINOJSON_USE_DOUBLE 1
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SparkFun_AS3935.h>
#include <SensirionI2cSps30.h>
#include <ArduinoOTA.h>
#include <math.h>
#include <stdarg.h>

#define LOG_BUF_SIZE 4096
char logBuf[LOG_BUF_SIZE];
int logBufPos = 0;

static void logAdd(const char* s) {
  while (*s) {
    logBuf[logBufPos] = *s++;
    logBufPos = (logBufPos + 1) % LOG_BUF_SIZE;
  }
}
static void logAdd(const String& s) { logAdd(s.c_str()); }

#define LOG(msg) do { Serial.print(msg); logAdd(msg); } while(0)
#define LOGLN(msg) do { Serial.println(msg); logAdd(msg); logAdd("\n"); } while(0)
#define LOGF(fmt, ...) do { char _lf[256]; snprintf(_lf, sizeof(_lf), fmt, ##__VA_ARGS__); Serial.print(_lf); logAdd(_lf); } while(0)

#define DHTPIN 4
#define DHTTYPE DHT22
#define ONE_WIRE_BUS 16
#define AS3935_PIN 2
#define AS3935_ADDRESS 0x03

#ifndef WIFI_SSID
#define WIFI_SSID "YOUR_WIFI_SSID"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#endif

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

IPAddress ip(192, 168, 1, 100);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns1(77, 88, 8, 8);
IPAddress dns2(1, 1, 1, 1);

#ifndef TELEGRAM_BOT_TOKEN
#define TELEGRAM_BOT_TOKEN "YOUR_TELEGRAM_BOT_TOKEN"
#endif
#ifndef TELEGRAM_CHAT_ID_VALUE
#define TELEGRAM_CHAT_ID_VALUE "YOUR_TELEGRAM_CHAT_ID"
#endif

const char* TELEGRAM_TOKEN = TELEGRAM_BOT_TOKEN;
const char* TELEGRAM_CHAT_ID = TELEGRAM_CHAT_ID_VALUE;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "ru.pool.ntp.org", 25200);
unsigned long lastNtpLog = 0;

const float LATITUDE = 55.7558f;
const float LONGITUDE = 37.6173f;
const char* DEVICE_MAC = "YOUR_DEVICE_MAC";

const unsigned long DHT_INTERVAL = 30000UL;
const unsigned long LIGHT_INTERVAL = 30000UL;
const unsigned long BME_INTERVAL = 5000UL;
const unsigned long DS_INTERVAL = 30000UL;
const unsigned long SPS30_INTERVAL = 3000UL;
const unsigned long TELEGRAM_INTERVAL = 3600000UL;
const unsigned long TELEGRAM_RETRY_INTERVAL = 60000UL;
const unsigned long NARODMON_INTERVAL = 300000UL;
const unsigned long LIGHTNING_TIMEOUT = 30000UL;
const unsigned long LIGHTNING_SETTLE_MS = 150UL;
const unsigned long LIGHTNING_DEBOUNCE_MS = 1500UL;
const unsigned long WIFI_RECONNECT_INTERVAL = 15000UL;
const unsigned long WIFI_RESTART_THRESHOLD = 1800000UL;
const unsigned long COMPONENT_RETRY_INTERVAL = 60000UL;
const unsigned long HTTP_TIMEOUT_MS = 4000UL;
const int MAX_TELEGRAM_FAILURES = 10;

DHT dht(DHTPIN, DHTTYPE);
BH1750 lightMeter;
Adafruit_BME280 bme;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
SparkFun_AS3935 lightning(AS3935_ADDRESS);
SensirionI2cSps30 sps30;
AsyncWebServer server(80);

float temperatureDHT = 0.0f;
float humidityDHT = 0.0f;
float lux = 0.0f;
float pressureBME = 0.0f;
float temperatureBME = 0.0f;
float humidityBME = 0.0f;
float temperatureDS = 0.0f;
float sps30pm1p0 = 0.0f;
float sps30pm2p5 = 0.0f;
float sps30pm4p0 = 0.0f;
float sps30pm10p0 = 0.0f;
String formattedTime = "00:00:00";
unsigned long lightningDistance = 0;
unsigned long lightningEnergy = 0;
bool lightningDetected = false;
bool dhtValid = false;
bool lightValid = false;
bool bmeValid = false;
bool dsValid = false;
bool sps30Valid = false;
bool lightSensorAvailable = false;
bool bmeAvailable = false;
bool sps30Available = false;
bool lightningSensorAvailable = false;
volatile bool lightningPending = false;
volatile unsigned long lastLightningIrqAt = 0;
volatile bool manualTelegramPending = false;
volatile bool lightningResetPending = false;
unsigned long lastLightningTime = 0;
unsigned long lastSuccessfulTelegramSend = 0;
unsigned long lastWiFiReconnect = 0;
unsigned long wifiDisconnectedSince = 0;
unsigned long lastComponentRetry = 0;
unsigned long uptimeSec = 0;
unsigned long freeHeap = 0;
int consecutiveTelegramFailures = 0;
String lastTelegramError = "none";

unsigned long lastDhtRead = 0;
unsigned long lastLightRead = 0;
unsigned long lastBmeRead = 0;
unsigned long lastDsRead = 0;
unsigned long lastSps30Read = 0;
unsigned long lastTelegramSend = 0;
unsigned long lastPeriodicTelegramSuccess = 0;
unsigned long lastNarodmonSend = 0;
unsigned long lastLightningTimestamp = 0;

portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;

void IRAM_ATTR lightningIRQ();
void startWiFiConnection();
bool ensureWiFiConnected();
void updateClock();
void updateAllSensors(bool force = false);
void retryUnavailableComponents();
void handleLightningEvent();
bool sendTelegramMessage(bool fromLightning = false);
bool isLightningActive();
void sendToNarodmon();
String buildTelegramMessage(bool fromLightning);
void readSps30Sensor(bool force = false);

#define PRESSURE_HISTORY_SIZE 60
float pressureHistory[PRESSURE_HISTORY_SIZE];
int pressureHistoryIndex = 0;
int pressureHistoryCount = 0;
unsigned long lastPressureTrendCalc = 0;
float rainProbability = 0.0f;
unsigned long lastLightning30min = 0;

float calcDewPoint(float t, float h);
float calcRainProbability();

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Метеостанция</title>
  <link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css" />
  <style>
    body {
      font-family: Arial, sans-serif;
      background: #f0f0f0;
      margin: 0;
      padding: 20px;
    }
    .container {
      display: flex;
      flex-wrap: wrap;
      gap: 20px;
    }
    .data-panel {
      flex: 1 1 320px;
    }
    .map-panel {
      flex: 2 1 420px;
      min-height: 500px;
    }
    .card {
      background: #ffffff;
      border-radius: 10px;
      padding: 15px;
      margin-bottom: 15px;
      box-shadow: 0 2px 5px rgba(0, 0, 0, 0.1);
    }
    h1 {
      text-align: center;
      color: #333333;
      margin-bottom: 20px;
    }
    .sensor-value {
      font-size: 24px;
      font-weight: bold;
      color: #007bff;
    }
    .lightning-value {
      color: #ff4444;
    }
    #map {
      height: 100%;
      min-height: 420px;
      width: 100%;
      border-radius: 10px;
    }
    .update-info {
      color: #666666;
      font-size: 14px;
      text-align: center;
    }
    .telegram-section {
      background: #0088cc;
      color: #ffffff;
      border-radius: 10px;
      padding: 15px;
    }
    .telegram-btn {
      background: #ffffff;
      color: #0088cc;
      border: none;
      border-radius: 6px;
      padding: 10px 16px;
      font-weight: bold;
      cursor: pointer;
      margin-top: 10px;
    }
    .danger-btn {
      background: #ff6b6b;
      color: #ffffff;
    }
    .pm-warning {
      color: #e67e22;
    }
    .pm-danger {
      color: #e74c3c;
    }
    .pm-good {
      color: #27ae60;
    }
    .console-panel {
      margin-top: 20px;
    }
    .console-box {
      background: #1e1e1e;
      color: #d4d4d4;
      font-family: 'Cascadia Code', 'Consolas', 'Courier New', monospace;
      font-size: 12px;
      padding: 10px;
      border-radius: 6px;
      max-height: 300px;
      overflow-y: auto;
      white-space: pre-wrap;
      word-break: break-all;
    }
    .console-box::-webkit-scrollbar {
      width: 8px;
    }
    .console-box::-webkit-scrollbar-thumb {
      background: #444;
      border-radius: 4px;
    }
    .rain-bar {
      height: 20px;
      border-radius: 10px;
      background: #eee;
      overflow: hidden;
      margin: 8px 0;
    }
    .rain-fill {
      height: 100%;
      border-radius: 10px;
      transition: width 1s, background 1s;
    }
    .rain-label {
      font-size: 14px;
    }
  </style>
  <script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
  <script>
    let map;
    let marker;

    function initMap() {
      map = L.map('map').setView([55.7558, 37.6173], 10);
      L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
        attribution: '&copy; OpenStreetMap'
      }).addTo(map);
      marker = L.marker([55.7558, 37.6173]).addTo(map);
      marker.bindPopup('<b>Метеостанция</b><br>Точка установки').openPopup();
    }

    function pmClass25(val) {
      if (val <= 15) return 'pm-good';
      if (val <= 35) return 'pm-warning';
      return 'pm-danger';
    }
    function pmClass10(val) {
      if (val <= 45) return 'pm-good';
      if (val <= 60) return 'pm-warning';
      return 'pm-danger';
    }

    function fmt(val, dec) {
      if (val == null || isNaN(val)) return '—';
      return val.toFixed(dec);
    }

    function updateData() {
      fetch('/data')
        .then(response => response.json())
        .then(data => {
          document.getElementById('temp-dht').innerText = fmt(data.tempDHT, 1) + ' °C';
          document.getElementById('humidity-dht').innerText = fmt(data.humidityDHT, 1) + ' %';
          document.getElementById('lux').innerText = fmt(data.lux, 0) + ' lx';
          document.getElementById('temp-bme').innerText = fmt(data.tempBME, 1) + ' °C';
          document.getElementById('humidity-bme').innerText = fmt(data.humidityBME, 1) + ' %';
          document.getElementById('pressure').innerText = fmt(data.pressureBME, 1) + ' мм рт. ст.';
          document.getElementById('temp-ds').innerText = fmt(data.tempDS, 1) + ' °C';
          document.getElementById('time').innerText = data.time;

          const pm25el = document.getElementById('pm2p5');
          pm25el.innerText = fmt(data.sps30pm2p5, 1) + ' µg/m³';
          pm25el.className = 'sensor-value ' + pmClass25(data.sps30pm2p5 || 0);

          const pm10el = document.getElementById('pm10p0');
          pm10el.innerText = fmt(data.sps30pm10p0, 1) + ' µg/m³';
          pm10el.className = 'sensor-value ' + pmClass10(data.sps30pm10p0 || 0);

          document.getElementById('pm1p0').innerText = fmt(data.sps30pm1p0, 1) + ' µg/m³';
          document.getElementById('pm4p0').innerText = fmt(data.sps30pm4p0, 1) + ' µg/m³';

          document.getElementById('lightning-distance').innerText =
            data.lightningDistance > 0 ? data.lightningDistance + ' км' : 'Нет данных';
          document.getElementById('lightning-energy').innerText =
            data.lightningEnergy > 0 ? data.lightningEnergy : 'Нет данных';

          const lightningStatus = document.getElementById('lightning-status');
          lightningStatus.innerText = data.lightningDetected ? 'Обнаружена' : 'Спокойно';
          lightningStatus.className = data.lightningDetected
            ? 'sensor-value lightning-value'
            : 'sensor-value';

          const rainVal = data.rainProbability || 0;
          document.getElementById('rain-prob').innerText = rainVal + '%';
          const fill = document.getElementById('rain-fill');
          fill.style.width = rainVal + '%';
          if (rainVal < 30) fill.style.background = '#27ae60';
          else if (rainVal < 60) fill.style.background = '#e67e22';
          else fill.style.background = '#e74c3c';

        })
        .catch(() => {
        });
    }

    function sendTelegramManual() {
      fetch('/send-telegram?manual=true')
        .then(response => response.text())
        .then(text => alert(text));
    }

    function resetData() {
      if (!confirm('Сбросить данные о молниях?')) return;
      fetch('/reset')
        .then(response => response.text())
        .then(text => {
          alert(text);
          updateData();
        });
    }

    function updateLogs() {
      fetch('/logs')
        .then(r => r.text())
        .then(t => {
          const el = document.getElementById('console-box');
          if (!el) return;
          el.innerHTML = t;
          el.scrollTop = el.scrollHeight;
        })
        .catch(() => {});
    }

    document.addEventListener('DOMContentLoaded', () => {
      initMap();
      updateData();
      setInterval(updateData, 3000);
      updateLogs();
      setInterval(updateLogs, 2000);
    });
  </script>
</head>
<body>
  <h1>Метеостанция</h1>
  <div class="container">
    <div class="data-panel">
      <div class="card">
        <p>Текущее время: <span id="time">--:--:--</span></p>
      </div>
      <div class="card"><h2>Температура (DHT22)</h2><p class="sensor-value" id="temp-dht">--</p></div>
      <div class="card"><h2>Влажность (DHT22)</h2><p class="sensor-value" id="humidity-dht">--</p></div>
      <div class="card"><h2>Освещенность</h2><p class="sensor-value" id="lux">--</p></div>
      <div class="card"><h2>Температура (BME280)</h2><p class="sensor-value" id="temp-bme">--</p></div>
      <div class="card"><h2>Влажность (BME280)</h2><p class="sensor-value" id="humidity-bme">--</p></div>
      <div class="card"><h2>Давление</h2><p class="sensor-value" id="pressure">--</p></div>
      <div class="card"><h2>Температура (DS18B20)</h2><p class="sensor-value" id="temp-ds">--</p></div>
      <div class="card"><h2>PM1.0</h2><p class="sensor-value" id="pm1p0">--</p></div>
      <div class="card"><h2>PM2.5</h2><p class="sensor-value" id="pm2p5">--</p></div>
      <div class="card"><h2>PM4.0</h2><p class="sensor-value" id="pm4p0">--</p></div>
      <div class="card"><h2>PM10.0</h2><p class="sensor-value" id="pm10p0">--</p></div>
      <div class="card">
        <h2>Вероятность дождя (1-2ч)</h2>
        <div class="rain-bar"><div class="rain-fill" id="rain-fill" style="width:0%"></div></div>
        <p class="rain-label" id="rain-prob">--%</p>
      </div>
      <div class="card">
        <h2>Молнии</h2>
        <p>Расстояние: <span class="sensor-value" id="lightning-distance">--</span></p>
        <p>Энергия: <span class="sensor-value" id="lightning-energy">--</span></p>
        <p>Статус: <span class="sensor-value" id="lightning-status">--</span></p>
      </div>
      <div class="telegram-section">
        <h2>Telegram</h2>
        <p>Отправка данных вручную или при обнаружении молнии.</p>
        <button class="telegram-btn" onclick="sendTelegramManual()">Отправить сейчас</button>
        <hr style="margin:15px 0;border:none;border-top:1px solid rgba(255,255,255,0.35);">
        <p>Сбросить накопленные данные о молниях.</p>
        <button class="telegram-btn danger-btn" onclick="resetData()">Сбросить молнии</button>
      </div>
    </div>
    <div class="map-panel">
      <div class="card" style="height:100%;">
        <h2>Карта</h2>
        <div id="map"></div>
        <p class="update-info">Координаты: 55.7558, 37.6173</p>
      </div>
    </div>
  </div>
  <div class="console-panel" style="max-width:900px;margin:20px auto;">
    <div class="card">
      <h2>Консоль</h2>
      <div class="console-box" id="console-box">Загрузка...</div>
    </div>
  </div>
</body>
</html>
)rawliteral";

void startWiFiConnection() {
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.config(ip, gateway, subnet, dns1, dns2);
  WiFi.begin(ssid, password);
  lastWiFiReconnect = millis();
  if (wifiDisconnectedSince == 0) {
    wifiDisconnectedSince = millis();
  }
  LOGLN("Wi-Fi: start connect");
}

bool ensureWiFiConnected() {
  if (WiFi.status() == WL_CONNECTED) {
    if (wifiDisconnectedSince != 0) {
      LOGLN("Wi-Fi restored, IP: " + WiFi.localIP().toString());
    }
    wifiDisconnectedSince = 0;
    return true;
  }

  unsigned long now = millis();
  if (wifiDisconnectedSince == 0) {
    wifiDisconnectedSince = now;
  }

  if (now - lastWiFiReconnect >= WIFI_RECONNECT_INTERVAL) {
    LOGLN("Wi-Fi disconnected, retrying...");
    WiFi.disconnect(false, false);
    delay(50);
    startWiFiConnection();
  }

  if (wifiDisconnectedSince != 0 && now - wifiDisconnectedSince >= WIFI_RESTART_THRESHOLD) {
    LOGLN("Wi-Fi missing too long, restarting ESP32");
    delay(100);
    ESP.restart();
  }

  return false;
}

String floatOrFallback(float value, unsigned int decimals, bool valid, const char* fallback = "нет данных") {
  return valid ? String(static_cast<double>(value), decimals) : String(fallback);
}

String valueWithUnit(float value, unsigned int decimals, bool valid, const char* unit) {
  if (!valid) return "нет данных";
  return String(static_cast<double>(value), decimals) + unit;
}

void readDhtSensor(bool force = false) {
  if (!force && millis() - lastDhtRead < DHT_INTERVAL) return;
  float newTemperature = dht.readTemperature();
  float newHumidity = dht.readHumidity();
  if (!isnan(newTemperature) && !isnan(newHumidity)) {
    temperatureDHT = newTemperature;
    humidityDHT = newHumidity;
    dhtValid = true;
  } else {
    dhtValid = false;
    LOGLN("DHT22 read error");
  }
  lastDhtRead = millis();
}

void readLightSensor(bool force = false) {
  if (!lightSensorAvailable || (!force && millis() - lastLightRead < LIGHT_INTERVAL)) return;
  float newLux = lightMeter.readLightLevel();
  if (!isnan(newLux) && newLux >= 0.0f) {
    lux = newLux;
    lightValid = true;
  } else {
    lightValid = false;
    LOGLN("BH1750 read error");
  }
  lastLightRead = millis();
}

void readBmeSensor(bool force = false) {
  if (!bmeAvailable || (!force && millis() - lastBmeRead < BME_INTERVAL)) return;
  float newTemperature = bme.readTemperature();
  float newHumidity = bme.readHumidity();
  float newPressure = bme.readPressure() / 133.322f;
  if (!isnan(newTemperature) && !isnan(newHumidity) && !isnan(newPressure)) {
    temperatureBME = newTemperature;
    humidityBME = newHumidity;
    pressureBME = newPressure;
    bmeValid = true;
  } else {
    bmeValid = false;
    LOGLN("BME280 read error");
  }
  lastBmeRead = millis();
}

void readDsSensor(bool force = false) {
  if (!force && millis() - lastDsRead < DS_INTERVAL) return;
  sensors.requestTemperatures();
  float newTemperature = sensors.getTempCByIndex(0);
  if (newTemperature != DEVICE_DISCONNECTED_C && !isnan(newTemperature)) {
    temperatureDS = newTemperature;
    dsValid = true;
  } else {
    dsValid = false;
    LOGLN("DS18B20 read error");
  }
  lastDsRead = millis();
}

void readSps30Sensor(bool force) {
  if (!sps30Available || (!force && millis() - lastSps30Read < SPS30_INTERVAL)) return;

  float nc0p5, nc1p0, nc2p5, nc4p0, nc10p0, typicalParticleSize;
  int16_t error = sps30.readMeasurementValuesFloat(sps30pm1p0, sps30pm2p5, sps30pm4p0, sps30pm10p0,
                                                   nc0p5, nc1p0, nc2p5, nc4p0, nc10p0, typicalParticleSize);
  if (!error) {
    sps30Valid = true;
  } else {
    sps30Valid = false;
  }
  lastSps30Read = millis();
}

void updateClock() {
  if (WiFi.status() == WL_CONNECTED) {
    if (timeClient.getEpochTime() < 100000) {
      timeClient.forceUpdate();
    } else {
      timeClient.update();
    }
  }
  if (timeClient.getEpochTime() > 100000) {
    formattedTime = timeClient.getFormattedTime();
  } else {
    formattedTime = "--:--:--";
  }
  if (millis() - lastNtpLog > 60000) {
    lastNtpLog = millis();
    LOGF("Time: %s epoch=%lu\n", formattedTime.c_str(), timeClient.getEpochTime());
  }
}

void updateAllSensors(bool force) {
  readDhtSensor(force);
  readLightSensor(force);
  readBmeSensor(force);
  readDsSensor(force);
  readSps30Sensor(force);
  updateClock();
  freeHeap = ESP.getFreeHeap();
  uptimeSec = millis() / 1000UL;
}

void configureLightningSensorInterrupt() {
  pinMode(AS3935_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(AS3935_PIN), lightningIRQ, RISING);
}

void retryUnavailableComponents() {
  unsigned long now = millis();
  if (now - lastComponentRetry < COMPONENT_RETRY_INTERVAL) return;
  lastComponentRetry = now;

  if (!lightSensorAvailable) {
    lightSensorAvailable = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
    LOGLN(lightSensorAvailable ? "BH1750 restored" : "BH1750 still unavailable");
  }
  if (!bmeAvailable) {
    bmeAvailable = bme.begin(0x76);
    LOGLN(bmeAvailable ? "BME280 restored" : "BME280 still unavailable");
  }
  if (!sps30Available) {
    sps30.begin(Wire, SPS30_I2C_ADDR_69);
    int16_t error = sps30.wakeUpSequence();
    if (!error) {
      error = sps30.startMeasurement(SPS30_OUTPUT_FORMAT_OUTPUT_FORMAT_FLOAT);
      if (!error) {
        sps30Available = true;
        LOGLN("SPS30 restored");
      }
    }
    if (!sps30Available) {
      LOGLN("SPS30 still unavailable");
    }
  }
  if (!lightningSensorAvailable) {
    lightningSensorAvailable = lightning.begin();
    if (lightningSensorAvailable) {
      lightning.setIndoorOutdoor(1);
      lightning.watchdogThreshold(2);
      lightning.setNoiseLevel(1);
      lightning.spikeRejection(2);
      configureLightningSensorInterrupt();
      LOGLN("AS3935 restored");
    } else {
      LOGLN("AS3935 still unavailable");
    }
  }
}

String buildTelegramMessage(bool fromLightning) {
  String message;
  message.reserve(1024);

  if (fromLightning) {
    message = "Обнаружена молния\n\n";
    message += "Расстояние: " + String(lightningDistance) + " км\n";
    message += "Энергия: " + String(lightningEnergy) + "\n";
    message += "Время: " + formattedTime + "\n";
  } else {
    message = "Отчет метеостанции\n\n";
    message += "Время: " + formattedTime + "\n";
    message += "Температура DHT22: " + valueWithUnit(temperatureDHT, 1, dhtValid, " C") + "\n";
    message += "Влажность DHT22: " + valueWithUnit(humidityDHT, 1, dhtValid, " %") + "\n";
    message += "Освещенность BH1750: " + valueWithUnit(lux, 0, lightValid, " lx") + "\n";
    message += "Температура BME280: " + valueWithUnit(temperatureBME, 1, bmeValid, " C") + "\n";
    message += "Влажность BME280: " + valueWithUnit(humidityBME, 1, bmeValid, " %") + "\n";
    message += "Давление BME280: " + valueWithUnit(pressureBME, 1, bmeValid, " mmHg") + "\n";
    message += "Температура DS18B20: " + valueWithUnit(temperatureDS, 1, dsValid, " C") + "\n";
    message += "\nПыль SPS30:\n";
    message += String("PM1.0 ") + valueWithUnit(sps30pm1p0, 1, sps30Valid, " ug/m3") + "\n";
    message += String("PM2.5 ") + valueWithUnit(sps30pm2p5, 1, sps30Valid, " ug/m3") + "\n";
    message += String("PM4.0 ") + valueWithUnit(sps30pm4p0, 1, sps30Valid, " ug/m3") + "\n";
    message += String("PM10.0 ") + valueWithUnit(sps30pm10p0, 1, sps30Valid, " ug/m3") + "\n";
    message += "\nМолнии:\n";
    if (lightningDistance > 0) {
      message += "Расстояние: " + String(lightningDistance) + " км\n";
      message += "Энергия: " + String(lightningEnergy) + "\n";
    } else {
      message += "Расстояние: Нет данных\n";
      message += "Энергия: Нет данных\n";
    }
    message += "Статус: " + String(lightningDetected ? "Обнаружена" : "Спокойно") + "\n";
  }

  message += "\nКарта: https://maps.google.com/?q=" + String(LATITUDE, 4) + "," + String(LONGITUDE, 4);
  return message;
}

bool sendTelegramMessage(bool fromLightning) {
  if (!ensureWiFiConnected()) {
    lastTelegramError = "WiFi disconnected";
    return false;
  }

  String message = buildTelegramMessage(fromLightning);
  DynamicJsonDocument payloadDoc(2048);
  payloadDoc["chat_id"] = TELEGRAM_CHAT_ID;
  payloadDoc["text"] = message;

  String payload;
  payload.reserve(1536);
  serializeJson(payloadDoc, payload);

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(HTTP_TIMEOUT_MS);

  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setReuse(false);

  String url = "https://api.telegram.org/bot" + String(TELEGRAM_TOKEN) + "/sendMessage";
  if (!http.begin(client, url)) {
    consecutiveTelegramFailures++;
    lastTelegramError = "http.begin failed";
    LOGLN("Telegram connection open failed");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  int httpCode = http.POST(payload);
  String response = httpCode > 0 ? http.getString() : String();
  http.end();

  if (httpCode == HTTP_CODE_OK) {
    lastSuccessfulTelegramSend = millis();
    consecutiveTelegramFailures = 0;
    lastTelegramError = "none";
    LOGLN("Telegram sent successfully");
    return true;
  }

  consecutiveTelegramFailures++;
  lastTelegramError = httpCode > 0 ? ("HTTP " + String(httpCode)) : ("HTTP error " + String(httpCode));
  LOGLN("Telegram send failed: " + lastTelegramError);
  if (response.length() > 0) {
    LOGLN("Telegram response: " + response);
  }

  if (consecutiveTelegramFailures >= MAX_TELEGRAM_FAILURES) {
    LOGLN("Too many Telegram failures, restarting ESP32");
    delay(100);
    ESP.restart();
  }

  return false;
}

void IRAM_ATTR lightningIRQ() {
  portENTER_CRITICAL_ISR(&stateMux);
  lightningPending = true;
  lastLightningIrqAt = millis();
  portEXIT_CRITICAL_ISR(&stateMux);
}

void sendToNarodmon() {
  if (!ensureWiFiConnected()) {
    LOGLN("No Wi-Fi, skip Narodmon");
    return;
  }

  if (millis() - lastLightningTimestamp > LIGHTNING_TIMEOUT) {
    lightningDistance = 0;
    lightningEnergy = 0;
  }

  String cleanMac = String(DEVICE_MAC);
  cleanMac.replace(":", "");

  String data;
  data.reserve(256);
  data = "ID=" + cleanMac + "&";
  data += cleanMac + "01=" + floatOrFallback(temperatureDHT, 6, dhtValid, "0") + "&";
  data += cleanMac + "02=" + floatOrFallback(humidityDHT, 6, dhtValid, "0") + "&";
  data += cleanMac + "03=" + floatOrFallback(lux, 6, lightValid, "0") + "&";
  data += cleanMac + "04=" + floatOrFallback(temperatureBME, 6, bmeValid, "0") + "&";
  data += cleanMac + "05=" + floatOrFallback(pressureBME, 6, bmeValid, "0") + "&";
  data += cleanMac + "06=" + floatOrFallback(humidityBME, 6, bmeValid, "0") + "&";
  data += cleanMac + "07=" + floatOrFallback(temperatureDS, 6, dsValid, "0") + "&";
  data += cleanMac + "08=" + floatOrFallback(sps30pm2p5, 6, sps30Valid, "0") + "&";
  data += cleanMac + "09=" + floatOrFallback(sps30pm10p0, 6, sps30Valid, "0") + "&";
  data += cleanMac + "10=" + String(lightningDistance) + "&";
  data += cleanMac + "11=" + String(lightningEnergy) + "&";
  data += cleanMac + "12=" + floatOrFallback(sps30pm4p0, 6, sps30Valid, "0") + "&";
  data += cleanMac + "13=" + floatOrFallback(sps30pm1p0, 6, sps30Valid, "0");

  WiFiClient client;
  client.setTimeout(HTTP_TIMEOUT_MS);

  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setReuse(false);

  if (!http.begin(client, "http://narodmon.ru/post.php")) {
    LOGLN("Narodmon connect failed");
    return;
  }

  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  
  int httpResponseCode = http.POST(data);
  http.end();

  if (httpResponseCode == HTTP_CODE_OK) {
    LOGLN("Narodmon sent successfully");
  } else if (httpResponseCode > 0) {
    LOGLN("Narodmon HTTP code: " + String(httpResponseCode));
  } else {
    LOGLN("Narodmon send error: " + String(httpResponseCode));
  }
}

bool isLightningActive() {
  return lightningDetected && (millis() - lastLightningTimestamp < LIGHTNING_TIMEOUT);
}

float calcDewPoint(float t, float h) {
  if (h <= 0 || t < -40 || t > 60) return t;
  float a = 17.27, b = 237.7;
  float gamma = logf(h / 100.0f) + (a * t) / (b + t);
  return (b * gamma) / (a - gamma);
}

float calcRainProbability() {
  float score = 0;

  if (bmeValid) {
    float h = humidityBME;
    if (h > 90) score += 28;
    else if (h > 80) score += 22;
    else if (h > 70) score += 16;
    else if (h > 60) score += 10;
    else if (h > 50) score += 5;

    float dp = calcDewPoint(temperatureBME, h);
    float diff = temperatureBME - dp;
    if (diff < 1) score += 25;
    else if (diff < 2) score += 20;
    else if (diff < 3) score += 15;
    else if (diff < 5) score += 8;
    else if (diff < 8) score += 3;

    if (pressureHistoryCount >= 4) {
      int n = min(pressureHistoryCount, 12);
      int start = (pressureHistoryIndex - n + PRESSURE_HISTORY_SIZE) % PRESSURE_HISTORY_SIZE;
      float sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;
      for (int i = 0; i < n; i++) {
        int idx = (start + i) % PRESSURE_HISTORY_SIZE;
        float y = pressureHistory[idx];
        float x = (float)i;
        sumX += x; sumY += y; sumXY += x * y; sumX2 += x * x;
      }
      float slope = (n * sumXY - sumX * sumY) / (n * sumX2 - sumX * sumX);
      float pRate = slope * 720.0f;
      if (pRate < -3.0f) score += 35;
      else if (pRate < -2.0f) score += 28;
      else if (pRate < -1.0f) score += 20;
      else if (pRate < -0.5f) score += 12;
      else if (pRate < -0.2f) score += 5;
    }
  }

  if (lightningDetected && (millis() - lastLightningTimestamp < 1800000)) {
    score += 30;
  }

  if (lightValid && lux < 100) score += 5;

  return min(score, 100.0f);
}

void updatePressureHistory() {
  if (!bmeValid) return;
  pressureHistory[pressureHistoryIndex] = pressureBME;
  pressureHistoryIndex = (pressureHistoryIndex + 1) % PRESSURE_HISTORY_SIZE;
  if (pressureHistoryCount < PRESSURE_HISTORY_SIZE) pressureHistoryCount++;
}

void handleLightningEvent() {
  if (!lightningSensorAvailable) return;

  bool pending = false;
  unsigned long irqAt = 0;
  portENTER_CRITICAL(&stateMux);
  pending = lightningPending;
  irqAt = lastLightningIrqAt;
  portEXIT_CRITICAL(&stateMux);

  if (!pending) return;

  unsigned long now = millis();
  if (now - irqAt < LIGHTNING_SETTLE_MS) return;

  portENTER_CRITICAL(&stateMux);
  lightningPending = false;
  portEXIT_CRITICAL(&stateMux);

  if (now - lastLightningTime < LIGHTNING_DEBOUNCE_MS) {
    LOGLN("Lightning debounce: ignored");
    return;
  }

  lastLightningTime = now;
  lightningDistance = lightning.distanceToStorm();
  lightningEnergy = lightning.lightningEnergy();

  if (lightningDistance > 0) {
    lightningDetected = true;
    lastLightningTimestamp = now;
    LOGLN("Lightning detected at " + String(lightningDistance) + " km");
    sendTelegramMessage(true);
  } else {
    LOGLN("AS3935 event classified as noise");
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  LOGLN("Weather station start");


  startWiFiConnection();

  dht.begin();

  Wire.begin(21, 22);
  lightSensorAvailable = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
  bmeAvailable = bme.begin(0x76);

  sps30.begin(Wire, SPS30_I2C_ADDR_69);
  int16_t sps30Error = sps30.wakeUpSequence();
  if (!sps30Error) {
    sps30Error = sps30.startMeasurement(SPS30_OUTPUT_FORMAT_OUTPUT_FORMAT_FLOAT);
    sps30Available = (sps30Error == 0);
  }
  if (sps30Available) {
    LOGLN("SPS30 I2C initialized");
  } else {
    LOGLN("SPS30 I2C init failed");
  }

  sensors.begin();

  lightningSensorAvailable = lightning.begin();
  if (lightningSensorAvailable) {
    LOGLN("AS3935 found on I2C");
    lightning.setIndoorOutdoor(1);
    lightning.watchdogThreshold(2);
    lightning.setNoiseLevel(1);
    lightning.spikeRejection(2);
    configureLightningSensorInterrupt();
    LOGLN("AS3935: outdoor, wdthr=2, noise=1, spike=2");
  } else {
    LOGLN("AS3935 not found on I2C");
  }

  timeClient.begin();
  timeClient.update();

  updateAllSensors(true);

  ArduinoOTA.setHostname("esp32-weather-station");
  ArduinoOTA.setPassword("YOUR_OTA_PASSWORD");
  ArduinoOTA.onStart([]() {
    LOGLN("OTA start");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    LOGF("OTA progress: %u%%\r", progress / (total / 100));
  });
  ArduinoOTA.onEnd([]() {
    LOGLN("\nOTA end");
  });
  ArduinoOTA.begin();
  LOGLN("OTA ready");

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    AsyncWebServerResponse* r = request->beginResponse(200, "text/html; charset=utf-8", index_html);
    r->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    r->addHeader("Pragma", "no-cache");
    r->addHeader("Expires", "0");
    request->send(r);
  });

  server.on("/data", HTTP_GET, [](AsyncWebServerRequest* request) {
    bool pendingLightning = false;
    portENTER_CRITICAL(&stateMux);
    pendingLightning = lightningPending;
    portEXIT_CRITICAL(&stateMux);

    DynamicJsonDocument doc(1536);
    doc["tempDHT"] = temperatureDHT;
    doc["humidityDHT"] = humidityDHT;
    doc["lux"] = lux;
    doc["tempBME"] = temperatureBME;
    doc["humidityBME"] = humidityBME;
    doc["pressureBME"] = pressureBME;
    doc["tempDS"] = temperatureDS;
    doc["sps30pm1p0"] = sps30pm1p0;
    doc["sps30pm2p5"] = sps30pm2p5;
    doc["sps30pm4p0"] = sps30pm4p0;
    doc["sps30pm10p0"] = sps30pm10p0;
    doc["time"] = formattedTime;
    doc["lightningDistance"] = lightningDistance;
    doc["lightningEnergy"] = lightningEnergy;
    doc["lightningDetected"] = isLightningActive();
    doc["wifiConnected"] = WiFi.status() == WL_CONNECTED;
    doc["lastTelegramOk"] = lastSuccessfulTelegramSend;
    doc["lastTelegramError"] = lastTelegramError;
    doc["freeHeap"] = freeHeap;
    doc["uptimeSec"] = uptimeSec;
    doc["lightningPending"] = pendingLightning;
    doc["dhtValid"] = dhtValid;
    doc["lightValid"] = lightValid;
    doc["bmeValid"] = bmeValid;
    doc["dsValid"] = dsValid;
    doc["sps30Valid"] = sps30Valid;
    doc["rainProbability"] = rainProbability;

    String json;
    serializeJson(doc, json);
    AsyncWebServerResponse* r = request->beginResponse(200, "application/json", json);
    r->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    request->send(r);
  });

  server.on("/reset", HTTP_GET, [](AsyncWebServerRequest* request) {
    portENTER_CRITICAL(&stateMux);
    lightningResetPending = true;
    portEXIT_CRITICAL(&stateMux);
    request->send(200, "text/plain; charset=utf-8", "Запрос на сброс данных о молниях принят");
  });

  server.on("/send-telegram", HTTP_GET, [](AsyncWebServerRequest* request) {
    portENTER_CRITICAL(&stateMux);
    manualTelegramPending = true;
    portEXIT_CRITICAL(&stateMux);
    request->send(200, "text/plain; charset=utf-8", "Запрос принят, отправка в Telegram будет выполнена в основном цикле");
  });

  server.on("/logs", HTTP_GET, [](AsyncWebServerRequest* request) {
    String logs;
    logs.reserve(LOG_BUF_SIZE);
    int end = logBufPos;
    int start = (end + 1) % LOG_BUF_SIZE;
    for (int i = start; i != end; i = (i + 1) % LOG_BUF_SIZE) {
      char c = logBuf[i];
      if (c == '\n') logs += "<br>";
      else if (c == '\r') {}
      else logs += c;
    }
    AsyncWebServerResponse* r = request->beginResponse(200, "text/html; charset=utf-8", logs);
    r->addHeader("Cache-Control", "no-cache");
    request->send(r);
  });

  server.begin();
  LOGLN("HTTP server started");
}

void loop() {
  ArduinoOTA.handle();
  

  ensureWiFiConnected();
  retryUnavailableComponents();
  updateClock();
  readDhtSensor();
  readLightSensor();
  readBmeSensor();
  readDsSensor();
  readSps30Sensor();
  updatePressureHistory();
  rainProbability = calcRainProbability();
  freeHeap = ESP.getFreeHeap();
  uptimeSec = millis() / 1000UL;

  bool shouldSendManualTelegram = false;
  bool shouldResetLightning = false;

  portENTER_CRITICAL(&stateMux);
  shouldSendManualTelegram = manualTelegramPending;
  manualTelegramPending = false;
  shouldResetLightning = lightningResetPending;
  lightningResetPending = false;
  portEXIT_CRITICAL(&stateMux);

  if (shouldResetLightning) {
    lightningDistance = 0;
    lightningEnergy = 0;
    lightningDetected = false;
    lastLightningTime = 0;
    lastLightningTimestamp = 0;
    portENTER_CRITICAL(&stateMux);
    lightningPending = false;
    lastLightningIrqAt = 0;
    portEXIT_CRITICAL(&stateMux);
    LOGLN("Lightning data reset");
  }

  handleLightningEvent();

  if (lightningDetected && (millis() - lastLightningTimestamp >= LIGHTNING_TIMEOUT)) {
    lightningDetected = false;
    lightningDistance = 0;
    lightningEnergy = 0;
    LOGLN("Lightning flag reset by timeout");
  }

  if (shouldSendManualTelegram) {
    updateAllSensors(true);
    sendTelegramMessage(false);
  }

  if ((millis() - lastPeriodicTelegramSuccess >= TELEGRAM_INTERVAL) &&
      (millis() - lastTelegramSend >= TELEGRAM_RETRY_INTERVAL)) {
    lastTelegramSend = millis();
    updateAllSensors(true);
    if (sendTelegramMessage(false)) {
      lastPeriodicTelegramSuccess = millis();
    }
  }

  if (millis() - lastNarodmonSend >= NARODMON_INTERVAL) {
    lastNarodmonSend = millis();
    sendToNarodmon();
  }

  
  delay(10);
}
