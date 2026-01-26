/*
 * Bresser 5-in-1 Weather Station Receiver + GitHub Publisher
 * NodeMCU ESP8266 Version
 * Using BresserWeatherSensorReceiver library by matthias-bs
 * https://github.com/matthias-bs/BresserWeatherSensorReceiver
 *
 * WIRING (NodeMCU ESP8266 + CC1101):
 *   CC1101    NodeMCU     GPIO
 *   ------    -------     ----
 *   VCC       3.3V        -
 *   GND       GND         -
 *   CSN       D8          GPIO15
 *   SCLK      D5          GPIO14  (SCK)
 *   SI        D7          GPIO13  (MOSI)
 *   SO        D6          GPIO12  (MISO)
 *   GDO0      D2          GPIO4   (interrupt)
 *   GDO2      D1          GPIO5   (optional)
 *
 * PIN MAPPING from ESP32:
 *   ESP32 GPIO 5  (CS)   -> NodeMCU D8 (GPIO15)
 *   ESP32 GPIO 18 (SCK)  -> NodeMCU D5 (GPIO14)
 *   ESP32 GPIO 23 (MOSI) -> NodeMCU D7 (GPIO13)
 *   ESP32 GPIO 19 (MISO) -> NodeMCU D6 (GPIO12)
 *   ESP32 GPIO 4  (GDO0) -> NodeMCU D2 (GPIO4)
 *   ESP32 GPIO 2  (GDO2) -> NodeMCU D1 (GPIO5)
 */

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>

// Stub out ESP32-specific logging macros used by BresserWeatherSensorReceiver
// The library is designed for ESP32; these stubs allow compilation on ESP8266
#ifndef log_d
#define log_d(...)
#endif
#ifndef log_i
#define log_i(...)
#endif
#ifndef log_w
#define log_w(...)
#endif
#ifndef log_e
#define log_e(...)
#endif

#include "WeatherSensorCfg_ESP8266.h"
#include "WeatherSensor.h"

// ============ Base64 encode/decode for ESP8266 ============
static const char b64_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

String base64Encode(const String& input) {
  String encoded = "";
  int i = 0;
  int j = 0;
  uint8_t arr3[3];
  uint8_t arr4[4];
  int len = input.length();
  const char* bytes = input.c_str();

  while (len--) {
    arr3[i++] = *(bytes++);
    if (i == 3) {
      arr4[0] = (arr3[0] & 0xfc) >> 2;
      arr4[1] = ((arr3[0] & 0x03) << 4) + ((arr3[1] & 0xf0) >> 4);
      arr4[2] = ((arr3[1] & 0x0f) << 2) + ((arr3[2] & 0xc0) >> 6);
      arr4[3] = arr3[2] & 0x3f;
      for (i = 0; i < 4; i++) {
        encoded += b64_alphabet[arr4[i]];
      }
      i = 0;
    }
  }

  if (i) {
    for (j = i; j < 3; j++) {
      arr3[j] = '\0';
    }
    arr4[0] = (arr3[0] & 0xfc) >> 2;
    arr4[1] = ((arr3[0] & 0x03) << 4) + ((arr3[1] & 0xf0) >> 4);
    arr4[2] = ((arr3[1] & 0x0f) << 2) + ((arr3[2] & 0xc0) >> 6);
    arr4[3] = arr3[2] & 0x3f;
    for (j = 0; j < i + 1; j++) {
      encoded += b64_alphabet[arr4[j]];
    }
    while (i++ < 3) {
      encoded += '=';
    }
  }
  return encoded;
}

