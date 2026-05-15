package com.buddybot.parent.ui.components

import androidx.compose.animation.core.*
import androidx.compose.foundation.*
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.*
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.*
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.*
import com.buddybot.parent.ui.theme.*

// ── Reusable card with HUD corner brackets ─────────────────────────────────
@Composable
fun HudCard(
    modifier: Modifier = Modifier,
    accent: Color = Cyan,
    title: String? = null,
    titleRight: String? = null,
    content: @Composable ColumnScope.() -> Unit,
) {
    Box(modifier = modifier) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .background(Card.copy(alpha = 0.92f), RoundedCornerShape(4.dp))
                .border(BorderStroke(1.dp, Border), RoundedCornerShape(4.dp))
                .drawBehind {
                    // Top accent line
                    drawLine(accent.copy(alpha = 0.7f), Offset(0f, 0f), Offset(size.width, 0f), strokeWidth = 1.5f)
                }
                .padding(horizontal = 8.dp, vertical = 6.dp)
        ) {
            if (title != null) {
                Row(
                    modifier = Modifier.fillMaxWidth().padding(bottom = 4.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(title, fontSize = 7.sp, color = accent, fontFamily = FontFamily.Monospace,
                        fontWeight = FontWeight.Bold, letterSpacing = 2.sp)
                    if (titleRight != null)
                        Text(titleRight, fontSize = 7.sp, color = GrayLight, fontFamily = FontFamily.Monospace)
                }
                Canvas(modifier = Modifier.fillMaxWidth().height(0.5.dp)) {
                    drawLine(Border, Offset(0f, 0f), Offset(size.width, 0f), strokeWidth = 1f)
                }
                Spacer(Modifier.height(4.dp))
            }
            content()
        }
        // Corner brackets
        HudCorners(accent)
    }
}

@Composable
fun BoxScope.HudCorners(color: Color, size: Dp = 10.dp, thick: Dp = 1.5.dp) {
    val mod = Modifier.size(size)
    val s = thick
    Canvas(mod.align(Alignment.TopStart)) {
        drawLine(color, Offset(0f,this.size.height), Offset(0f,0f), s.toPx())
        drawLine(color, Offset(0f,0f), Offset(this.size.width,0f), s.toPx())
    }
    Canvas(mod.align(Alignment.TopEnd)) {
        drawLine(color, Offset(this.size.width,0f), Offset(0f,0f), s.toPx())
        drawLine(color, Offset(this.size.width,0f), Offset(this.size.width,this.size.height), s.toPx())
    }
    Canvas(mod.align(Alignment.BottomStart)) {
        drawLine(color, Offset(0f,0f), Offset(0f,this.size.height), s.toPx())
        drawLine(color, Offset(0f,this.size.height), Offset(this.size.width,this.size.height), s.toPx())
    }
    Canvas(mod.align(Alignment.BottomEnd)) {
        drawLine(color, Offset(this.size.width,0f), Offset(this.size.width,this.size.height), s.toPx())
        drawLine(color, Offset(0f,this.size.height), Offset(this.size.width,this.size.height), s.toPx())
    }
}

// ── Battery bar ────────────────────────────────────────────────────────────
@Composable
fun BatteryBar(pct: Int, volts: Float) {
    val col = when {
        pct > 60 -> Mint
        pct > 25 -> Amber
        else     -> Coral
    }
    val anim by animateFloatAsState(pct / 100f, tween(500), label = "bat")
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        Text("BATTERY", fontSize = 7.sp, color = GrayLight, fontFamily = FontFamily.Monospace, letterSpacing = 2.sp)
        Canvas(modifier = Modifier.width(48.dp).height(16.dp)) {
            // Outer shell
            drawRoundRect(col, style = Stroke(1.5f), cornerRadius = CornerRadius(3f))
            // Cap
            drawRect(col, Offset(size.width, size.height*0.3f), Size(4f, size.height*0.4f))
            // Fill
            drawRoundRect(col.copy(alpha = 0.8f), size = Size(anim * (size.width - 4f), size.height),
                cornerRadius = CornerRadius(2f))
        }
        Text("${pct}%", fontSize = 11.sp, color = col, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold)
        Text("${String.format("%.1f", volts)}V", fontSize = 9.sp, color = GrayLight, fontFamily = FontFamily.Monospace)
    }
}

// ── Status chip ────────────────────────────────────────────────────────────
@Composable
fun StatusChip(label: String, ok: Boolean) {
    val col = if (ok) Mint else Coral
    val pulse = rememberInfiniteTransition(label = "pulse")
    val alpha by pulse.animateFloat(0.5f, 1f, infiniteRepeatable(tween(900), RepeatMode.Reverse), label = "a")
    Row(
        modifier = Modifier
            .background(col.copy(alpha = 0.12f), RoundedCornerShape(50))
            .border(BorderStroke(0.5.dp, col.copy(alpha = 0.4f)), RoundedCornerShape(50))
            .padding(horizontal = 6.dp, vertical = 2.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(4.dp)
    ) {
        Canvas(Modifier.size(5.dp)) {
            drawCircle(col.copy(alpha = if (ok) alpha else 1f))
        }
        Text(label, fontSize = 8.sp, color = col, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
    }
}

// ── Sensor value row ───────────────────────────────────────────────────────
@Composable
fun SensorBar(label: String, value: String, barFraction: Float, color: Color) {
    val anim by animateFloatAsState(barFraction.coerceIn(0f, 1f), tween(400), label = "bar")
    Row(Modifier.fillMaxWidth().padding(vertical = 2.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label, fontSize = 8.sp, color = GrayLight, fontFamily = FontFamily.Monospace, modifier = Modifier.width(76.dp))
        Canvas(modifier = Modifier.weight(1f).height(5.dp)) {
            drawRoundRect(GrayDark, cornerRadius = CornerRadius(3f))
            if (anim > 0f) drawRoundRect(color, size = Size(anim * size.width, size.height),
                cornerRadius = CornerRadius(3f))
        }
        Spacer(Modifier.width(6.dp))
        Text(value, fontSize = 9.sp, color = color, fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold, modifier = Modifier.width(56.dp), textAlign = androidx.compose.ui.text.style.TextAlign.End)
    }
}

// ── Glowing command button ─────────────────────────────────────────────────
@Composable
fun CmdButton(label: String, color: Color, onClick: () -> Unit, modifier: Modifier = Modifier) {
    var pressed by remember { mutableStateOf(false) }
    Box(
        modifier = modifier
            .clip(RoundedCornerShape(4.dp))
            .background(if (pressed) color.copy(0.3f) else color.copy(0.12f))
            .border(BorderStroke(1.dp, if (pressed) color else color.copy(0.4f)), RoundedCornerShape(4.dp))
            .clickable { pressed = !pressed; onClick() }
            .padding(vertical = 7.dp),
        contentAlignment = Alignment.Center
    ) {
        Text(label, fontSize = 9.sp, color = color, fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
    }
}
