package com.buddybot.parent.ui.components

import androidx.compose.animation.core.*
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.*
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.*
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.*
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.*
import com.buddybot.parent.ui.theme.*

// ── BUDDYBOT Logo ──────────────────────────────────────────────────────────
@Composable
fun BuddyBotLogo(modifier: Modifier = Modifier) {
    val pulse = rememberInfiniteTransition(label = "ant")
    val antAlpha by pulse.animateFloat(1f, 0.3f,
        infiniteRepeatable(tween(1800), RepeatMode.Reverse), label = "a")

    Row(modifier, verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(10.dp)) {

        // Robot head icon
        Canvas(Modifier.size(40.dp)) {
            val w = size.width; val h = size.height
            // Head
            drawRoundRect(Card, size = androidx.compose.ui.geometry.Size(w * 0.88f, h * 0.58f),
                topLeft = Offset(w * 0.06f, h * 0.16f),
                cornerRadius = androidx.compose.ui.geometry.CornerRadius(6f),
                style = Stroke(1.5f))
            drawRoundRect(Cyan.copy(0.15f), size = androidx.compose.ui.geometry.Size(w * 0.88f, h * 0.58f),
                topLeft = Offset(w * 0.06f, h * 0.16f),
                cornerRadius = androidx.compose.ui.geometry.CornerRadius(6f))
            // Eyes
            val eyeR = w * 0.1f
            drawCircle(Cyan.copy(0.9f), eyeR, Offset(w * 0.33f, h * 0.38f))
            drawCircle(Cyan.copy(0.9f), eyeR, Offset(w * 0.67f, h * 0.38f))
            drawCircle(Color.White, eyeR * 0.4f, Offset(w * 0.35f, h * 0.35f))
            drawCircle(Color.White, eyeR * 0.4f, Offset(w * 0.69f, h * 0.35f))
            // Mouth
            val path = Path().apply {
                moveTo(w * 0.30f, h * 0.58f)
                cubicTo(w * 0.38f, h * 0.68f, w * 0.62f, h * 0.68f, w * 0.70f, h * 0.58f)
            }
            drawPath(path, Cyan.copy(0.85f), style = Stroke(1.5f))
            // Antenna
            drawLine(Cyan, Offset(w * 0.5f, h * 0.16f), Offset(w * 0.5f, h * 0.06f), 1.5f)
            drawCircle(Cyan.copy(antAlpha), 4f, Offset(w * 0.5f, h * 0.04f))
            // Ears
            drawRoundRect(Cyan.copy(0.6f),
                size = androidx.compose.ui.geometry.Size(4f, h * 0.22f),
                topLeft = Offset(w * 0.03f, h * 0.30f),
                cornerRadius = androidx.compose.ui.geometry.CornerRadius(2f),
                style = Stroke(1f))
            drawRoundRect(Cyan.copy(0.6f),
                size = androidx.compose.ui.geometry.Size(4f, h * 0.22f),
                topLeft = Offset(w * 0.93f, h * 0.30f),
                cornerRadius = androidx.compose.ui.geometry.CornerRadius(2f),
                style = Stroke(1f))
        }

        // Text
        Column(verticalArrangement = Arrangement.Center) {
            Text("BUDDYBOT", fontSize = 18.sp,
                fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Black,
                color = Cyan, letterSpacing = 2.sp,
                style = androidx.compose.ui.text.TextStyle(
                    brush = Brush.horizontalGradient(listOf(Cyan, Mint))
                ))
            Text("GUARDIAN SYSTEM", fontSize = 7.sp, color = GrayLight,
                fontFamily = FontFamily.Monospace, letterSpacing = 4.sp)
        }
    }
}

// ── REINSMA INNOVATIONS Logo ───────────────────────────────────────────────
@Composable
fun ReinsmLogo(modifier: Modifier = Modifier) {
    Row(modifier, verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp)) {

        // Hexagon emblem
        Canvas(Modifier.size(32.dp)) {
            val cx = size.width / 2f; val cy = size.height / 2f; val r = 13f
            val hex = Path()
            for (i in 0..5) {
                val a = Math.toRadians((60.0 * i - 30.0))
                val x = cx + r * kotlin.math.cos(a).toFloat()
                val y = cy + r * kotlin.math.sin(a).toFloat()
                if (i == 0) hex.moveTo(x, y) else hex.lineTo(x, y)
            }
            hex.close()
            drawPath(hex, Brush.linearGradient(listOf(Cyan, Purple, Magenta)),
                style = Stroke(1.5f))
            drawPath(hex, Card.copy(0.8f))
            // Inner hex
            val hex2 = Path()
            for (i in 0..5) {
                val a = Math.toRadians((60.0 * i - 30.0))
                val x = cx + (r * 0.6f) * kotlin.math.cos(a).toFloat()
                val y = cy + (r * 0.6f) * kotlin.math.sin(a).toFloat()
                if (i == 0) hex2.moveTo(x, y) else hex2.lineTo(x, y)
            }
            hex2.close()
            drawPath(hex2, Cyan.copy(0.15f))
        }

        Column(verticalArrangement = Arrangement.Center) {
            Text("REINSMA INNOVATIONS", fontSize = 9.sp,
                fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold,
                style = androidx.compose.ui.text.TextStyle(
                    brush = Brush.horizontalGradient(listOf(Cyan, Purple, Magenta))
                ), letterSpacing = 1.sp)
            Text("GENERATIVE  AI", fontSize = 6.sp, color = GrayDark,
                fontFamily = FontFamily.Monospace, letterSpacing = 4.sp)
        }
    }
}
