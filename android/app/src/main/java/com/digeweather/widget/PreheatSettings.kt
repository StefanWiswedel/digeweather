package com.digeweather.widget

import android.content.Context
import android.util.Base64
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL

/**
 * Morning pre-heat settings, stored in the repo as config/precondition.json so the
 * GitHub job can read them. Reading is public; saving needs a GitHub token with
 * write access to this repo's contents, kept only on this phone.
 */
data class PreheatSettings(val enabled: Boolean, val frostTemp: Double) {

    fun toJson(): JSONObject = JSONObject().put("enabled", enabled).put("frostTemp", frostTemp)

    companion object {
        private const val API =
            "https://api.github.com/repos/StefanWiswedel/digeweather/contents/config/precondition.json"
        private const val PREFS = "preheat"

        fun parse(j: JSONObject) = PreheatSettings(
            enabled = j.optBoolean("enabled", true),
            frostTemp = j.optDouble("frostTemp", 5.0),
        )

        /** Blocking. */
        fun fetch(): PreheatSettings = parse(fetchJson("precondition.json", "config/"))

        fun token(ctx: Context): String =
            ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString("token", "") ?: ""

        fun saveToken(ctx: Context, token: String) {
            ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                .putString("token", token.trim()).apply()
        }

        /** Writes the settings to the repo. Blocking. Throws with a readable message on failure. */
        fun save(token: String, s: PreheatSettings) {
            require(token.isNotBlank()) { "Add a GitHub token first." }
            var lastError = ""
            repeat(3) {
                val sha = getSha(token)
                val body = JSONObject()
                    .put("message", "Update pre-heat settings from phone")
                    .put("content", Base64.encodeToString((s.toJson().toString() + "\n").toByteArray(), Base64.NO_WRAP))
                if (sha != null) body.put("sha", sha)
                val conn = open(token, "PUT")
                conn.doOutput = true
                conn.outputStream.use { it.write(body.toString().toByteArray()) }
                val code = conn.responseCode
                conn.disconnect()
                when (code) {
                    200, 201 -> return
                    401, 403 -> throw RuntimeException("GitHub refused the token (HTTP $code). Check it has Contents: write for digeweather.")
                    409 -> lastError = "conflict" // the weather bot committed at the same moment; retry
                    else -> lastError = "HTTP $code"
                }
            }
            throw RuntimeException("Could not save settings ($lastError).")
        }

        private fun getSha(token: String): String? {
            val conn = open(token, "GET")
            try {
                if (conn.responseCode == 404) return null
                if (conn.responseCode != 200) throw RuntimeException("GitHub HTTP ${conn.responseCode}")
                return JSONObject(conn.inputStream.bufferedReader().use { it.readText() }).optString("sha")
            } finally {
                conn.disconnect()
            }
        }

        private fun open(token: String, method: String): HttpURLConnection =
            (URL(API).openConnection() as HttpURLConnection).apply {
                requestMethod = method
                connectTimeout = 10000
                readTimeout = 10000
                setRequestProperty("Authorization", "Bearer $token")
                setRequestProperty("Accept", "application/vnd.github+json")
                setRequestProperty("User-Agent", "DigeWeatherWidget/1.0")
                if (method == "PUT") setRequestProperty("Content-Type", "application/json")
            }
    }
}
