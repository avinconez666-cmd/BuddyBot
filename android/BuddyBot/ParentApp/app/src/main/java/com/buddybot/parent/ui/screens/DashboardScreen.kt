package com.buddybot.parent.ui.screens

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.*
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.*
import androidx.compose.foundation.gestures.*
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.*
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.*
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.*
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.*
import com.buddybot.parent.data.BotEvent
import com.buddybot.parent.data.Telemetry
import com.buddybot.parent.ui.components.*
import com.buddybot.parent.ui.theme.*
import kotlinx.coroutines.flow.SharedFlow
import java.text.SimpleDateFormat
import java.util.*

// ══════════════════════════════════════════════════════════════════════════
//  DASHBOARD SCREEN
// ══════════════════════════════════════════════════════════════════════════
@Composable
fun DashboardScreen(
    telem: Telemetry,
    connected: Boolean,
    streamUrl: String,
    events: SharedFlow<BotEvent>,
    onCmd: (String) -> Unit,
    onSettings: () -> Unit,
) {
    var faceDetected  by remember { mutableStateOf(false) }
    var alerts        by remember { mutableStateOf(listOf<Pair<String, Color>>()) }
    var speed         by remember { mutableStateOf("NORMAL") }
    val time          = remember { mutableStateOf(Date()) }

    // Clock tick
    LaunchedEffect(Unit) {
        while (true) { time.value = Date(); kotlinx.coroutines.delay(1000) }
    }

    // Demo face detection pulse
    LaunchedEffect(Unit) {
        while (true) {
            kotlinx.coroutines.delay(4000)
            faceDetected = !faceDetected
        }
    }

    // Event listener → alerts
    LaunchedEffect(events) {
        events.collect { ev ->
            val (msg, col) = when (ev) {
                is BotEvent.FlameAlert -> ev.msg to Coral
                is BotEvent.GasAlert   -> ev.msg to Amber
                is BotEvent.BatLow     -> ev.msg to Coral
                is BotEvent.Connected  -> "Connected to ${ev.ip}" to Mint
                BotEvent.Disconnected  -> "Connection lost" to Coral
            }
            alerts = (listOf(msg to col) + alerts).take(4)
        }
    }

    Box(Modifier.fillMaxSize().background(BgDeep)) {

        // Subtle grid background
        Canvas(Modifier.fillMaxSize()) {
            val gridSize = 40f
            val gridCol = GrayDark.copy(0.12f)
            var x = 0f; while (x < size.width)  { drawLine(gridCol, Offset(x,0f), Offset(x,size.height), 0.5f); x += gridSize }
            var y = 0f; while (y < size.height) { drawLine(gridCol, Offset(0f,y), Offset(size.width,y), 0.5f); y += gridSize }
        }

        Column(Modifier.fillMaxSize()) {

            // ── TOP HUD BAR ──────────────────────────────────────────────
            TopHudBar(telem, connected, time.value, onSettings, onCmd, speed) { s ->
                speed = s; onCmd(s)
            }

            // ── MAIN CONTENT ─────────────────────────────────────────────
            Row(Modifier.fillMaxSize().padding(6.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {

                // LEFT: Camera feed
                Column(Modifier.weight(1f).fillMaxHeight(), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    CameraPanel(streamUrl, faceDetected, telem, Modifier.weight(1f).fillMaxWidth())
                    SensorPanel(telem, Modifier.fillMaxWidth())
                }

                // RIGHT: Radar + Controls
                Column(Modifier.width(240.dp).fillMaxHeight(), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    RadarPanel(telem, Modifier.fillMaxWidth())
                    ControlPanel(telem, onCmd, speed, { s -> speed = s; onCmd(s) }, Modifier.fillMaxWidth())
                    StatusPanel(telem, connected, Modifier.fillMaxWidth().weight(1f))
                }
            }
        }

        // Alert toasts
        Column(Modifier.align(Alignment.TopEnd).padding(top = 56.dp, end = 8.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp)) {
            alerts.forEachIndexed { i, (msg, col) ->
                AlertToast(msg, col) { alerts = alerts.filterIndexed { j, _ -> j != i } }
            }
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════
//  TOP HUD BAR
// ══════════════════════════════════════════════════════════════════════════
@Composable
fun TopHudBar(
    t: Telemetry, connected: Boolean, time: Date,
    onSettings: () -> Unit, onCmd: (String) -> Unit,
    speed: String, onSpeed: (String) -> Unit,
) {
    val timeFmt = remember { SimpleDateFormat("HH:mm:ss", Locale.getDefault()) }
    val dateFmt = remember { SimpleDateFormat("EEE d MMM", Locale.getDefault()) }

    Box(
        Modifier.fillMaxWidth()
            .background(Surface.copy(0.97f))
            .drawBehind {
                drawLine(Cyan.copy(0.5f), Offset(0f, size.height), Offset(size.width, size.height), 1.5f)
            }
            .padding(horizontal = 10.dp, vertical = 5.dp)
    ) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween) {

            // Left: logos
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                BuddyBotLogo()
                Box(Modifier.width(1.dp).height(32.dp).background(Border))
                ReinsmLogo()
            }

            // Centre: vitals
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(14.dp)) {
                BatteryBar(t.battery, t.voltage)
                VDivider()
                VitalChip("TEMP", "${String.format("%.1f", t.temp)}°C",
                    when { t.temp > 38 -> Coral; t.temp > 32 -> Amber; else -> Mint })
                VDivider()
                VitalChip("MODE", if (t.estop) "E-STOP" else t.mode,
                    if (t.estop) Coral else if (t.autoMode) Mint else Cyan)
                VDivider()
                // Status chips
                Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    StatusChip("R3",  t.r3ok)
                    StatusChip("ESP", connected)
                    StatusChip("CAM", false)
                }
                // Hazard badges
                if (t.flame) HazardBadge("🔥 FLAME", Coral)
                if (t.gas > 300) HazardBadge("☁ GAS", Amber)
                if (t.estop) HazardBadge("⛔ E-STOP", Coral)
            }

            // Right: clock + settings
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                Column(horizontalAlignment = Alignment.End) {
                    Text(timeFmt.format(time), fontSize = 14.sp, color = WhiteSoft,
                        fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold, letterSpacing = 2.sp)
                    Text(dateFmt.format(time), fontSize = 8.sp, color = GrayLight,
                        fontFamily = FontFamily.Monospace, letterSpacing = 1.sp)
                }
                IconButton(onClick = onSettings,
                    modifier = Modifier.size(32.dp).border(BorderStroke(1.dp, Border), RoundedCornerShape(4.dp))) {
                    Text("⚙", fontSize = 14.sp, color = GrayLight)
                }
            }
        }
    }
}

