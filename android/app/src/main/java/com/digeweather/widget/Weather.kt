package com.digeweather.widget

import android.content.Context
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.time.LocalDateTime
import java.time.format.DateTimeFormatter

/** The four values shown in the widgets. NaN = not available. */
data class Snapshot(
    val timestamp: String,
    val temperature: Double,
    val humidity: Double,
    val rain24h: Double,
    val gust24h: Double,
) {
    fun time(): LocalDateTime? = parseTs(timestamp)

    /** True if the station's last reading is older than [STALE_MINUTES]. */
    fun isStale(now: LocalDateTime = LocalDateTime.now()): Boolean {
        val t = time() ?: return true
        return t.plusMinutes(STALE_MINUTES).isBefore(now)
    }
}

const val STALE_MINUTES = 30L
private const val DATA_URL =
    "https://raw.githubusercontent.com/StefanWiswedel/digeweather/main/data/weather.json"
private val TS_FORMAT = DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss")

fun parseTs(s: String): LocalDateTime? = try {
    when (s.length) {
        13 -> LocalDateTime.parse("$s:00:00", TS_FORMAT) // hourly key "yyyy-MM-dd HH"
        else -> LocalDateTime.parse(s, TS_FORMAT)
    }
} catch (e: Exception) {
    null
}

object Weather {

    /** Downloads weather.json and computes the snapshot. Blocking; call off the main thread. */
    fun fetch(): Snapshot {
        val conn = URL(DATA_URL).openConnection() as HttpURLConnection
        conn.connectTimeout = 8000
        conn.readTimeout = 8000
        conn.useCaches = false
        conn.setRequestProperty("User-Agent", "DigeWeatherWidget/1.0")
        try {
            if (conn.responseCode != 200) throw RuntimeException("HTTP ${conn.responseCode}")
            val body = conn.inputStream.bufferedReader().use { it.readText() }
            return compute(JSONObject(body))
        } finally {
            conn.disconnect()
        }
    }

    /**
     * Same rules as the web dashboard:
     *  - rain 24h = sum of hourly rainDelta for hours within 24h of the latest reading
     *  - gust 24h = max gust over the same window (hourly gustMax, falling back to
     *    windMax for older entries without gustMax) plus the raw readings in history
     */
    fun compute(json: JSONObject): Snapshot {
        val current = json.getJSONObject("current")
        val ts = current.optString("timestamp")
        val latest = parseTs(ts) ?: LocalDateTime.now()
        val cutoff = latest.minusHours(24)

        var rain = 0.0
        var gust = Double.NaN
        fun bumpGust(v: Double) {
            if (!v.isNaN() && (gust.isNaN() || v > gust)) gust = v
        }

        val hourly = json.optJSONArray("hourly")
        if (hourly != null) {
            for (i in 0 until hourly.length()) {
                val h = hourly.getJSONObject(i)
                val t = parseTs(h.optString("hour")) ?: continue
                if (t.isBefore(cutoff)) continue
                rain += maxOf(0.0, h.optDouble("rainDelta", 0.0))
                bumpGust(h.optDouble("gustMax", h.optDouble("windMax")))
            }
        }
        val history = json.optJSONArray("history")
        if (history != null) {
            for (i in 0 until history.length()) {
                val h = history.getJSONObject(i)
                val t = parseTs(h.optString("timestamp")) ?: continue
                if (t.isBefore(cutoff)) continue
                bumpGust(h.optDouble("windGust"))
            }
        }

        return Snapshot(
            timestamp = ts,
            temperature = current.optDouble("temperature"),
            humidity = current.optDouble("humidity"),
            rain24h = rain,
            gust24h = gust,
        )
    }
}

/** Last fetched snapshot, kept so widgets can redraw without the network. */
object Cache {
    private const val PREFS = "weather"

    fun save(ctx: Context, s: Snapshot) {
        ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
            .putString("ts", s.timestamp)
            .putString("t", s.temperature.toString())
            .putString("h", s.humidity.toString())
            .putString("r", s.rain24h.toString())
            .putString("g", s.gust24h.toString())
            .putLong("fetchedAt", System.currentTimeMillis())
            .apply()
    }

    fun load(ctx: Context): Snapshot? {
        val p = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        val ts = p.getString("ts", null) ?: return null
        fun d(k: String) = p.getString(k, null)?.toDoubleOrNull() ?: Double.NaN
        return Snapshot(ts, d("t"), d("h"), d("r"), d("g"))
    }

    fun fetchedAt(ctx: Context): Long =
        ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getLong("fetchedAt", 0L)
}
