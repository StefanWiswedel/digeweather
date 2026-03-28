/**
 * Server-side weather data processor for GitHub Actions.
 *
 * Receives a single weather reading from the ESP32 (via repository_dispatch),
 * updates data/weather.json with:
 *   - current reading
 *   - history (last 60 entries)
 *   - hourly aggregation (last 168 hours)
 *
 * Usage: node update-weather.js '<JSON payload>'
 *
 * Payload fields (short keys to minimise ESP32 POST size):
 *   ts  = timestamp  ("2026-03-28 12:00:00")
 *   bat = battery    ("OK" | "LOW")
 *   t   = temperature (float, -999 = missing)
 *   h   = humidity   (uint, 255 = missing)
 *   ws  = windSpeed  (float, -1 = missing)
 *   wg  = windGust   (float)
 *   wd  = windDirection (int degrees)
 *   r   = rain       (float, -1 = missing)
 */

const fs = require('fs');
const path = require('path');

const MAX_HISTORY = 60;
const MAX_HOURLY = 168;

const COMPASS_DIRS = ['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW'];

// ---- Helpers ----

function round1(v) {
  return Math.round(v * 10) / 10;
}

function degreesToCompassIndex(deg) {
  while (deg < 0) deg += 360;
  while (deg >= 360) deg -= 360;
  return Math.floor((deg + 22.5) / 45) % 8;
}

// ---- Build the "current" and "history entry" from payload ----

function buildEntry(p) {
  const entry = {
    timestamp: p.ts,
    battery: p.bat || 'OK'
  };
  if (p.t !== undefined && p.t > -999) entry.temperature = round1(p.t);
  if (p.h !== undefined && p.h <= 100)  entry.humidity = p.h;
  if (p.ws !== undefined && p.ws >= 0) {
    entry.windSpeed = round1(p.ws);
    entry.windGust = round1(p.wg);
    entry.windDirection = Math.round(p.wd);
  }
  if (p.r !== undefined && p.r >= 0) entry.rain = round1(p.r);
  return entry;
}

// ---- Hourly aggregation (mirrors firmware computeHourlyAggregate) ----

function computeHourlyAggregate(history, hourStr) {
  let tempSum = 0, tMin = Infinity, tMax = -Infinity;
  let windSum = 0, wMax = 0;
  let rainStart = -1, rainEnd = 0;
  let humSum = 0, hMin = 255, hMax = 0;
  let tempCount = 0, windCount = 0, humCount = 0;
  const dirCounts = new Array(8).fill(0);

  // Use last 60 history entries (same as firmware)
  const entries = history.slice(-60);
  for (const h of entries) {
    if (h.temperature !== undefined) {
      tempSum += h.temperature;
      if (h.temperature < tMin) tMin = h.temperature;
      if (h.temperature > tMax) tMax = h.temperature;
      tempCount++;
    }
    if (h.humidity !== undefined) {
      humSum += h.humidity;
      if (h.humidity < hMin) hMin = h.humidity;
      if (h.humidity > hMax) hMax = h.humidity;
      humCount++;
    }
    if (h.windSpeed !== undefined) {
      windSum += h.windSpeed;
      if (h.windSpeed > wMax) wMax = h.windSpeed;
      windCount++;
      if (h.windDirection !== undefined) {
        dirCounts[degreesToCompassIndex(h.windDirection)]++;
      }
    }
    if (h.rain !== undefined && h.rain >= 0) {
      if (rainStart < 0) rainStart = h.rain;
      rainEnd = h.rain;
    }
  }

  if (tempCount === 0 && windCount === 0) return null;

  const hourly = { hour: hourStr };

  if (tempCount > 0) {
    hourly.tempAvg = round1(tempSum / tempCount);
    hourly.tempMin = round1(tMin);
    hourly.tempMax = round1(tMax);
  }

  if (windCount > 0) {
    hourly.windAvg = round1(windSum / windCount);
    hourly.windMax = round1(wMax);
    // Dominant wind direction
    let maxC = 0, domIdx = 0;
    for (let i = 0; i < 8; i++) {
      if (dirCounts[i] > maxC) { maxC = dirCounts[i]; domIdx = i; }
    }
    hourly.windDir = COMPASS_DIRS[domIdx];
  }

  if (rainStart >= 0) {
    hourly.rainDelta = round1(rainEnd - rainStart);
  }

  if (humCount > 0) {
    hourly.humidityAvg = round1(humSum / humCount);
    hourly.humidityMin = hMin;
    hourly.humidityMax = hMax;
  }

  return hourly;
}

// ---- Main ----

function main() {
  const payloadStr = process.argv[2];
  if (!payloadStr) {
    console.error('Usage: node update-weather.js \'<JSON payload>\'');
    process.exit(1);
  }

  let payload;
  try {
    payload = JSON.parse(payloadStr);
  } catch (e) {
    console.error('Failed to parse payload:', e.message);
    process.exit(1);
  }

  console.log('Received reading:', payload.ts);

  // Read existing data
  const dataPath = path.join(process.cwd(), 'data', 'weather.json');
  let data = { current: {}, history: [], hourly: [] };
  if (fs.existsSync(dataPath)) {
    try {
      data = JSON.parse(fs.readFileSync(dataPath, 'utf8'));
    } catch (e) {
      console.warn('Failed to parse existing weather.json, starting fresh:', e.message);
      data = { current: {}, history: [], hourly: [] };
    }
  }

  // Ensure arrays exist
  if (!Array.isArray(data.history)) data.history = [];
  if (!Array.isArray(data.hourly)) data.hourly = [];

  // Build new entry
  const entry = buildEntry(payload);

  // Update current
  data.current = entry;

  // Check if hour changed — do hourly aggregation BEFORE adding the new entry
  // (so the aggregate covers the previous hour's data, same as firmware logic)
  const currentHour = payload.ts.substring(0, 13); // "2026-03-28 12"
  const lastHourlyHour = data.hourly.length > 0
    ? data.hourly[data.hourly.length - 1].hour
    : '';

  // Determine previous hour from last history entry
  const lastHistHour = data.history.length > 0
    ? data.history[data.history.length - 1].timestamp.substring(0, 13)
    : '';

  if (lastHistHour && lastHistHour !== lastHourlyHour && data.history.length >= 30) {
    const hourlyEntry = computeHourlyAggregate(data.history, lastHistHour);
    if (hourlyEntry) {
      data.hourly.push(hourlyEntry);
      console.log('Added hourly aggregate for', lastHistHour);
      // Trim hourly
      if (data.hourly.length > MAX_HOURLY) {
        data.hourly = data.hourly.slice(-MAX_HOURLY);
      }
    }
  }

  // Append to history
  data.history.push(entry);
  // Trim history
  if (data.history.length > MAX_HISTORY) {
    data.history = data.history.slice(-MAX_HISTORY);
  }

  // Write back
  const output = JSON.stringify(data);
  fs.writeFileSync(dataPath, output);
  console.log(`Updated weather.json: ${data.history.length} history, ${data.hourly.length} hourly, ${output.length} bytes`);
}

main();
