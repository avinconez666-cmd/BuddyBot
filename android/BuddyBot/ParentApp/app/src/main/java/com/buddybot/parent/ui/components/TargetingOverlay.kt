package com.buddybot.parent.ui.components

import androidx.compose.animation.core.*
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.*
import androidx.compose.ui.graphics.drawscope.Stroke
import com.buddybot.parent.ui.theme.*

@Composable
fun TargetingOverlay(faceDetected: Boolean, modifier: Modifier = Modifier) {
    val pulse = rememberInfiniteTransition(label = "tgt")

    val reticleScale by pulse.animateFloat(1f, 1.04f,
        infiniteRepeatable(tween(900), RepeatMode.Reverse), label = "rs")
    val glowAlpha by pulse.animateFloat(0.6f, 1f,
        infiniteRepeatable(tween(1200), RepeatMode.Reverse), label = "ga")
    val scanY by pulse.animateFloat(0f, 1f,
        infiniteRepeatable(tween(2400, easing = LinearEasing)), label = "scan")
    val rotDeg by pulse.animateFloat(0f, 360f,
        infiniteRepeatable(tween(8000, easing = LinearEasing)), label = "rot")

    val faceCol = if (faceDetected) Mint else Cyan

    Canvas(modifier = modifier.fillMaxSize()) {
        val cx = size.width / 2f
        val cy = size.height / 2f
        val baseR = minOf(size.width, size.height) * 0.22f * reticleScale

        // Scan line
        val scanLineY = scanY * size.height
        drawLine(faceCol.copy(0.12f), Offset(0f, scanLineY), Offset(size.width, scanLineY), 1.5f)

        // CRT scanlines
        var lineY = 0f
        while (lineY < size.height) {
            drawLine(Color.Black.copy(0.06f), Offset(0f, lineY), Offset(size.width, lineY), 2f)
            lineY += 4f
        }

        // Corner brackets
        val bSize = 28f
        val bTh = 2f
        // TL
        drawLine(faceCol.copy(0.9f), Offset(8f, 8f + bSize), Offset(8f, 8f), bTh)
        drawLine(faceCol.copy(0.9f), Offset(8f, 8f), Offset(8f + bSize, 8f), bTh)
        // TR
        drawLine(faceCol.copy(0.9f), Offset(size.width-8f, 8f+bSize), Offset(size.width-8f, 8f), bTh)
        drawLine(faceCol.copy(0.9f), Offset(size.width-8f, 8f), Offset(size.width-8f-bSize, 8f), bTh)
        // BL
        drawLine(faceCol.copy(0.9f), Offset(8f, size.height-8f-bSize), Offset(8f, size.height-8f), bTh)
        drawLine(faceCol.copy(0.9f), Offset(8f, size.height-8f), Offset(8f+bSize, size.height-8f), bTh)
        // BR
        drawLine(faceCol.copy(0.9f), Offset(size.width-8f, size.height-8f-bSize), Offset(size.width-8f, size.height-8f), bTh)
        drawLine(faceCol.copy(0.9f), Offset(size.width-8f, size.height-8f), Offset(size.width-8f-bSize, size.height-8f), bTh)

        // Outer rotating ring
        drawCircle(faceCol.copy(0.25f * glowAlpha), baseR * 1.6f, Offset(cx, cy), style = Stroke(1f,
            pathEffect = PathEffect.dashPathEffect(floatArrayOf(10f, 8f), rotDeg)))

        // Outer circle
        drawCircle(faceCol.copy(0.5f * glowAlpha), baseR, Offset(cx, cy), style = Stroke(1.2f))

        // Inner circle
        drawCircle(faceCol.copy(0.8f * glowAlpha), baseR * 0.6f, Offset(cx, cy), style = Stroke(1.5f))
        if (faceDetected) drawCircle(Mint.copy(0.15f), baseR * 0.6f, Offset(cx, cy))

        // Crosshairs
        val gapR = baseR * 0.15f
        drawLine(faceCol.copy(0.85f), Offset(cx, cy - baseR * 1.0f), Offset(cx, cy - gapR), 1.5f)
        drawLine(faceCol.copy(0.85f), Offset(cx, cy + gapR), Offset(cx, cy + baseR * 1.0f), 1.5f)
        drawLine(faceCol.copy(0.85f), Offset(cx - baseR * 1.0f, cy), Offset(cx - gapR, cy), 1.5f)
        drawLine(faceCol.copy(0.85f), Offset(cx + gapR, cy), Offset(cx + baseR * 1.0f, cy), 1.5f)

        // Tick marks
        for (i in 0..11) {
            val a = Math.toRadians(i * 30.0)
            val r1 = baseR * 0.92f; val r2 = baseR
            val x1 = cx + kotlin.math.cos(a).toFloat() * r1
            val y1 = cy + kotlin.math.sin(a).toFloat() * r1
            val x2 = cx + kotlin.math.cos(a).toFloat() * r2
            val y2 = cy + kotlin.math.sin(a).toFloat() * r2
            drawLine(faceCol.copy(0.4f), Offset(x1, y1), Offset(x2, y2), 1f)
        }

        // Centre dot
        drawCircle(faceCol.copy(glowAlpha), 4f, Offset(cx, cy))
        drawCircle(faceCol.copy(0.3f * glowAlpha), 10f, Offset(cx, cy))

        // Vignette
        drawRect(Brush.radialGradient(
            listOf(Color.Transparent, Color.Black.copy(0.55f)),
            center = Offset(cx, cy), radius = maxOf(size.width, size.height) * 0.6f))
    }
}