int b64_lookup(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

String base64Decode(const String& input) {
  String decoded = "";
  int i = 0;
  int j = 0;
  int len = input.length();
  uint8_t arr4[4];
  uint8_t arr3[3];

  for (int pos = 0; pos < len; pos++) {
    char c = input[pos];
    if (c == '=') break;
    int val = b64_lookup(c);
    if (val == -1) continue;

    arr4[i++] = val;
    if (i == 4) {
      arr3[0] = (arr4[0] << 2) + ((arr4[1] & 0x30) >> 4);
      arr3[1] = ((arr4[1] & 0xf) << 4) + ((arr4[2] & 0x3c) >> 2);
      arr3[2] = ((arr4[2] & 0x3) << 6) + arr4[3];
      for (i = 0; i < 3; i++) {
        decoded += (char)arr3[i];
      }
      i = 0;
    }
  }

  if (i) {
    for (j = i; j < 4; j++) {
      arr4[j] = 0;
    }
    arr3[0] = (arr4[0] << 2) + ((arr4[1] & 0x30) >> 4);
    arr3[1] = ((arr4[1] & 0xf) << 4) + ((arr4[2] & 0x3c) >> 2);
    arr3[2] = ((arr4[2] & 0x3) << 6) + arr4[3];
    for (j = 0; j < i - 1; j++) {
      decoded += (char)arr3[j];
    }
  }
  return decoded;
}
// ============ End Base64 ============

// NTP Configuration - Copenhagen (CET/CEST)
const char* NTP_SERVER = "pool.ntp.org";
const long GMT_OFFSET_SEC = 3600;       // GMT+1 (Central European Time)
const int DAYLIGHT_OFFSET_SEC = 3600;   // +1 hour for summer time (CEST)

// ============ CONFIGURATION - EDIT THESE ============
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// GitHub configuration
const char* GITHUB_USER   = "YOUR_GITHUB_USERNAME";
const char* GITHUB_REPO   = "YOUR_REPO_NAME";
const char* GITHUB_TOKEN  = "YOUR_GITHUB_TOKEN";    // Generate at: github.com/settings/tokens
const char* GITHUB_FILE   = "data/weather.json";    // Path in repo

// Update interval (milliseconds)
const unsigned long UPLOAD_INTERVAL = 60000;  // 1 minute
// ====================================================

WeatherSensor ws;

// Accumulated weather data
struct {
  float temperature = -999;
  uint8_t humidity = 255;
  float windSpeed = -1;
  float windGust = -1;
  float windDir = -1;
  float rain = -1;
  bool batteryOk = true;
  uint32_t sensorId = 0;
  unsigned long lastUpdate = 0;
} weather;

unsigned long lastUploadTime = 0;
String currentFileSha = "";

// WiFi client for HTTPS
WiFiClientSecure client;

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("\n\n========================================");
  Serial.println("Bresser Weather Station + GitHub");
  Serial.println("NodeMCU ESP8266 Version");
  Serial.println("========================================\n");

  // Connect to WiFi
  connectWiFi();

  // Initialize weather sensor
  int16_t state = ws.begin();
  if (state != 0) {
    Serial.print("ERROR: Receiver init failed, code: ");
    Serial.println(state);
    while (1) delay(1000);
  }

  Serial.println("CC1101 initialized successfully!");
  Serial.println("Waiting for weather data...\n");
}

void connectWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    // Initialize NTP
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
    Serial.println("Syncing time with NTP...");

    // Wait for time to sync
    time_t now = time(nullptr);
    int syncAttempts = 0;
    while (now < 8 * 3600 * 2 && syncAttempts < 20) {
      delay(500);
      now = time(nullptr);
      syncAttempts++;
    }
    if (syncAttempts < 20) {
      Serial.println("Time synchronized!");
    }

    // Set client to insecure mode (skip certificate verification)
    // For production, consider using fingerprint or certificate
    client.setInsecure();
  } else {
    Serial.println("\nWiFi connection failed! Will retry later.");
  }
}

void loop() {
  // Reconnect WiFi if needed
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  // Clear previous data
  ws.clearSlots();

  // Try to receive weather data
  int decode_status = ws.getMessage();

  if (decode_status == DECODE_OK) {
    for (size_t i = 0; i < ws.sensor.size(); i++) {
      if (ws.sensor[i].valid) {
        updateWeatherData(i);
      }
    }
  }

  // Upload to GitHub at interval
  if (millis() - lastUploadTime >= UPLOAD_INTERVAL && weather.sensorId != 0) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("[");
      Serial.print(getTimestamp());
      Serial.println("] Starting upload...");
      uploadToGitHub();
    } else {
      Serial.println("WiFi disconnected, skipping upload");
    }
    lastUploadTime = millis();
  }

  delay(100);
}

void updateWeatherData(size_t idx) {
  // Debug output
  Serial.print("[RX] ");
  if (ws.sensor[idx].w.temp_ok) {
    Serial.print("T:");
    Serial.print(ws.sensor[idx].w.temp_c, 1);
    Serial.print(" ");
  }
  if (ws.sensor[idx].w.humidity_ok) {
    Serial.print("H:");
    Serial.print(ws.sensor[idx].w.humidity);
    Serial.print(" ");
  }
  if (ws.sensor[idx].w.wind_ok) {
    Serial.print("W:");
    Serial.print(ws.sensor[idx].w.wind_avg_meter_sec, 1);
    Serial.print("m/s ");
  }
  if (ws.sensor[idx].w.rain_ok) {
    Serial.print("R:");
    Serial.print(ws.sensor[idx].w.rain_mm, 1);
  }
  Serial.println();

  // Store sensor ID
  weather.sensorId = ws.sensor[idx].sensor_id;
  weather.batteryOk = ws.sensor[idx].battery_ok;
  weather.lastUpdate = millis();

  // Accumulate valid data
  if (ws.sensor[idx].w.temp_ok) {
    weather.temperature = ws.sensor[idx].w.temp_c;
  }

  if (ws.sensor[idx].w.humidity_ok && ws.sensor[idx].w.humidity <= 100) {
    weather.humidity = ws.sensor[idx].w.humidity;
  }

  if (ws.sensor[idx].w.wind_ok) {
    weather.windSpeed = ws.sensor[idx].w.wind_avg_meter_sec;
    weather.windGust = ws.sensor[idx].w.wind_gust_meter_sec;
    weather.windDir = ws.sensor[idx].w.wind_direction_deg;
  }

  if (ws.sensor[idx].w.rain_ok) {
    weather.rain = ws.sensor[idx].w.rain_mm;
  }
}

