package com.buddybot.kids

import android.content.Context
import android.content.pm.PackageManager
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.net.wifi.WifiManager
import android.os.Build
import androidx.core.content.ContextCompat

object WiFiNetworkHelper {

    fun isOnWifi(context: Context): Boolean {
        val cm = context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        val network = cm.activeNetwork ?: return false
        val caps = cm.getNetworkCapabilities(network) ?: return false
        return caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)
    }

    fun hasLocationPermission(context: Context): Boolean =
        ContextCompat.checkSelfPermission(
            context, android.Manifest.permission.ACCESS_FINE_LOCATION
        ) == PackageManager.PERMISSION_GRANTED

    /**
     * Returns the SSID of the WiFi network the phone is currently connected to.
     * Requires [android.Manifest.permission.ACCESS_FINE_LOCATION] on Android 8+.
     */
    fun getConnectedSsid(context: Context): String? {
        if (!isOnWifi(context)) return null
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M && !hasLocationPermission(context)) {
            return null
        }

        return try {
            val wifiManager = context.applicationContext
                .getSystemService(Context.WIFI_SERVICE) as WifiManager

            @Suppress("DEPRECATION")
            val rawSsid = wifiManager.connectionInfo?.ssid ?: return null
            val ssid = rawSsid.trim().removeSurrounding("\"")
            if (ssid.isBlank() || ssid.equals("<unknown ssid>", ignoreCase = true)) null
            else ssid
        } catch (e: SecurityException) {
            // Location permission granted but system location toggle is off (common on Samsung)
            null
        } catch (e: Exception) {
            null
        }
    }
}