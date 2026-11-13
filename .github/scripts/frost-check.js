/**
 * Decides whether to pre-condition the car this reading.
 * Run AFTER update-weather.js has written data/weather.json.
 *
 * Settings come from config/precondition.json ({"enabled": bool, "frostTemp": °C}),
 * which the phone app can change.
 *
 * Yes when ALL of:
 *   - enabled in config/precondition.json
 *   - weekday (Mon–Fri)
 *   - the reading is between START_TIME and LATEST_TIME, and today's attempt hasn't
 *     already succeeded (failed attempts retry on the next reading)
 *   - the lowest temperature in the last LOOKBACK_HOURS was <= FROST_TEMP
 *
 * Timestamps are the station's local time. Prints "precondition=true|false" to
 * $GITHUB_OUTPUT when available.
 */
const fs = require('fs');
const path = require('path');

const START_TIME = process.env.START_TIME || '05:45';   // car runs ~15 min -> ready for 06:00
const LATEST_TIME = process.env.LATEST_TIME || '06:00'; // don't fire after this
function loadConfig() {
  try {
    return JSON.parse(fs.readFileSync(path.join(process.cwd(), 'config', 'precondition.json'), 'utf8'));
  } catch (e) {
    return {};
  }
}
const CONFIG = loadConfig();
const ENABLED = CONFIG.enabled !== false;
const FROST_TEMP = typeof CONFIG.frostTemp === 'number' ? CONFIG.frostTemp : 5;
const LOOKBACK_HOURS = parseFloat(process.env.LOOKBACK_HOURS || '4');

function parse(ts) {
  // "2026-11-04 05:45:12" or hourly "2026-11-04 05" -> Date (treated as UTC; only differences matter)
  const s = ts.length === 13 ? ts + ':00:00' : ts;
  return new Date(s.replace(' ', 'T') + 'Z');
}

function hhmm(ts) { return ts.substring(11, 16); }

function loadPreheat() {
  try {
    return JSON.parse(fs.readFileSync(path.join(process.cwd(), 'data', 'preheat.json'), 'utf8'));
  } catch (e) {
    return {};
  }
}

function decide(data, { enabled = ENABLED, frostTemp = FROST_TEMP, preheat = loadPreheat() } = {}) {
  if (!enabled) return { yes: false, reason: 'turned off in settings' };
  const history = data.history || [];
  if (history.length < 2) return { yes: false, reason: 'not enough history' };
  const cur = history[history.length - 1];
    const now = parse(cur.timestamp);

  const day = now.getUTCDay(); // 0 = Sunday (local time, parsed as UTC)
  if (day === 0 || day === 6) return { yes: false, reason: 'weekend' };

  // Try on every reading from START_TIME until LATEST_TIME until today's attempt has
  // succeeded (or was skipped for low battery). Renault sometimes refuses a call for a
  // while, so a failed attempt is retried on the next reading (about a minute later).
  const t = hhmm(cur.timestamp);
  if (t < START_TIME || t >= LATEST_TIME) return { yes: false, reason: `outside ${START_TIME}-${LATEST_TIME} (${t})` };
  const today = cur.timestamp.substring(0, 10);
  if (preheat.date === today && preheat.result !== 'failed') {
    return { yes: false, reason: `already done today (${preheat.result})` };
  }

  const cutoff = new Date(now.getTime() - LOOKBACK_HOURS * 3600 * 1000);
  let min = Infinity;
  for (const h of data.hourly || []) {
    // Hourly entry "HH" is written just after HH:00 and covers the hour before it
    // (HH-1:00 to HH:00), so include it if that hour ends after the cutoff.
    if (h.tempMin !== undefined && parse(h.hour).getTime() > cutoff.getTime()) {
      min = Math.min(min, h.tempMin);
    }
  }
  for (const h of history) {
    if (h.temperature !== undefined && parse(h.timestamp) >= cutoff) min = Math.min(min, h.temperature);
  }
  if (min === Infinity) return { yes: false, reason: 'no temperature data' };

  const yes = min <= frostTemp;
  return {
    yes, low: min, date: cur.timestamp.substring(0, 10), time: t,
    reason: `lowest ${min}°C in last ${LOOKBACK_HOURS}h (limit ${frostTemp}°C)`,
  };
}

if (require.main === module) {
  const data = JSON.parse(fs.readFileSync(path.join(process.cwd(), 'data', 'weather.json'), 'utf8'));
  const r = decide(data);
  console.log(`Pre-condition: ${r.yes ? 'YES' : 'no'} – ${r.reason}`);
  if (process.env.GITHUB_OUTPUT) {
    fs.appendFileSync(process.env.GITHUB_OUTPUT,
      `precondition=${r.yes}\nlow=${r.low ?? ''}\ndate=${r.date ?? ''}\ntime=${r.time ?? ''}\n`);
  }
}

module.exports = { decide };