@Composable
private fun VDivider() = Box(Modifier.width(1.dp).height(28.dp).background(Border))

@Composable
private fun VitalChip(label: String, value: String, color: Color) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Text(label, fontSize = 7.sp, color = GrayLight, fontFamily = FontFamily.Monospace, letterSpacing = 2.sp)
        Text(value, fontSize = 11.sp, color = color, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold)
    }
}

@Composable
private fun HazardBadge(text: String, color: Color) {
    val pulse = rememberInfiniteTransition(label = "hz")
    val alpha by pulse.animateFloat(0.6f, 1f,
        infiniteRepeatable(tween(600), RepeatMode.Reverse), label = "a")
    Text(text, fontSize = 9.sp, color = color.copy(alpha),
        fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold,
        modifier = Modifier
            .background(color.copy(0.15f), RoundedCornerShape(3.dp))
            .border(BorderStroke(1.dp, color.copy(0.5f)), RoundedCornerShape(3.dp))
            .padding(horizontal = 6.dp, vertical = 2.dp))
}

// ══════════════════════════════════════════════════════════════════════════
//  PANELS
// ══════════════════════════════════════════════════════════════════════════

@Composable
fun CameraPanel(streamUrl: String, faceDetected: Boolean, t: Telemetry, modifier: Modifier) {
    HudCard(modifier, Cyan, "VISUAL FEED  ◉",
        if (faceDetected) "● FACE LOCK" else "○ SCANNING") {
        Box(Modifier.fillMaxSize()) {
            MjpegView(streamUrl, Modifier.fillMaxSize())
            TargetingOverlay(faceDetected, Modifier.fillMaxSize())
            // Top-left HUD text
            Column(Modifier.align(Alignment.TopStart).padding(8.dp)) {
                HudText("RES  1920×1080")
                HudText("FPS  30")
                HudText(if (faceDetected) "● FACE LOCK" else "○ SCANNING",
                    if (faceDetected) Mint else GrayLight)
            }
            // Bottom-right
            Column(Modifier.align(Alignment.BottomEnd).padding(8.dp),
                horizontalAlignment = Alignment.End) {
                HudText("AI VISION ACTIVE", Cyan)
                HudText(if (t.autoMode) "AUTO MODE ●" else "MANUAL MODE", if (t.autoMode) Mint else GrayLight)
            }
        }
    }
}

