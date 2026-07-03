package com.buddybot.kids

import android.content.Context
import android.content.SharedPreferences
import android.util.Log
import androidx.security.crypto.EncryptedSharedPreferences
import androidx.security.crypto.MasterKey

/**
 * Persists WiFi passwords per SSID for robot provisioning.
 * Passwords are stored in EncryptedSharedPreferences; SSID list in plain prefs.
 */
object WiFiCredentialsStore {

    private const val TAG = "WiFiCredentialsStore"
    private const val PLAIN_PREFS = "buddybot_wifi"
    private const val SECURE_PREFS = "buddybot_wifi_secure"
    private const val KEY_LAST_SSID = "last_ssid"

    /** Mega S9 serial buffer drops lines longer than 80 chars (including CMD: prefix). */
    const val MEGA_CMD_MAX_LEN = 80
    private const val CMD_WIFI_OVERHEAD = "CMD:WIFI|".length + 1 // trailing | before password

    private fun plainPrefs(context: Context): SharedPreferences =
        context.applicationContext.getSharedPreferences(PLAIN_PREFS, Context.MODE_PRIVATE)

    private fun securePrefs(context: Context): SharedPreferences {
        return try {
            val masterKey = MasterKey.Builder(context.applicationContext)
                .setKeyScheme(MasterKey.KeyScheme.AES256_GCM)
                .build()
            EncryptedSharedPreferences.create(
                context.applicationContext,
                SECURE_PREFS,
                masterKey,
                EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV,
                EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM
            )
        } catch (e: Exception) {
            Log.e(TAG, "Encrypted prefs unavailable, using plain fallback", e)
            context.applicationContext.getSharedPreferences("${PLAIN_PREFS}_fallback", Context.MODE_PRIVATE)
        }
    }

    fun save(context: Context, ssid: String, password: String): Boolean {
        val trimmedSsid = ssid.trim()
        if (trimmedSsid.isEmpty() || password.isEmpty()) return false
        return try {
            securePrefs(context).edit()
                .putString(prefKey(trimmedSsid), password)
                .apply()
            plainPrefs(context).edit()
                .putString(KEY_LAST_SSID, trimmedSsid)
                .apply()
            true
        } catch (e: Exception) {
            Log.e(TAG, "save failed for $trimmedSsid", e)
            false
        }
    }

    fun loadPassword(context: Context, ssid: String): String? {
        val trimmedSsid = ssid.trim()
        if (trimmedSsid.isEmpty()) return null
        return try {
            securePrefs(context).getString(prefKey(trimmedSsid), null)?.takeIf { it.isNotEmpty() }
        } catch (e: Exception) {
            Log.e(TAG, "load failed for $trimmedSsid", e)
            null
        }
    }

    fun hasSaved(context: Context, ssid: String): Boolean =
        loadPassword(context, ssid) != null

    fun loadLastSsid(context: Context): String? =
        plainPrefs(context).getString(KEY_LAST_SSID, null)?.takeIf { it.isNotBlank() }

    fun clear(context: Context, ssid: String) {
        val trimmedSsid = ssid.trim()
        if (trimmedSsid.isEmpty()) return
        try {
            securePrefs(context).edit().remove(prefKey(trimmedSsid)).apply()
        } catch (e: Exception) {
            Log.e(TAG, "clear failed for $trimmedSsid", e)
        }
    }

    /** Returns null if credentials fit Mega buffer; otherwise an error message. */
    fun validateMegaCommandLength(ssid: String, password: String): String? {
        val total = "CMD:WIFI|$ssid|$password".length
        if (total <= MEGA_CMD_MAX_LEN) return null
        val maxPass = (MEGA_CMD_MAX_LEN - CMD_WIFI_OVERHEAD - ssid.length).coerceAtLeast(0)
        return "WiFi name + password too long for robot serial ($total/$MEGA_CMD_MAX_LEN chars). " +
            "Shorten password to $maxPass characters or fewer."
    }

    private fun prefKey(ssid: String): String = "wifi_${ssid.hashCode()}"
}