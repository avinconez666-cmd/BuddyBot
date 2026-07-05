package com.buddybot.kids

import android.content.Context
import android.content.SharedPreferences
import android.util.Log

/**
 * Persists WiFi passwords per SSID for robot provisioning.
 *
 * IMPORTANT: Previously used EncryptedSharedPreferences (security-crypto alpha06).
 * That alpha build throws android.os.ServiceSpecificException and
 * java.lang.IllegalStateException on Samsung S9 due to TEE keystore issues —
 * the crash happens inside MasterKey.Builder.build() and is NOT reliably
 * caught by `catch (e: Exception)` on all Samsung builds.
 *
 * Resolution: Use regular SharedPreferences. WiFi passwords stored here are
 * convenience data (already stored in the Android WiFi subsystem). The risk
 * of using plain storage is negligible for this use case.
 */
object WiFiCredentialsStore {

    private const val TAG       = "WiFiStore"
    private const val PREFS     = "buddybot_wifi"
    private const val KEY_LAST  = "last_ssid"

    const val MEGA_CMD_MAX_LEN  = 80
    private const val OVERHEAD  = "CMD:WIFI|".length + 1

    private fun prefs(context: Context): SharedPreferences =
        context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    fun save(context: Context, ssid: String, password: String): Boolean {
        val s = ssid.trim()
        if (s.isEmpty() || password.isEmpty()) return false
        return try {
            prefs(context).edit().putString(key(s), password).putString(KEY_LAST, s).apply()
            Log.d(TAG, "Saved credentials for '$s'")
            true
        } catch (e: Exception) {
            Log.e(TAG, "save failed for '$s'", e)
            false
        }
    }

    fun loadPassword(context: Context, ssid: String): String? {
        val s = ssid.trim(); if (s.isEmpty()) return null
        return try { prefs(context).getString(key(s), null)?.takeIf { it.isNotEmpty() } }
        catch (e: Exception) { Log.e(TAG, "load failed for '$s'", e); null }
    }

    fun hasSaved(context: Context, ssid: String): Boolean = loadPassword(context, ssid) != null

    fun loadLastSsid(context: Context): String? =
        try { prefs(context).getString(KEY_LAST, null)?.takeIf { it.isNotBlank() } }
        catch (e: Exception) { null }

    fun clear(context: Context, ssid: String) {
        val s = ssid.trim(); if (s.isEmpty()) return
        try { prefs(context).edit().remove(key(s)).apply() } catch (e: Exception) { Log.e(TAG, "clear failed", e) }
    }

    fun validateMegaCommandLength(ssid: String, password: String): String? {
        val total = "CMD:WIFI|$ssid|$password".length
        if (total <= MEGA_CMD_MAX_LEN) return null
        val maxPass = (MEGA_CMD_MAX_LEN - OVERHEAD - ssid.length).coerceAtLeast(0)
        return "WiFi name + password too long ($total/$MEGA_CMD_MAX_LEN). Shorten password to $maxPass chars."
    }

    private fun key(ssid: String): String = "wifi_${ssid.hashCode()}"
}