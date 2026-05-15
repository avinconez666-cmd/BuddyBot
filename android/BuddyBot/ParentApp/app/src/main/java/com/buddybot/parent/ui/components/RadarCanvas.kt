package com.buddybot.parent.ui.components

import androidx.compose.animation.core.*
import androidx.compose.foundation.Canvas
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.*
import androidx.compose.ui.graphics.drawscope.Stroke
import com.buddybot.parent.ui.theme.*
import kotlin.math.*

@Composable
fun RadarCanvas(
    front: Int, rear: Int, left: Int, right: Int,
    modifier: Modifier = Modifier
) {
    // Rotating sweep
    val sweep = rememberInfiniteTransition(label = "sweep")
    val sweepAngle by sweep.animateFloat(
        0f, 360f,
        infiniteRepeatable(tween(3000, easing = LinearEasing)),
        label = "sweepA"
    )

    Canvas(modifier = modifier) {
        val cx = size.width / 2f
        val cy = size.height / 2f
        val maxR = minOf(cx, cy) - 18f

        // Background glow
        drawCircle(
            Brush.radialGradient(listOf(Cyan.copy(0.05f), Color.Transparent),
                center = Offset(cx, cy), radius = maxR + 8f),
            radius = maxR + 8f, center = Offset(cx, cy)
        )

        // Range rings
        val ringCols = listOf(Coral.copy(0.5f), Amber.copy(0.4f), GrayDark.copy(0.6f), GrayDark.copy(0.4f))
        for (i in 1..4) {
            drawCircle(
                color = ringCols[i - 1],
                radius = maxR * i / 4f,
                center = Offset(cx, cy),
                style = Stroke(if (i == 4) 1.2f else 0.8f, pathEffect =
                    if (i < 4) PathEffect.dashPathEffect(floatArrayOf(4f, 5f)) else null)
            )
        }

        // Cross hairs
        drawLine(GrayDark, Offset(cx - maxR, cy), Offset(cx + maxR, cy), 0.8f)
        drawLine(GrayDark, Offset(cx, cy - maxR), Offset(cx, cy + maxR), 0.8f)

        // Sweep line + trailing glow
        val sweepRad = Math.toRadians((sweepAngle - 90).toDouble())
        val sx = cx + cos(sweepRad).toFloat() * maxR
        val sy = cy + sin(sweepRad).toFloat() * maxR
        drawLine(Mint.copy(0.8f), Offset(cx, cy), Offset(sx, sy), 2f)

        // Sweep trail (last 60 degrees)
        val trailPath = Path()
        val steps = 20
        for (k in steps downTo 0) {
            val a = Math.toRadians((sweepAngle - 90 - k * 3.0))
            val px = cx + cos(a).toFloat() * maxR
            val py = cy + sin(a).toFloat() * maxR
            val alpha = k.toFloat() / steps * 0.25f
            drawLine(Mint.copy(alpha), Offset(cx, cy), Offset(px, py), 1f)
        }

        // Plot sensor dots
        fun plotSensor(d: Int, angleDeg: Float, label: String) {
            if (d <= 0) return
            val frac = (1f - (d.coerceAtMost(100) / 100f)).coerceIn(0f, 1f)
            val rad = Math.toRadians((angleDeg - 90).toDouble())
            val r = frac * maxR
            val px = cx + cos(rad).toFloat() * r
            val py = cy + sin(rad).toFloat() * r
            val col = when {
                d < 25 -> Coral
                d < 60 -> Amber
                else   -> Mint
            }
            // Glow ring
            drawCircle(col.copy(0.2f), 12f, Offset(px, py))
            // Dot
            drawCircle(col, 5f, Offset(px, py))
            // Pulse
            val pulse = (sweepAngle % 60f) / 60f
            drawCircle(col.copy((1f - pulse) * 0.6f),
                5f + pulse * 12f, Offset(px, py), style = Stroke(1f))
        }

        plotSensor(front, 0f,   "F")
        plotSensor(rear,  180f, "R")
        plotSensor(left,  270f, "L")
        plotSensor(right, 90f,  "Ri")

        // Centre dot
        drawCircle(Card, 8f, Offset(cx, cy))
        drawCircle(Cyan, 8f, Offset(cx, cy), style = Stroke(1.5f))
        drawCircle(Cyan.copy(0.9f), 3f, Offset(cx, cy))
    }
}
