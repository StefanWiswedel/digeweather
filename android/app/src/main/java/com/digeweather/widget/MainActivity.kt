package com.digeweather.widget

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.widget.Button
import android.widget.TextView
import kotlin.concurrent.thread

class MainActivity : Activity() {

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
        show(Cache.load(this), null)
        refresh()
    }

    private fun refresh() {
        findViewById<TextView>(R.id.status).text = getString(R.string.loading)
        thread {
            val ok = BaseWidget.refreshNow(applicationContext)
            runOnUiThread {
                show(Cache.load(this), if (ok) null else getString(R.string.fetch_failed))
            }
        }
    }

    private fun show(s: Snapshot?, error: String?) {
        findViewById<TextView>(R.id.temp).text = Metric.TEMP.format(s)
        findViewById<TextView>(R.id.humidity).text = Metric.HUMIDITY.format(s)
        findViewById<TextView>(R.id.rain).text = Metric.RAIN.format(s)
        findViewById<TextView>(R.id.gust).text = Metric.GUST.format(s)
        val status = when {
            error != null -> error
            s == null -> getString(R.string.no_data)
            s.isStale() -> getString(R.string.stale, s.timestamp)
            else -> getString(R.string.updated, s.timestamp)
        }
        findViewById<TextView>(R.id.status).text = status
    }
}
