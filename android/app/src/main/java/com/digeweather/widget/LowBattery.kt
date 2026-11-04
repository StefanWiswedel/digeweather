package com.digeweather.widget

import android.Manifest
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build

const val LOW_BATTERY = 20

/**
 * Notifies once when the car drops below [LOW_BATTERY]%. Re-arms after it has been
 * charged above the limit (or is charging), so you get one reminder per low spell.
 */
object LowBattery {
    private const val CHANNEL = "car-battery"
    private const val PREFS = "lowbattery"

    fun check(ctx: Context, car: CarStatus) {
        if (car.level < 0) return
        val prefs = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        val notified = prefs.getBoolean("notified", false)

        if (car.level >= LOW_BATTERY || car.charging) {
            if (notified) prefs.edit().putBoolean("notified", false).apply()
            return
        }
        if (notified) return
        if (notify(ctx, car)) prefs.edit().putBoolean("notified", true).apply()
    }

    private fun notify(ctx: Context, car: CarStatus): Boolean {
        if (Build.VERSION.SDK_INT >= 33 &&
            ctx.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
        ) return false

        val nm = ctx.getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(
            NotificationChannel(CHANNEL, "Car battery", NotificationManager.IMPORTANCE_DEFAULT)
        )
        val open = PendingIntent.getActivity(
            ctx, 0, Intent(ctx, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE
        )
        val range = if (car.rangeKm >= 0) " (${car.rangeKm} km)" else ""
        val n = android.app.Notification.Builder(ctx, CHANNEL)
            .setSmallIcon(R.drawable.ic_battery)
            .setContentTitle("Car battery low: ${car.level}%$range")
            .setContentText("Remember to charge the ZOE.")
            .setContentIntent(open)
            .setAutoCancel(true)
            .build()
        nm.notify(1, n)
        return true
    }
}