String getTimestamp() {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);

  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", timeinfo);
  return String(buf);
}

// Helper to get current hour string (YYYY-MM-DD HH)
String getCurrentHour() {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);

  char buf[16];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H", timeinfo);
  return String(buf);
}

// Convert degrees to compass direction index (0-7 for N,NE,E,SE,S,SW,W,NW)
int degreesToCompassIndex(float degrees) {
  while (degrees < 0) degrees += 360;
  while (degrees >= 360) degrees -= 360;
  return ((int)((degrees + 22.5) / 45.0)) % 8;
}

// Get compass direction name from index
String compassIndexToName(int idx) {
  const char* dirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  return String(dirs[idx % 8]);
}

void uploadToGitHub() {
  Serial.println("\n--- Uploading to GitHub ---");

  String url = "/repos/";
  url += GITHUB_USER;
  url += "/";
  url += GITHUB_REPO;
  url += "/contents/";
  url += GITHUB_FILE;

  HTTPClient http;

  // GET existing file
  http.begin(client, "https://api.github.com" + url);
  http.addHeader("Authorization", String("token ") + GITHUB_TOKEN);
  http.addHeader("Accept", "application/vnd.github.v3+json");
  http.addHeader("User-Agent", "ESP8266-Weather");

  JsonDocument fullDoc;
  JsonArray historyArray;
  JsonArray hourlyArray;

  int httpCode = http.GET();
  if (httpCode == 200) {
    String response = http.getString();
    JsonDocument respDoc;
    deserializeJson(respDoc, response);
    currentFileSha = respDoc["sha"].as<String>();

    String existingContent = respDoc["content"].as<String>();
    existingContent.replace("\n", "");
    String decoded = base64Decode(existingContent);

    if (decoded.length() > 2) {
      deserializeJson(fullDoc, decoded);
      if (fullDoc.containsKey("history")) historyArray = fullDoc["history"].as<JsonArray>();
      if (fullDoc.containsKey("hourly")) hourlyArray = fullDoc["hourly"].as<JsonArray>();
    }

    Serial.print("Existing: ");
    Serial.print(historyArray.size());
    Serial.print(" min, ");
    Serial.print(hourlyArray.size());
    Serial.println(" hourly");
  } else if (httpCode == 404) {
    Serial.println("Creating new file");
    currentFileSha = "";
  } else {
    Serial.print("GET error: ");
    Serial.println(httpCode);
    http.end();
    return;
  }
  http.end();

  // Create current reading
  JsonDocument currentDoc;
  String timestamp = getTimestamp();
  String currentHour = getCurrentHour();

  currentDoc["timestamp"] = timestamp;
  currentDoc["battery"] = weather.batteryOk ? "OK" : "LOW";

  if (weather.temperature > -999) {
    currentDoc["temperature"] = round(weather.temperature * 10) / 10.0;
  }
  if (weather.humidity <= 100) {
    currentDoc["humidity"] = weather.humidity;
  }
  if (weather.windSpeed >= 0) {
    currentDoc["windSpeed"] = round(weather.windSpeed * 10) / 10.0;
    currentDoc["windGust"] = round(weather.windGust * 10) / 10.0;
    currentDoc["windDirection"] = (int)weather.windDir;
  }
  if (weather.rain >= 0) {
    currentDoc["rain"] = round(weather.rain * 10) / 10.0;
  }

  // Build new document
  JsonDocument newDoc;
  newDoc["current"] = currentDoc;

  // === MINUTE DATA (last 24 hours = 1440 entries) ===
  JsonArray newHistory = newDoc["history"].to<JsonArray>();
  int startIdx = (historyArray.size() >= 1440) ? historyArray.size() - 1439 : 0;
  for (int i = startIdx; i < (int)historyArray.size(); i++) {
    newHistory.add(historyArray[i]);
  }
  newHistory.add(currentDoc);

  // === HOURLY AGGREGATION (unlimited) ===
  JsonArray newHourly = newDoc["hourly"].to<JsonArray>();

  // Copy ALL existing hourly data (no limit)
  for (int i = 0; i < (int)hourlyArray.size(); i++) {
    newHourly.add(hourlyArray[i]);
  }

  // Check if we need to aggregate a new hour
  String lastHourlyTs = "";
  if (hourlyArray.size() > 0) {
    lastHourlyTs = hourlyArray[hourlyArray.size() - 1]["hour"].as<String>();
  }

  if (currentHour != lastHourlyTs && historyArray.size() >= 30) {
    // Aggregate last hour from minute data
    float tempSum = 0, tempMin = 999, tempMax = -999;
    float windSum = 0, windMax = 0;
    float rainStart = -1, rainEnd = 0;
    int tempCount = 0, windCount = 0;

    // Count wind directions (8 compass points)
    int directionCounts[8] = {0, 0, 0, 0, 0, 0, 0, 0};

    // Look at entries from the previous hour
    int startFrom = (int)historyArray.size() - 60;
    if (startFrom < 0) startFrom = 0;

    for (int i = startFrom; i < (int)historyArray.size(); i++) {
      JsonObject entry = historyArray[i];

      if (entry.containsKey("temperature")) {
        float t = entry["temperature"].as<float>();
        tempSum += t;
        if (t < tempMin) tempMin = t;
        if (t > tempMax) tempMax = t;
        tempCount++;
      }
      if (entry.containsKey("windSpeed")) {
        float w = entry["windSpeed"].as<float>();
        windSum += w;
        if (w > windMax) windMax = w;
        windCount++;
      }
      if (entry.containsKey("windDirection")) {
        int dir = entry["windDirection"].as<int>();
        int idx = degreesToCompassIndex(dir);
        directionCounts[idx]++;
      }
      if (entry.containsKey("rain")) {
        float r = entry["rain"].as<float>();
        if (rainStart < 0) rainStart = r;
        rainEnd = r;
      }
    }

    if (tempCount > 0 || windCount > 0) {
      JsonObject hourlyEntry = newHourly.add<JsonObject>();
      hourlyEntry["hour"] = lastHourlyTs.length() > 0 ? lastHourlyTs : currentHour;

      if (tempCount > 0) {
        hourlyEntry["tempAvg"] = round(tempSum / tempCount * 10) / 10.0;
        hourlyEntry["tempMin"] = round(tempMin * 10) / 10.0;
        hourlyEntry["tempMax"] = round(tempMax * 10) / 10.0;
      }
      if (windCount > 0) {
        hourlyEntry["windAvg"] = round(windSum / windCount * 10) / 10.0;
        hourlyEntry["windMax"] = round(windMax * 10) / 10.0;

        // Find dominant wind direction
        int maxCount = 0;
        int dominantIdx = 0;
        for (int i = 0; i < 8; i++) {
          if (directionCounts[i] > maxCount) {
            maxCount = directionCounts[i];
            dominantIdx = i;
          }
        }
        if (maxCount > 0) {
          hourlyEntry["windDir"] = compassIndexToName(dominantIdx);
        }
      }
      if (rainStart >= 0) {
        hourlyEntry["rainDelta"] = round((rainEnd - rainStart) * 10) / 10.0;
      }

      Serial.println("Added hourly aggregate");
    }
  }

  // Serialize and upload
  String jsonContent;
  serializeJson(newDoc, jsonContent);

  Serial.print("Uploading: ");
  Serial.print(newHistory.size());
  Serial.print(" min, ");
  Serial.print(newHourly.size());
  Serial.println(" hourly");

  http.begin(client, "https://api.github.com" + url);
  http.addHeader("Authorization", String("token ") + GITHUB_TOKEN);
  http.addHeader("Accept", "application/vnd.github.v3+json");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("User-Agent", "ESP8266-Weather");

  JsonDocument putDoc;
  putDoc["message"] = "Weather update " + timestamp;
  putDoc["content"] = base64Encode(jsonContent);

  if (currentFileSha.length() > 0) {
    putDoc["sha"] = currentFileSha;
  }

  String putBody;
  serializeJson(putDoc, putBody);

  httpCode = http.PUT(putBody);

  if (httpCode == 200 || httpCode == 201) {
    Serial.println("Upload successful!");
    String response = http.getString();
    JsonDocument respDoc;
    deserializeJson(respDoc, response);
    currentFileSha = respDoc["content"]["sha"].as<String>();
  } else {
    Serial.print("Upload failed: ");
    Serial.println(httpCode);
    Serial.println(http.getString());
  }

  http.end();
  Serial.println("----------------------------\n");
}
