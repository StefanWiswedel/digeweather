package com.digeweather.widget

import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import org.json.JSONObject
import java.time.LocalDate
import java.time.LocalTime
import java.util.Locale

/** One notification per morning when the GitHub job pre-heats (or skips/fails). */
object PreheatNotify {
    private const val CHANNEL = "preheat"
    private const val PREFS = "preheat"

    fun describe(j: JSONObject): String {
        val low = if (j.isNull("overnightLow")) "" else
            " (overnight low %.1f°)".format(Locale.US, j.optDouble("overnightLow"))
        val battery = if (j.isNull("battery")) "" else "${j.optInt("battery")}%"
        return when (j.optString("result")) {
            "sent" -> "Pre-heat started at ${j.optString("time")}$low" +
                (if (j.optInt("attempts", 1) > 1) ", try ${j.optInt("attempts")}" else "")
            "skipped_battery" -> "Frost likely$low, but battery is $battery: not pre-heating"
            "failed" -> "Pre-heat FAILED after ${j.optInt("attempts", 1)} tries$low"
            else -> j.optString("result")
        }
    }

    fun check(ctx: Context, j: JSONObject) {
        val date = j.optString("date")
        if (date != LocalDate.now().toString()) return // only today's
        val result = j.optString("result")
        // Failed attempts are retried every minute until 06:00; only report a failure
        // once the retries are over.
        if (result == "failed" && LocalTime.now().isBefore(LocalTime.of(6, 0))) return
        val key = "$date/$result"
        val prefs = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        if (prefs.getString("notifiedKey", "") == key) return

        if (Build.VERSION.SDK_INT >= 33 &&
            ctx.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
        ) return

        val nm = ctx.getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(
            NotificationChannel(CHANNEL, "Morning pre-heat", NotificationManager.IMPORTANCE_DEFAULT)
        )
        val open = PendingIntent.getActivity(
            ctx, 0, Intent(ctx, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE
        )
        val n = Notification.Builder(ctx, CHANNEL)
            .setSmallIcon(R.drawable.ic_battery)
            .setContentTitle(describe(j))
            .setContentText("Renault accepted the request; the car itself doesn't confirm.")
            .setContentIntent(open)
            .setAutoCancel(true)
            .build()
        nm.notify(2, n)
        prefs.edit().putString("notifiedKey", key).apply()
    }
}
