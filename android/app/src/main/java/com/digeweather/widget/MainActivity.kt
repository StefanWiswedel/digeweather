package com.digeweather.widget

import android.Manifest
import android.app.Activity
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Color
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.view.Gravity
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import org.json.JSONObject
import java.time.Duration
import java.time.Instant
import java.time.LocalDateTime
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.util.Locale
import kotlin.concurrent.thread

class MainActivity : Activity() {

    private val timeFmt = DateTimeFormatter.ofPattern("HH:mm")
    private val dayTimeFmt = DateTimeFormatter.ofPattern("d MMM HH:mm", Locale.ENGLISH)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<Button>(R.id.refresh).setOnClickListener { refresh() }
        findViewById<Button>(R.id.dashboard).setOnClickListener {
            startActivity(
                Intent(Intent.ACTION_VIEW, Uri.parse("https://stefanwiswedel.github.io/digeweather/"))
            )
        }
        RefreshWorker.schedule(this)
        // Needed on Android 13+ for the low-battery reminder.
        if (Build.VERSION.SDK_INT >= 33 &&
            checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
        ) {
            requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 1)
        }
        show(null)
    }

    // Opening the app (also from a widget tap) always fetches fresh data.
    override fun onResume() {
        super.onResume()
        refresh()
    }

    private fun refresh() {
        findViewById<TextView>(R.id.status).text = getString(R.string.loading)
        findViewById<Button>(R.id.refresh).isEnabled = false
        thread {
            val ok = BaseWidget.refreshNow(applicationContext)
            runOnUiThread {
                findViewById<Button>(R.id.refresh).isEnabled = true
                show(if (ok) null else getString(R.string.fetch_failed))
            }
        }
    }

    private fun show(error: String?) {
        showWeather()
        showCar()
        findViewById<TextView>(R.id.status).text = error ?: ""
    }

    private fun showWeather() {
        val s = Cache.load(this)
        val rows = findViewById<LinearLayout>(R.id.weather_rows)
        rows.removeAllViews()
        row(rows, "Temperature", Metric.TEMP.format(s))
        row(rows, "Humidity", Metric.HUMIDITY.format(s))
        row(rows, "Rain (24h)", Metric.RAIN.format(s))
        row(rows, "Max gust (24h)", Metric.GUST.format(s))

        val t = findViewById<TextView>(R.id.weather_time)
        val reading = s?.time()
        t.text = buildString {
            append("Station reading: ")
            append(reading?.let { "${it.format(timeFmt)} (${ago(Duration.between(it, LocalDateTime.now()))})" } ?: "--")
            append("\nLast refreshed: ").append(fetchedText(Cache.fetchedAt(this@MainActivity)))
        }
        t.setTextColor(if (s == null || s.isStale()) AMBER else LABEL)
    }

    private fun showCar() {
        val j = Cache.loadCarJson(this)
        val rows = findViewById<LinearLayout>(R.id.car_rows)
        rows.removeAllViews()
        val t = findViewById<TextView>(R.id.car_time)
        if (j == null) {
            row(rows, "Battery", "--")
            t.text = "No car data yet."
            return
        }
        val car = CarStatus.parse(j)
        val level = if (car.level >= 0) "${car.level}%" else "--"
        row(rows, "Battery", level, batteryColor(car.level))
        row(rows, "Range", if (car.rangeKm >= 0) "${car.rangeKm} km" else "--")
        row(rows, "Plugged in", if (car.plugged) "Yes" else "No")
        if (car.charging) {
            val mins = j.optInt("chargingRemainingMinutes", -1)
            row(rows, "Charging", if (mins > 0) "Yes · full in ${hm(mins)}" else "Yes")
        } else {
            row(rows, "Charging", "No")
        }

        j.optJSONObject("climate")?.let { c ->
            c.str("status")?.let { row(rows, "Climate", if (it == "on") "On" else it.replaceFirstChar { ch -> ch.uppercase() }) }
            c.num("insideTemp")?.let { row(rows, "Inside temp", "%.0f°".format(Locale.US, it)) }
            c.num("outsideTemp")?.let { row(rows, "Outside temp (car)", "%.0f°".format(Locale.US, it)) }
            c.num("minBattery")?.let { row(rows, "Climate needs battery ≥", "%.0f%%".format(Locale.US, it)) }
        }
        j.optJSONObject("tyres")?.let { p ->
            val vals = listOf("fl", "fr", "rl", "rr").map { p.num(it) }
            if (vals.any { it != null && it > 0 }) {
                // Renault reports mbar
                fun bar(v: Double?) = v?.let { "%.1f".format(Locale.US, it / 1000) } ?: "-"
                row(rows, "Tyres front (bar)", "${bar(vals[0])} / ${bar(vals[1])}")
                row(rows, "Tyres rear (bar)", "${bar(vals[2])} / ${bar(vals[3])}")
            }
        }

        val reported = try {
            Instant.parse(car.timestamp)
        } catch (e: Exception) {
            null
        }
        t.text = buildString {
            append("Car last reported: ")
            append(reported?.let {
                "${it.atZone(ZoneId.systemDefault()).format(dayTimeFmt)} (${ago(Duration.between(it, Instant.now()))})"
            } ?: "--")
            append("\nLast refreshed: ").append(fetchedText(Cache.carFetchedAt(this@MainActivity)))
            append("\nThe car only reports when it's awake, so this can be hours old.")
        }
    }

    // ---- helpers ----

    private fun row(parent: LinearLayout, label: String, value: String, valueColor: Int = Color.WHITE) {
        val line = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setPadding(0, dp(4), 0, dp(4))
        }
        line.addView(TextView(this).apply {
            text = label
            setTextColor(LABEL)
            textSize = 15f
            layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
        })
        line.addView(TextView(this).apply {
            text = value
            setTextColor(valueColor)
            textSize = 17f
            gravity = Gravity.END
            setTypeface(typeface, android.graphics.Typeface.BOLD)
        })
        parent.addView(line)
    }

    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()

    private fun fetchedText(millis: Long): String {
        if (millis <= 0L) return "never"
        val i = Instant.ofEpochMilli(millis)
        return i.atZone(ZoneId.systemDefault()).format(timeFmt)
    }

    private fun ago(d: Duration): String {
        val m = d.toMinutes()
        return when {
            m < 1 -> "just now"
            m < 60 -> "$m min ago"
            m < 48 * 60 -> "${m / 60} h ago"
            else -> "${m / (24 * 60)} days ago"
        }
    }

    private fun hm(mins: Int) = if (mins >= 60) "${mins / 60} h ${mins % 60} min" else "$mins min"

    private fun batteryColor(level: Int) = when {
        level < 0 -> Color.WHITE
        level < LOW_BATTERY -> 0xFFEF5350.toInt()
        level < 50 -> AMBER
        else -> 0xFF66BB6A.toInt()
    }

    private fun JSONObject.str(k: String): String? = if (isNull(k)) null else optString(k).ifEmpty { null }
    private fun JSONObject.num(k: String): Double? = if (isNull(k)) null else optDouble(k).takeIf { !it.isNaN() }

    companion object {
        private const val LABEL = 0xB3FFFFFF.toInt()
        private const val AMBER = 0xFFFFB74D.toInt()
    }
}
