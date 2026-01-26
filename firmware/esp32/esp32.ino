/*
 * Bresser 5-in-1 Weather Station Receiver + GitHub Publisher
 * Using BresserWeatherSensorReceiver library by matthias-bs
 * https://github.com/matthias-bs/BresserWeatherSensorReceiver
 *
 * WIRING (ESP32 + CC1101) - Default VSPI pins:
 *   CC1101    ESP32
 *   ------    -----
 *   VCC       3.3V
 *   GND       GND
 *   CSN       GPIO 5
 *   SCLK      GPIO 18
 *   SI        GPIO 23  (MOSI)
 *   SO        GPIO 19  (MISO)
 *   GDO0      GPIO 4   (interrupt)
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "mbedtls/base64.h"
#include <time.h>
#include "WeatherSensorCfg.h"
#include "WeatherSensor.h"

// Base64 helper functions using mbedtls
String base64Encode(const String& input) {
  size_t outputLen = 0;
  mbedtls_base64_encode(NULL, 0, &outputLen, (const unsigned char*)input.c_str(), input.length());

  unsigned char* output = (unsigned char*)malloc(outputLen + 1);
  if (!output) return "";

  mbedtls_base64_encode(output, outputLen, &outputLen, (const unsigned char*)input.c_str(), input.length());
  output[outputLen] = '\0';

  String result = String((char*)output);
  free(output);
  return result;
}

String base64Decode(const String& input) {
  size_t outputLen = 0;
  mbedtls_base64_decode(NULL, 0, &outputLen, (const unsigned char*)input.c_str(), input.length());

  unsigned char* output = (unsigned char*)malloc(outputLen + 1);
  if (!output) return "";

  mbedtls_base64_decode(output, outputLen, &outputLen, (const unsigned char*)input.c_str(), input.length());
  output[outputLen] = '\0';

  String result = String((char*)output);
  free(output);
  return result;
}

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

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("\n\n========================================");
  Serial.println("Bresser Weather Station + GitHub");
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
    struct tm timeinfo;
    int attempts = 0;
    while (!getLocalTime(&timeinfo) && attempts < 10) {
      delay(500);
      attempts++;
    }
    if (attempts < 10) {
      Serial.println("Time synchronized!");
    }
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
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Time not set";
  }

  char buf[32];
  // Format: YYYY-MM-DD HH:MM:SS
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buf);
}

// Helper to get current hour string (YYYY-MM-DD HH)
String getCurrentHour() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "";
  char buf[16];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H", &timeinfo);
  return String(buf);
}

// Convert degrees to compass direction index (0-7 for N,NE,E,SE,S,SW,W,NW)
int degreesToCompassIndex(float degrees) {
  // Normalize to 0-360
  while (degrees < 0) degrees += 360;
  while (degrees >= 360) degrees -= 360;
  // Each direction spans 45 degrees, offset by 22.5 so N is -22.5 to 22.5
  return ((int)((degrees + 22.5) / 45.0)) % 8;
}

// Get compass direction name from index
String compassIndexToName(int idx) {
  const char* dirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  return String(dirs[idx % 8]);
}

void uploadToGitHub() {
  Serial.println("\n--- Uploading to GitHub ---");

  String url = "https://api.github.com/repos/";
  url += GITHUB_USER;
  url += "/";
  url += GITHUB_REPO;
  url += "/contents/";
  url += GITHUB_FILE;

  HTTPClient http;

  // GET existing file
  http.begin(url);
  http.addHeader("Authorization", String("token ") + GITHUB_TOKEN);
  http.addHeader("Accept", "application/vnd.github.v3+json");
  http.addHeader("User-Agent", "ESP32-Weather");

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
    for (int i = max(0, (int)historyArray.size() - 60); i < (int)historyArray.size(); i++) {
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

  http.begin(url);
  http.addHeader("Authorization", String("token ") + GITHUB_TOKEN);
  http.addHeader("Accept", "application/vnd.github.v3+json");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("User-Agent", "ESP32-Weather");

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