@Composable
fun SensorPanel(t: Telemetry, modifier: Modifier) {
    HudCard(modifier, Purple, "ENVIRONMENTAL SENSORS") {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(16.dp)) {
            Column(Modifier.weight(1f)) {
                val gasCol = when { t.gas > 400 -> Coral; t.gas > 200 -> Amber; else -> Mint }
                SensorBar("GAS LEVEL",   "${t.gas}",               t.gas / 800f.coerceAtLeast(1f), gasCol)
                SensorBar("HUMIDITY",    "${t.humidity.toInt()}%",  t.humidity / 100f,              Cyan)
                SensorBar("TEMPERATURE", "${String.format("%.1f",t.temp)}°C",
                    (t.temp / 50f).coerceIn(0f,1f),
                    when { t.temp > 38 -> Coral; t.temp > 32 -> Amber; else -> Mint })
            }
            Column(Modifier.weight(1f)) {
                val batCol = when { t.battery > 60 -> Mint; t.battery > 25 -> Amber; else -> Coral }
                SensorBar("BATTERY",  "${t.battery}%", t.battery / 100f, batCol)
                SensorBar("VOLTAGE",  "${String.format("%.2f",t.voltage)}V",
                    (t.voltage / 8.4f).coerceIn(0f,1f), batCol)
                Row(Modifier.fillMaxWidth().padding(vertical = 3.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    StatusPill("FLAME", t.flame, Coral)
                    StatusPill("TILT",  t.tilt,  Amber)
                    StatusPill("PIR",   t.pir,   Mint)
                    StatusPill("IR",    t.ir,    Cyan)
                }
            }
        }
    }
}

@Composable
fun RadarPanel(t: Telemetry, modifier: Modifier) {
    HudCard(modifier, Mint, "PROXIMITY RADAR", "100cm MAX") {
        RadarCanvas(t.front, t.rear, t.left, t.right,
            Modifier.fillMaxWidth().height(180.dp))
        Spacer(Modifier.height(4.dp))
        // Distance readouts
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            listOf("F" to t.front, "R" to t.rear, "L" to t.left, "Ri" to t.right).forEach { (lbl, d) ->
                val col = when { d in 1..24 -> Coral; d in 25..59 -> Amber; d >= 60 -> Mint; else -> GrayDark }
                Box(Modifier.weight(1f).background(col.copy(0.1f), RoundedCornerShape(3.dp))
                    .border(BorderStroke(0.5.dp, col.copy(0.3f)), RoundedCornerShape(3.dp))
                    .padding(vertical = 3.dp), contentAlignment = Alignment.Center) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Text(lbl,  fontSize = 7.sp,  color = GrayLight, fontFamily = FontFamily.Monospace)
                        Text(if (d > 0) "${d}cm" else "--", fontSize = 9.sp, color = col,
                            fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold)
                    }
                }
            }
        }
    }
}

@Composable
fun ControlPanel(t: Telemetry, onCmd: (String) -> Unit, speed: String,
                 onSpeed: (String) -> Unit, modifier: Modifier) {
    HudCard(modifier, Cyan, "CONTROL") {
        // D-pad
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            // Left column: L
            DPadBtn("◀", Amber, Modifier.weight(1f)) { onCmd("L") }
            // Centre column: F / STOP / B
            Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                DPadBtn("▲", Cyan, Modifier.fillMaxWidth()) { onCmd("F") }
                DPadBtn("■  STOP", Coral, Modifier.fillMaxWidth()) { onCmd("S") }
                DPadBtn("▼", Cyan, Modifier.fillMaxWidth()) { onCmd("B") }
            }
            // Right column: R
            DPadBtn("▶", Amber, Modifier.weight(1f)) { onCmd("R") }
        }
        Spacer(Modifier.height(6.dp))
        // Speed
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            listOf("SLOW" to CyanDim, "NORMAL" to Mint, "FAST" to Coral).forEach { (s, c) ->
                val sel = speed == s
                Box(Modifier.weight(1f)
                    .background(if (sel) c.copy(0.25f) else Surface, RoundedCornerShape(3.dp))
                    .border(BorderStroke(if (sel) 1.dp else 0.5.dp, if (sel) c else Border), RoundedCornerShape(3.dp))
                    .clickable { onSpeed(s) }.padding(vertical = 5.dp),
                    contentAlignment = Alignment.Center) {
                    Text(s, fontSize = 8.sp, color = if (sel) c else GrayLight,
                        fontFamily = FontFamily.Monospace, fontWeight = if (sel) FontWeight.Bold else FontWeight.Normal)
                }
            }
        }
        Spacer(Modifier.height(6.dp))
        // Command buttons
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            CmdButton("AUTO",  Mint,   { onCmd("AUTO") },   Modifier.weight(1f))
            CmdButton("DANCE", Magenta,{ onCmd("DANCE") },  Modifier.weight(1f))
        }
        Row(Modifier.fillMaxWidth().padding(top = 4.dp), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            CmdButton("PATROL", Amber,  { onCmd("MODE:BODYGUARD") }, Modifier.weight(1f))
            CmdButton("DOG",    Purple, { onCmd("MODE:DOG") },        Modifier.weight(1f))
        }
        Spacer(Modifier.height(6.dp))
        // E-STOP
        val esCol = if (t.estop) Amber else Coral
        val esTxt = if (t.estop) "⚡ CLEAR E-STOP" else "⛔ EMERGENCY STOP"
        Box(Modifier.fillMaxWidth()
            .background(esCol.copy(0.18f), RoundedCornerShape(4.dp))
            .border(BorderStroke(1.5.dp, esCol), RoundedCornerShape(4.dp))
            .clickable { onCmd(if (t.estop) "ESTOP_CLEAR" else "EMERGENCY_STOP") }
            .padding(vertical = 9.dp), contentAlignment = Alignment.Center) {
            Text(esTxt, fontSize = 10.sp, color = esCol, fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Black, letterSpacing = 2.sp)
        }
    }
}

