package com.buddybot.kids.streaming

import android.content.Context
import android.graphics.Bitmap
import android.graphics.ImageFormat
import android.graphics.Rect
import android.graphics.YuvImage
import android.util.Log
import com.jiangdg.ausbc.callback.IPreviewDataCallBack
import java.io.ByteArrayOutputStream
import java.net.Inet4Address
import java.net.NetworkInterface
import java.nio.ByteBuffer

/**
 * CameraStreamManager
 *
 * Singleton bridge between the libausbc camera preview and MjpegServer.
 *
 * Lifecycle:
 *   Call start() once when the app / camera initialises.
 *   Register streamCallback with your CameraClient instance.
 *   Call stop() in onDestroy().
 *
 * Integration (in whichever class holds your CameraClient):
 *
 *   // 1. Start the server (call once, e.g. in onCreate / after camera permission granted)
 *   CameraStreamManager.start()
 *
 *   // 2. Register the frame callback on your camera client
 *   cameraClient.addPreviewDataCallBack(CameraStreamManager.streamCallback)
 *
 *   // 3. Get the URL to display/share with the parent app
 *   val url = CameraStreamManager.streamUrl(context)
 *   //  → "http://192.168.x.x:8554/stream"
 *
 *   // 4. Stop the server in onDestroy
 *   CameraStreamManager.stop()
 */
object CameraStreamManager {

    private const val TAG  = "CameraStreamManager"
    const val STREAM_PORT  = 8554
    const val JPEG_QUALITY = 72  // 70-75 = good quality, ~15-25 KB per frame at 640x480

    private var server: MjpegServer? = null
    @Volatile private var running = false

    // ── IPreviewDataCallBack implementation ───────────────────────────────────
    /**
     * Register this with your CameraClient:
     *   cameraClient.addPreviewDataCallBack(CameraStreamManager.streamCallback)
     *
     * The callback is lightweight -- it compresses one JPEG per frame on a
     * dedicated thread and hands it to the server. NV21 is preferred over RGBA
     * because YuvImage.compressToJpeg() uses a native codec (faster, less GC).
     */
    val streamCallback = object : IPreviewDataCallBack {
        override fun onPreviewData(
            data: ByteArray?,
            width: Int,
            height: Int,
            format: IPreviewDataCallBack.DataFormat
        ) {
            if (data == null || !running) return
            try {
                val jpeg = when (format) {
                    IPreviewDataCallBack.DataFormat.NV21  -> nv21ToJpeg(data, width, height)
                    IPreviewDataCallBack.DataFormat.RGBA  -> rgbaToJpeg(data, width, height)
                }
                server?.pushJpegFrame(jpeg)
            } catch (e: Exception) {
                Log.w(TAG, "Frame encode error: ${e.message}")
            }
        }
    }

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    /**
     * Start the MJPEG HTTP server. Safe to call multiple times -- no-op if already running.
     * @return true if the server is now running.
     */
    fun start(): Boolean {
        if (running) return true
        return try {
            server = MjpegServer(STREAM_PORT).also { it.start(NanoHTTPD.SOCKET_READ_TIMEOUT, false) }
            running = true
            Log.i(TAG, "MJPEG server started -- stream at port $STREAM_PORT")
            true
        } catch (e: Exception) {
            Log.e(TAG, "Failed to start MJPEG server: ${e.message}")
            false
        }
    }

    fun stop() {
        running = false
        server?.stop()
        server = null
        Log.i(TAG, "MJPEG server stopped")
    }

    fun isRunning() = running

    // ── URL helpers ───────────────────────────────────────────────────────────
    /**
     * Returns the raw MJPEG stream URL for the parent app ImageView / VideoView.
     * Returns null if the device has no active WiFi address.
     */
    fun streamUrl(): String? = localIp()?.let { "http://$it:$STREAM_PORT/stream" }

    /**
     * Returns the browser-viewable URL (shows a simple HTML viewer page).
     */
    fun viewerUrl(): String? = localIp()?.let { "http://$it:$STREAM_PORT/" }

    /**
     * Returns the single-frame snapshot URL.
     */
    fun snapshotUrl(): String? = localIp()?.let { "http://$it:$STREAM_PORT/snapshot" }

    // ── Private helpers ───────────────────────────────────────────────────────
    private fun nv21ToJpeg(nv21: ByteArray, w: Int, h: Int): ByteArray {
        val yuv = YuvImage(nv21, ImageFormat.NV21, w, h, null)
        val out = ByteArrayOutputStream(w * h / 4)
        yuv.compressToJpeg(Rect(0, 0, w, h), JPEG_QUALITY, out)
        return out.toByteArray()
    }

    private fun rgbaToJpeg(rgba: ByteArray, w: Int, h: Int): ByteArray {
        val bmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
        bmp.copyPixelsFromBuffer(ByteBuffer.wrap(rgba))
        val out = ByteArrayOutputStream(w * h / 4)
        bmp.compress(Bitmap.CompressFormat.JPEG, JPEG_QUALITY, out)
        bmp.recycle()
        return out.toByteArray()
    }

    private fun localIp(): String? {
        return try {
            NetworkInterface.getNetworkInterfaces()
                ?.asSequence()
                ?.filter { it.isUp && !it.isLoopback }
                ?.flatMap { it.inetAddresses.asSequence() }
                ?.filterIsInstance<Inet4Address>()
                ?.firstOrNull { !it.isLoopbackAddress }
                ?.hostAddress
        } catch (e: Exception) {
            Log.w(TAG, "Could not determine local IP: ${e.message}")
            null
        }
    }
}

// NanoHTTPD import shim -- avoids using the companion object's constant from outside
private typealias NanoHTTPD = fi.iki.elonen.NanoHTTPD