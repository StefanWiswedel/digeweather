package com.digeweather.widget

import android.app.PendingIntent
import android.appwidget.AppWidgetManager
import android.appwidget.AppWidgetProvider
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.graphics.Color
import android.widget.RemoteViews
import java.time.format.DateTimeFormatter
import java.util.Locale
import kotlin.concurrent.thread

const val ACTION_REFRESH = "com.digeweather.widget.REFRESH"

private const val COLOR_TEXT = Color.WHITE
private const val COLOR_STALE = 0xFFFFB74D.toInt() // amber
private val HHMM = DateTimeFormatter.ofPattern("HH:mm")

enum class Metric(val label: String, val unit: String) {
    TEMP("Temp", "°"),
    HUMIDITY("Humidity", "%"),
    RAIN("Rain 24h", " mm"),
    GUST("Gust 24h", " m/s");

    fun value(s: Snapshot): Double = when (this) {
        TEMP -> s.temperature
        HUMIDITY -> s.humidity
        RAIN -> s.rain24h
        GUST -> s.gust24h
    }

    fun format(s: Snapshot?): String {
        val v = s?.let { value(it) } ?: Double.NaN
        if (v.isNaN()) return "--"
        return if (this == HUMIDITY) "${v.toInt()}$unit"
        else String.format(Locale.US, "%.1f", v) + unit
    }
}

/** Base provider: draws from cache, refreshes in the background, tap = refresh. */
abstract class BaseWidget : AppWidgetProvider() {

    abstract fun render(ctx: Context, s: Snapshot?): RemoteViews

    override fun onUpdate(ctx: Context, mgr: AppWidgetManager, ids: IntArray) {
        drawAll(ctx)
        RefreshWorker.schedule(ctx)
        // Fetch if the cache is older than 5 minutes (e.g. widget just added).
        if (System.currentTimeMillis() - Cache.fetchedAt(ctx) > 5 * 60_000L) refreshAsync(ctx)
    }

    override fun onReceive(ctx: Context, intent: Intent) {
        super.onReceive(ctx, intent)
        if (intent.action == ACTION_REFRESH) refreshAsync(ctx)
    }

    private fun refreshAsync(ctx: Context) {
        val pending = goAsync()
        val app = ctx.applicationContext
        thread {
            try {
                refreshNow(app)
            } finally {
                pending.finish()
            }
        }
    }

    protected fun tapIntent(ctx: Context): PendingIntent {
        val i = Intent(ctx, javaClass).setAction(ACTION_REFRESH)
        return PendingIntent.getBroadcast(
            ctx, javaClass.name.hashCode(), i,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
    }

    companion object {
        private val providers = listOf(
            AllWidget::class.java, TempWidget::class.java, HumidityWidget::class.java,
            RainWidget::class.java, GustWidget::class.java, CarWidget::class.java,
        )

        /** Fetches fresh data and redraws every widget. Blocking. Returns true on success. */
        fun refreshNow(ctx: Context): Boolean {
            val ok = try {
                Cache.save(ctx, Weather.fetch())
                true
            } catch (e: Exception) {
                false
            }
            try {
                val car = CarStatus.fetch()
                Cache.saveCar(ctx, car)
                LowBattery.check(ctx, car)
            } catch (e: Exception) {
                // car.json missing or offline: keep last known value
            }
            drawAll(ctx)
            return ok
        }

        fun drawAll(ctx: Context) {
            val mgr = AppWidgetManager.getInstance(ctx)
            val snap = Cache.load(ctx)
            for (cls in providers) {
                val ids = mgr.getAppWidgetIds(ComponentName(ctx, cls))
                if (ids.isEmpty()) continue
                val views = (cls.getDeclaredConstructor().newInstance() as BaseWidget).render(ctx, snap)
                mgr.updateAppWidget(ids, views)
            }
        }

        fun timeColor(s: Snapshot?) = if (s == null || s.isStale()) COLOR_STALE else COLOR_TEXT
        fun timeText(s: Snapshot?) = s?.time()?.format(HHMM) ?: "--:--"
    }
}

/** One value per tile. */
abstract class SingleWidget(private val metric: Metric) : BaseWidget() {
    override fun render(ctx: Context, s: Snapshot?): RemoteViews =
        RemoteViews(ctx.packageName, R.layout.widget_single).apply {
            setTextViewText(R.id.value, metric.format(s))
            setTextViewText(R.id.label, metric.label)
            // Stale data: show the value in amber so it's obvious it's old.
            setTextColor(R.id.value, if (s == null || s.isStale()) COLOR_STALE else COLOR_TEXT)
            setOnClickPendingIntent(R.id.root, tapIntent(ctx))
        }
}

class TempWidget : SingleWidget(Metric.TEMP)
class HumidityWidget : SingleWidget(Metric.HUMIDITY)
class RainWidget : SingleWidget(Metric.RAIN)
class GustWidget : SingleWidget(Metric.GUST)

/** All four values in a 4x1 strip, plus the time of the last reading. */
class AllWidget : BaseWidget() {
    override fun render(ctx: Context, s: Snapshot?): RemoteViews =
        RemoteViews(ctx.packageName, R.layout.widget_all).apply {
            setTextViewText(R.id.temp, Metric.TEMP.format(s))
            setTextViewText(R.id.humidity, Metric.HUMIDITY.format(s))
            setTextViewText(R.id.rain, Metric.RAIN.format(s))
            setTextViewText(R.id.gust, Metric.GUST.format(s))
            setTextViewText(R.id.time, timeText(s))
            setTextColor(R.id.time, timeColor(s))
            setOnClickPendingIntent(R.id.root, tapIntent(ctx))
        }
}

/** Car battery tile: % and range, background red/amber/green by charge level. */
class CarWidget : BaseWidget() {
    override fun render(ctx: Context, s: Snapshot?): RemoteViews {
        val car = Cache.loadCar(ctx)
        return RemoteViews(ctx.packageName, R.layout.widget_single).apply {
            val level = car?.level ?: -1
            val bolt = if (car?.charging == true) "⚡" else ""
            setTextViewText(R.id.value, if (level >= 0) "$bolt$level%" else "--")
            setTextViewText(
                R.id.label,
                if (car != null && car.rangeKm >= 0) "Car · ${car.rangeKm} km" else "Car"
            )
            setTextColor(R.id.value, COLOR_TEXT)
            setInt(R.id.root, "setBackgroundResource", batteryBackground(level))
            setOnClickPendingIntent(R.id.root, tapIntent(ctx))
        }
    }
}

fun batteryBackground(level: Int) = when {
    level < 0 -> R.drawable.widget_bg
    level < LOW_BATTERY -> R.drawable.widget_bg_red
    level < 50 -> R.drawable.widget_bg_amber
    else -> R.drawable.widget_bg_green
}
