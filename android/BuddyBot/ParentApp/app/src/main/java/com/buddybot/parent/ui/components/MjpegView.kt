package com.buddybot.parent.ui.components

import android.graphics.BitmapFactory
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.*
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.sp
import com.buddybot.parent.ui.theme.*
import kotlinx.coroutines.*
import okhttp3.*
import java.io.ByteArrayOutputStream
import java.io.InputStream
import java.util.concurrent.TimeUnit

@Composable
fun MjpegView(streamUrl: String, modifier: Modifier = Modifier) {
    var frame by remember { mutableStateOf<androidx.compose.ui.graphics.ImageBitmap?>(null) }
    var error by remember { mutableStateOf("") }

    DisposableEffect(streamUrl) {
        if (streamUrl.isBlank()) { error = "No stream URL configured"; return@DisposableEffect onDispose {} }
        val job = CoroutineScope(Dispatchers.IO).launch {
            val client = OkHttpClient.Builder()
                .readTimeout(10, TimeUnit.SECONDS)
                .connectTimeout(5, TimeUnit.SECONDS)
                .build()
            while (isActive) {
                try {
                    val req = Request.Builder().url(streamUrl).build()
                    val resp = client.newCall(req).execute()
                    val stream = resp.body?.byteStream() ?: throw Exception("No stream")
                    error = ""
                    parseMjpeg(stream) { jpegBytes ->
                        val bmp = BitmapFactory.decodeByteArray(jpegBytes, 0, jpegBytes.size)
                        if (bmp != null) frame = bmp.asImageBitmap()
                    }
                    stream.close()
                } catch (e: Exception) {
                    error = "Stream unavailable"
                    delay(3000)
                }
            }
        }
        onDispose { job.cancel() }
    }

    Box(modifier = modifier.background(Color(0xFF010508))) {
        if (frame != null) {
            Image(bitmap = frame!!, contentDescription = "Camera",
                modifier = Modifier.fillMaxSize(), contentScale = ContentScale.Crop)
        } else {
            Column(Modifier.align(Alignment.Center), horizontalAlignment = Alignment.CenterHorizontally) {
                Text("◎", fontSize = 32.sp, color = GrayDark)
                Spacer(Modifier.height(8.dp))
                Text(if (error.isNotEmpty()) error else "Connecting to stream...",
                    fontSize = 10.sp, color = GrayLight, fontFamily = FontFamily.Monospace)
                if (streamUrl.isBlank())
                    Text("Configure URL in Settings ⚙", fontSize = 8.sp, color = GrayDark,
                        fontFamily = FontFamily.Monospace)
            }
        }
    }
}

private suspend fun parseMjpeg(stream: InputStream, onFrame: (ByteArray) -> Unit) {
    val buf = ByteArrayOutputStream()
    val window = ByteArray(2)
    var b: Int
    // Scan for JPEG SOI markers (0xFF 0xD8) and EOI (0xFF 0xD9)
    while (true) {
        b = stream.read()
        if (b == -1) break
        window[0] = window[1]
        window[1] = b.toByte()
        buf.write(b)
        if (window[0] == 0xFF.toByte() && window[1] == 0xD8.toByte()) {
            buf.reset(); buf.write(0xFF); buf.write(0xD8)
        }
        if (window[0] == 0xFF.toByte() && window[1] == 0xD9.toByte() && buf.size() > 4) {
            onFrame(buf.toByteArray())
            buf.reset()
            yield()
        }
    }
}
