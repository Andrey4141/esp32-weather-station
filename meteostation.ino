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
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SparkFun_AS3935.h>
#include <SensirionUartSps30.h>
#include <ArduinoOTA.h>
#include <esp_task_wdt.h>

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

IPAddress ip(192, 168, 88, 110);
IPAddress gateway(192, 168, 88, 1);
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
SensirionUartSps30 sps30;
#define SPS30_RX_PIN 18
#define SPS30_TX_PIN 5
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

    function pmClass(val) {
      if (val <= 35) return 'pm-good';
      if (val <= 75) return 'pm-warning';
      return 'pm-danger';
    }

    function updateData() {
      fetch('/data')
        .then(response => response.json())
        .then(data => {
          document.getElementById('temp-dht').innerText = data.tempDHT + ' °C';
          document.getElementById('humidity-dht').innerText = data.humidityDHT + ' %';
          document.getElementById('lux').innerText = data.lux + ' lx';
          document.getElementById('temp-bme').innerText = data.tempBME + ' °C';
          document.getElementById('humidity-bme').innerText = data.humidityBME + ' %';
          document.getElementById('pressure').innerText = data.pressureBME + ' мм рт. ст.';
          document.getElementById('temp-ds').innerText = data.tempDS + ' °C';
          document.getElementById('time').innerText = data.time;

          const pm25el = document.getElementById('pm2p5');
          pm25el.innerText = data.sps30pm2p5.toFixed(1) + ' µg/m³';
          pm25el.className = 'sensor-value ' + pmClass(data.sps30pm2p5);

          const pm10el = document.getElementById('pm10p0');
          pm10el.innerText = data.sps30pm10p0.toFixed(1) + ' µg/m³';
          pm10el.className = 'sensor-value ' + pmClass(data.sps30pm10p0);

          document.getElementById('pm1p0').innerText = data.sps30pm1p0.toFixed(1) + ' µg/m³';
          document.getElementById('pm4p0').innerText = data.sps30pm4p0.toFixed(1) + ' µg/m³';

          document.getElementById('lightning-distance').innerText =
            data.lightningDistance > 0 ? data.lightningDistance + ' км' : 'Нет данных';
          document.getElementById('lightning-energy').innerText =
            data.lightningEnergy > 0 ? data.lightningEnergy : 'Нет данных';

          const lightningStatus = document.getElementById('lightning-status');
          lightningStatus.innerText = data.lightningDetected ? 'Обнаружена' : 'Спокойно';
          lightningStatus.className = data.lightningDetected
            ? 'sensor-value lightning-value'
            : 'sensor-value';

          document.getElementById('last-update').innerText = new Date().toLocaleTimeString();
        })
        .catch(() => {
          document.getElementById('last-update').innerText = 'Ошибка связи';
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

    document.addEventListener('DOMContentLoaded', () => {
      initMap();
      updateData();
      setInterval(updateData, 3000);
    });
  </script>
</head>
<body>
  <h1>Метеостанция</h1>
  <div class="container">
    <div class="data-panel">
      <div class="card">
        <p>Текущее время: <span id="time">--:--:--</span></p>
        <p class="update-info">Последнее обновление: <span id="last-update">--:--:--</span></p>
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
  Serial.println("Wi-Fi: start connect");
}

bool ensureWiFiConnected() {
  if (WiFi.status() == WL_CONNECTED) {
    if (wifiDisconnectedSince != 0) {
      Serial.println("Wi-Fi restored, IP: " + WiFi.localIP().toString());
    }
    wifiDisconnectedSince = 0;
    return true;
  }

  unsigned long now = millis();
  if (wifiDisconnectedSince == 0) {
    wifiDisconnectedSince = now;
  }

  if (now - lastWiFiReconnect >= WIFI_RECONNECT_INTERVAL) {
    Serial.println("Wi-Fi disconnected, retrying...");
    WiFi.disconnect(false, false);
    delay(50);
    startWiFiConnection();
  }

  if (wifiDisconnectedSince != 0 && now - wifiDisconnectedSince >= WIFI_RESTART_THRESHOLD) {
    Serial.println("Wi-Fi missing too long, restarting ESP32");
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
    Serial.println("DHT22 read error");
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
    Serial.println("BH1750 read error");
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
    Serial.println("BME280 read error");
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
    Serial.println("DS18B20 read error");
  }
  lastDsRead = millis();
}

void readSps30Sensor(bool force) {
  if (!sps30Available || (!force && millis() - lastSps30Read < SPS30_INTERVAL)) return;

  float nc0p5, nc1p0, nc2p5, nc4p0, nc10p0, typicalParticleSize;
  int16_t error = sps30.readMeasurementValuesFloat(sps30pm1p0, sps30pm2p5, sps30pm4p0, sps30pm10p0,
                                                   nc0p5, nc1p0, nc2p5, nc4p0, nc10p0, typicalParticleSize);
  if (!error) {
    sps30pm1p0 = roundf(sps30pm1p0 * 10.0f) / 10.0f;
    sps30pm2p5 = roundf(sps30pm2p5 * 10.0f) / 10.0f;
    sps30pm4p0 = roundf(sps30pm4p0 * 10.0f) / 10.0f;
    sps30pm10p0 = roundf(sps30pm10p0 * 10.0f) / 10.0f;
    sps30Valid = true;
  } else {
    sps30Valid = false;
  }
  lastSps30Read = millis();
}

void updateClock() {
  if (WiFi.status() == WL_CONNECTED) {
    timeClient.update();
  }
  formattedTime = timeClient.getFormattedTime();
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
    Serial.println(lightSensorAvailable ? "BH1750 restored" : "BH1750 still unavailable");
  }
  if (!bmeAvailable) {
    bmeAvailable = bme.begin(0x76);
    Serial.println(bmeAvailable ? "BME280 restored" : "BME280 still unavailable");
  }
  if (!sps30Available) {
    Serial2.begin(115200, SERIAL_8N1, SPS30_RX_PIN, SPS30_TX_PIN);
    sps30.begin(Serial2);
    int16_t error = sps30.wakeUpSequence();
    if (!error) {
      error = sps30.startMeasurement(SPS30_OUTPUT_FORMAT_OUTPUT_FORMAT_FLOAT);
      if (!error) {
        sps30Available = true;
        Serial.println("SPS30 restored");
      }
    }
    if (!sps30Available) {
      Serial.println("SPS30 still unavailable");
    }
  }
  if (!lightningSensorAvailable) {
    lightningSensorAvailable = lightning.begin();
    if (lightningSensorAvailable) {
      lightning.setIndoorOutdoor(0);
      lightning.watchdogThreshold(2);
      configureLightningSensorInterrupt();
      Serial.println("AS3935 restored");
    } else {
      Serial.println("AS3935 still unavailable");
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
    message += valueWithUnit(sps30pm1p0, 1, sps30Valid, " PM1.0 ug/m3") + "\n";
    message += valueWithUnit(sps30pm2p5, 1, sps30Valid, " PM2.5 ug/m3") + "\n";
    message += valueWithUnit(sps30pm4p0, 1, sps30Valid, " PM4.0 ug/m3") + "\n";
    message += valueWithUnit(sps30pm10p0, 1, sps30Valid, " PM10.0 ug/m3") + "\n";
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

  message += "\nКарта: https://maps.google.com/?q=" + String(LATITUDE, 6) + "," + String(LONGITUDE, 6);
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
    Serial.println("Telegram connection open failed");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  esp_task_wdt_reset();
  int httpCode = http.POST(payload);
  String response = httpCode > 0 ? http.getString() : String();
  http.end();

  if (httpCode == HTTP_CODE_OK) {
    lastSuccessfulTelegramSend = millis();
    consecutiveTelegramFailures = 0;
    lastTelegramError = "none";
    Serial.println("Telegram sent successfully");
    return true;
  }

  consecutiveTelegramFailures++;
  lastTelegramError = httpCode > 0 ? ("HTTP " + String(httpCode)) : ("HTTP error " + String(httpCode));
  Serial.println("Telegram send failed: " + lastTelegramError);
  if (response.length() > 0) {
    Serial.println("Telegram response: " + response);
  }

  if (consecutiveTelegramFailures >= MAX_TELEGRAM_FAILURES) {
    Serial.println("Too many Telegram failures, restarting ESP32");
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
    Serial.println("No Wi-Fi, skip Narodmon");
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
  data += cleanMac + "01=" + floatOrFallback(temperatureDHT, 1, dhtValid, "0") + "&";
  data += cleanMac + "02=" + floatOrFallback(humidityDHT, 1, dhtValid, "0") + "&";
  data += cleanMac + "03=" + floatOrFallback(lux, 0, lightValid, "0") + "&";
  data += cleanMac + "04=" + floatOrFallback(temperatureBME, 1, bmeValid, "0") + "&";
  data += cleanMac + "05=" + floatOrFallback(pressureBME, 1, bmeValid, "0") + "&";
  data += cleanMac + "06=" + floatOrFallback(humidityBME, 1, bmeValid, "0") + "&";
  data += cleanMac + "07=" + floatOrFallback(temperatureDS, 1, dsValid, "0") + "&";
  data += cleanMac + "08=" + floatOrFallback(sps30pm2p5, 1, sps30Valid, "0") + "&";
  data += cleanMac + "09=" + floatOrFallback(sps30pm10p0, 1, sps30Valid, "0") + "&";
  data += cleanMac + "10=" + String(lightningDistance) + "&";
  data += cleanMac + "11=" + String(lightningEnergy);

  WiFiClient client;
  client.setTimeout(HTTP_TIMEOUT_MS);

  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setReuse(false);

  if (!http.begin(client, "http://narodmon.ru/post.php")) {
    Serial.println("Narodmon connect failed");
    return;
  }

  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  esp_task_wdt_reset();
  int httpResponseCode = http.POST(data);
  http.end();

  if (httpResponseCode == HTTP_CODE_OK) {
    Serial.println("Narodmon sent successfully");
  } else if (httpResponseCode > 0) {
    Serial.println("Narodmon HTTP code: " + String(httpResponseCode));
  } else {
    Serial.println("Narodmon send error: " + String(httpResponseCode));
  }
}

bool isLightningActive() {
  return lightningDetected && (millis() - lastLightningTimestamp < LIGHTNING_TIMEOUT);
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
    Serial.println("Lightning debounce: ignored");
    return;
  }

  lastLightningTime = now;
  lightningDistance = lightning.distanceToStorm();
  lightningEnergy = lightning.lightningEnergy();

  if (lightningDistance > 0) {
    lightningDetected = true;
    lastLightningTimestamp = now;
    Serial.println("Lightning detected at " + String(lightningDistance) + " km");
    sendTelegramMessage(true);
  } else {
    Serial.println("AS3935 event classified as noise");
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("Weather station start");

  esp_task_wdt_init(15, true);
  esp_task_wdt_add(NULL);
  esp_task_wdt_reset();

  startWiFiConnection();

  dht.begin();

  Wire.begin(21, 22);
  lightSensorAvailable = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
  bmeAvailable = bme.begin(0x76);

  Serial2.begin(115200, SERIAL_8N1, SPS30_RX_PIN, SPS30_TX_PIN);
  sps30.begin(Serial2);
  int16_t sps30Error = sps30.wakeUpSequence();
  if (!sps30Error) {
    sps30Error = sps30.startMeasurement(SPS30_OUTPUT_FORMAT_OUTPUT_FORMAT_FLOAT);
    sps30Available = (sps30Error == 0);
  }
  if (sps30Available) {
    Serial.println("SPS30 UART initialized");
  } else {
    Serial.println("SPS30 UART init failed");
  }

  sensors.begin();

  lightningSensorAvailable = lightning.begin();
  if (lightningSensorAvailable) {
    lightning.setIndoorOutdoor(0);
    lightning.watchdogThreshold(2);
    configureLightningSensorInterrupt();
  }

  timeClient.begin();
  timeClient.update();

  updateAllSensors(true);

  ArduinoOTA.setHostname("meteostation");
  ArduinoOTA.setPassword("YOUR_OTA_PASSWORD");
  ArduinoOTA.onStart([]() {
    Serial.println("OTA start");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA progress: %u%%\r", progress / (total / 100));
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA end");
  });
  ArduinoOTA.begin();
  Serial.println("OTA ready");

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/html; charset=utf-8", index_html);
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

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
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

  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  ArduinoOTA.handle();
  esp_task_wdt_reset();

  ensureWiFiConnected();
  retryUnavailableComponents();
  updateClock();
  readDhtSensor();
  readLightSensor();
  readBmeSensor();
  readDsSensor();
  readSps30Sensor();
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
    Serial.println("Lightning data reset");
  }

  handleLightningEvent();

  if (lightningDetected && (millis() - lastLightningTimestamp >= LIGHTNING_TIMEOUT)) {
    lightningDetected = false;
    lightningDistance = 0;
    lightningEnergy = 0;
    Serial.println("Lightning flag reset by timeout");
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

  esp_task_wdt_reset();
  delay(10);
}