@Composable
fun StatusPanel(t: Telemetry, connected: Boolean, modifier: Modifier) {
    HudCard(modifier, GrayLight, "SYSTEM STATUS") {
        Column(verticalArrangement = Arrangement.spacedBy(3.dp),
            modifier = Modifier.verticalScroll(rememberScrollState())) {
            StatusRow("MEGA FW",    t.fw,     Cyan)
            StatusRow("ESP LINK",   if (connected) "ACTIVE" else "OFFLINE", if (connected) Mint else Coral)
            StatusRow("R3 MOTORS",  if (t.r3ok) "LINKED" else "OFFLINE",   if (t.r3ok) Mint else Coral)
            StatusRow("AUTO MODE",  if (t.autoMode) "ACTIVE" else "OFF",    if (t.autoMode) Mint else GrayDark)
            StatusRow("CURRENT",    "${String.format("%.2f",t.amps)}A",     GrayLight)
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════
//  SMALL HELPERS
// ══════════════════════════════════════════════════════════════════════════

@Composable
private fun HudText(text: String, color: Color = GrayLight.copy(0.8f)) {
    Text(text, fontSize = 8.sp, color = color, fontFamily = FontFamily.Monospace)
}

@Composable
private fun StatusRow(label: String, value: String, valueColor: Color) {
    Row(Modifier.fillMaxWidth()
        .drawBehind { drawLine(Border, Offset(0f, size.height), Offset(size.width, size.height), 0.5f) }
        .padding(vertical = 3.dp),
        horizontalArrangement = Arrangement.SpaceBetween) {
        Text(label, fontSize = 8.sp, color = GrayLight, fontFamily = FontFamily.Monospace)
        Text(value, fontSize = 8.sp, color = valueColor, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold)
    }
}

@Composable
private fun StatusPill(label: String, active: Boolean, color: Color) {
    Text(label, fontSize = 7.sp, color = if (active) color else GrayDark,
        fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold,
        modifier = Modifier
            .background(if (active) color.copy(0.15f) else GrayDark.copy(0.1f), RoundedCornerShape(50))
            .border(BorderStroke(0.5.dp, if (active) color.copy(0.5f) else GrayDark.copy(0.3f)), RoundedCornerShape(50))
            .padding(horizontal = 5.dp, vertical = 2.dp))
}

@Composable
private fun DPadBtn(label: String, color: Color, modifier: Modifier, onClick: () -> Unit) {
    Box(modifier
        .height(38.dp)
        .background(color.copy(0.12f), RoundedCornerShape(4.dp))
        .border(BorderStroke(1.dp, color.copy(0.4f)), RoundedCornerShape(4.dp))
        .clickable(onClick = onClick),
        contentAlignment = Alignment.Center) {
        Text(label, fontSize = 11.sp, color = color, fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold, textAlign = TextAlign.Center)
    }
}

@Composable
private fun AlertToast(msg: String, color: Color, onDismiss: () -> Unit) {
    Row(
        Modifier.width(280.dp)
            .background(color.copy(0.18f), RoundedCornerShape(4.dp))
            .border(BorderStroke(1.dp, color.copy(0.6f)), RoundedCornerShape(4.dp))
            .clickable(onClick = onDismiss)
            .padding(horizontal = 10.dp, vertical = 6.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(msg, fontSize = 9.sp, color = color, fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold, modifier = Modifier.weight(1f))
        Text("✕", fontSize = 10.sp, color = color.copy(0.6f))
    }
}
