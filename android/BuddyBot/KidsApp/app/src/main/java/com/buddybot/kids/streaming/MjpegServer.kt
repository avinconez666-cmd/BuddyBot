package com.buddybot.kids.streaming

import android.util.Log
import fi.iki.elonen.NanoHTTPD
import java.io.PipedInputStream
import java.io.PipedOutputStream
import java.util.concurrent.locks.ReentrantLock
import kotlin.concurrent.withLock

/**
 * MjpegServer
 *
 * Lightweight NanoHTTPD-based HTTP server that streams JPEG frames as
 * multipart/x-mixed-replace (MJPEG). Every connected viewer receives
 * the same latest frame -- no per-client buffering.
 *
 * Endpoints:
 *   /         → simple HTML viewer page (open in any browser)
 *   /stream   → raw MJPEG stream (plug into <img> or parent app)
 *   /snapshot → single JPEG of the most recent frame
 *   /health   → plaintext "OK" for connection checks
 *
 * Usage:
 *   val server = MjpegServer(8554)
 *   server.start()                 // start once
 *   server.pushJpegFrame(bytes)    // call from camera callback
 *   server.stop()                  // cleanup on destroy
 */
class MjpegServer(port: Int = DEFAULT_PORT) : NanoHTTPD(port) {

    companion object {
        private const val TAG = "MjpegServer"
        const val DEFAULT_PORT = 8554
        private const val BOUNDARY = "BuddyBotStream"
        private const val PIPE_BUFFER_BYTES = 512 * 1024   // 512 KB pipe per client
        private const val FRAME_INTERVAL_MS = 33L          // ~30 fps cap
        private const val FRAME_WAIT_TIMEOUT_MS = 5_000L
    }

    // ── Frame store ──────────────────────────────────────────────────────────
    @Volatile private var latestJpeg: ByteArray? = null
    private val frameLock = ReentrantLock()
    private val frameReady = frameLock.newCondition()

    /**
     * Push a new JPEG frame from the camera callback.
     * Thread-safe, non-blocking. Wakes all waiting stream threads.
     */
    fun pushJpegFrame(jpeg: ByteArray) {
        frameLock.withLock {
            latestJpeg = jpeg
            frameReady.signalAll()
        }
    }

    // ── Routing ───────────────────────────────────────────────────────────────
    override fun serve(session: IHTTPSession): Response = when (session.uri) {
        "/stream"   -> serveStream()
        "/snapshot" -> serveSnapshot()
        "/health"   -> newFixedLengthResponse(Response.Status.OK, "text/plain", "OK")
        else        -> serveIndex()
    }

    // ── MJPEG stream ─────────────────────────────────────────────────────────
    private fun serveStream(): Response {
        val pipeOut = PipedOutputStream()
        val pipeIn  = PipedInputStream(pipeOut, PIPE_BUFFER_BYTES)

        Thread({
            try {
                while (!Thread.currentThread().isInterrupted) {
                    // Block until a new frame is available (or timeout)
                    val jpeg: ByteArray
                    frameLock.withLock {
                        if (latestJpeg == null) {
                            frameReady.await(FRAME_WAIT_TIMEOUT_MS, java.util.concurrent.TimeUnit.MILLISECONDS)
                        }
                        jpeg = latestJpeg ?: return@withLock also { return@Thread }
                    }

                    val header = "--$BOUNDARY\r\n" +
                        "Content-Type: image/jpeg\r\n" +
                        "Content-Length: ${jpeg.size}\r\n\r\n"

                    pipeOut.write(header.toByteArray(Charsets.US_ASCII))
                    pipeOut.write(jpeg)
                    pipeOut.write("\r\n".toByteArray())
                    pipeOut.flush()

                    Thread.sleep(FRAME_INTERVAL_MS)
                }
            } catch (e: InterruptedException) {
                Thread.currentThread().interrupt()
            } catch (e: Exception) {
                Log.d(TAG, "Stream client disconnected: ${e.message}")
            } finally {
                runCatching { pipeOut.close() }
            }
        }, "mjpeg-client-${System.currentTimeMillis()}").also {
            it.isDaemon = true
            it.start()
        }

        return newChunkedResponse(
            Response.Status.OK,
            "multipart/x-mixed-replace; boundary=$BOUNDARY",
            pipeIn
        ).also { r ->
            r.addHeader("Cache-Control", "no-cache, no-store, must-revalidate")
            r.addHeader("Pragma",        "no-cache")
            r.addHeader("Expires",       "0")
            r.addHeader("Connection",    "keep-alive")
            r.addHeader("Access-Control-Allow-Origin", "*")  // allow parent app cross-origin
        }
    }

    // ── Single JPEG snapshot ──────────────────────────────────────────────────
    private fun serveSnapshot(): Response {
        val jpeg = latestJpeg
            ?: return newFixedLengthResponse(
                Response.Status.SERVICE_UNAVAILABLE, "text/plain",
                "No frame yet -- camera may still be initialising"
            )
        return newFixedLengthResponse(
            Response.Status.OK, "image/jpeg",
            jpeg.inputStream(), jpeg.size.toLong()
        ).also { r ->
            r.addHeader("Cache-Control", "no-cache")
            r.addHeader("Access-Control-Allow-Origin", "*")
        }
    }

    // ── Simple viewer page ────────────────────────────────────────────────────
    private fun serveIndex(): Response {
        val html = """<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>BuddyBot Live</title>
  <style>
    *{box-sizing:border-box;margin:0;padding:0}
    body{background:#111;display:flex;flex-direction:column;align-items:center;justify-content:center;min-height:100vh;font-family:sans-serif;color:#eee}
    h1{font-size:1.1rem;margin-bottom:.75rem;opacity:.7}
    img{max-width:100%;max-height:90vh;border-radius:8px;box-shadow:0 4px 24px rgba(0,0,0,.6)}
  </style>
</head>
<body>
  <h1>BuddyBot Live View</h1>
  <img src="/stream" alt="BuddyBot camera feed">
</body>
</html>"""
        return newFixedLengthResponse(Response.Status.OK, "text/html", html)
    }
}