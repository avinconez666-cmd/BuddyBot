package com.buddybot.kids

// ═══════════════════════════════════════════════════════════════════════════════
// SettingsScreen.kt  –  PHASE 2: Futuristic Settings Menu
// Dark neon theme · Glassmorphic cards · Compose animations · Glow effects
// ═══════════════════════════════════════════════════════════════════════════════

import androidx.compose.animation.*
import androidx.compose.animation.core.*
import androidx.compose.foundation.*
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.blur
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.draw.scale
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.*
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.lifecycleScope
import com.jiangdg.ausbc.CameraClient
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

// ── Neon colour palette ───────────────────────────────────────────────────────
private val NeonCyan    = Color(0xFF00F5FF)
private val NeonGreen   = Color(0xFF39FF14)
private val NeonPurple  = Color(0xFFBF00FF)
private val NeonOrange  = Color(0xFFFF6B00)
private val NeonRed     = Color(0xFFFF0040)
private val NeonYellow  = Color(0xFFFFE600)
private val DarkBg      = Color(0xFF050A14)
private val DarkCard    = Color(0xFF0D1B2A)
private val DarkCardAlt = Color(0xFF0A1628)
private val GlassWhite  = Color(0x14FFFFFF)
private val GlassBorder = Color(0x33FFFFFF)

// ── Glow modifier helper ──────────────────────────────────────────────────────
private fun Modifier.neonGlow(color: Color, radius: Float = 24f): Modifier = this.drawBehind {
    drawCircle(
        brush = Brush.radialGradient(
            colors = listOf(color.copy(alpha = 0.35f), Color.Transparent),
            radius = radius
        ),
        radius = radius
    )
}

// ═══════════════════════════════════════════════════════════════════════════════
// ROOT COMPOSABLE
// ═══════════════════════════════════════════════════════════════════════════════
@Composable
fun SettingsMenu(
    robotState: RobotState,
    telemetry: TelemetryData,
    logs: List<String>,
    onClose: () -> Unit,
    onModeChange: (RobotMode) -> Unit,
    onMotorCommand: (String) -> Unit,
    onIPChange: (String) -> Unit,
    onToggleCommunication: () -> Unit,
    webcamClient: CameraClient? = null,
    // Phase 2 additions – test callbacks wired in MainActivity
    onTestSerial: (() -> Unit)? = null,
    onTestWebSocket: (() -> Unit)? = null
) {
    // Slide-in animation
    AnimatedVisibility(
        visible = true,
        enter = slideInHorizontally(
            initialOffsetX = { it },
            animationSpec = tween(400, easing = FastOutSlowInEasing)
        ) + fadeIn(tween(300))
    ) {
        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(DarkBg)
        ) {
            // Animated background grid lines
            NeonGridBackground()

            // Main content
            Column(modifier = Modifier.fillMaxSize()) {
                FuturisticHeader(onClose = onClose)

                LazyColumn(
                    modifier = Modifier
                        .fillMaxWidth()
                        .weight(1f)
                        .padding(horizontal = 16.dp),
                    verticalArrangement = Arrangement.spacedBy(12.dp),
                    contentPadding = PaddingValues(bottom = 24.dp)
                ) {
                    // ── STATUS CARD ──────────────────────────────────────────
                    item { Spacer(Modifier.height(8.dp)) }
                    item {
                        StatusCard(
                            robotState = robotState,
                            telemetry = telemetry,
                            onTestSerial = onTestSerial,
                            onTestWebSocket = onTestWebSocket,
                            // Phase 3: pass live mic state for glowing indicator
                            isListening = robotState.isListening,
                            isSpeaking = robotState.isSpeaking
                        )
                    }

                    // ── CONNECTION CARD ──────────────────────────────────────
                    item {
                        ConnectionCard(
                            robotState = robotState,
                            onToggleCommunication = onToggleCommunication,
                            onIPChange = onIPChange
                        )
                    }

                    // ── AI SETTINGS CARD ─────────────────────────────────────
                    item {
                        AISettingsCard(robotState = robotState)
                    }

                    // ── ROBOT MODES CARD ─────────────────────────────────────
                    item {
                        RobotModesCard(
                            currentMode = robotState.currentMode,
                            onModeChange = onModeChange
                        )
                    }

                    // ── MOTOR CONTROLS CARD ──────────────────────────────────
                    item {
                        MotorControlsCard(onMotorCommand = onMotorCommand)
                    }

                    // ── CAMERA CARD ──────────────────────────────────────────
                    item {
                        CameraCard(webcamClient = webcamClient)
                    }

                    // ── TELEMETRY CARD ───────────────────────────────────────
                    item {
                        TelemetryCard(telemetry = telemetry)
                    }

                    // ── LOGS CARD ────────────────────────────────────────────
                    item {
                        LogsCard(logs = logs)
                    }

                    item { Spacer(Modifier.height(16.dp)) }
                }
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// ANIMATED BACKGROUND
// ═══════════════════════════════════════════════════════════════════════════════
@Composable
private fun NeonGridBackground() {
    val infiniteTransition = rememberInfiniteTransition(label = "grid")
    val offset by infiniteTransition.animateFloat(
        initialValue = 0f,
        targetValue = 40f,
        animationSpec = infiniteRepeatable(
            animation = tween(4000, easing = LinearEasing),
            repeatMode = RepeatMode.Restart
        ),
        label = "gridOffset"
    )
    Canvas(modifier = Modifier.fillMaxSize()) {
        val gridColor = NeonCyan.copy(alpha = 0.04f)
        val step = 40f
        var x = -step + (offset % step)
        while (x < size.width + step) {
            drawLine(gridColor, Offset(x, 0f), Offset(x, size.height), strokeWidth = 1f)
            x += step
        }
        var y = -step + (offset % step)
        while (y < size.height + step) {
            drawLine(gridColor, Offset(0f, y), Offset(size.width, y), strokeWidth = 1f)
            y += step
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// HEADER
// ═══════════════════════════════════════════════════════════════════════════════
@Composable
private fun FuturisticHeader(onClose: () -> Unit) {
    val pulse by rememberInfiniteTransition(label = "pulse").animateFloat(
        initialValue = 0.7f, targetValue = 1f,
        animationSpec = infiniteRepeatable(tween(1200), RepeatMode.Reverse),
        label = "pulseAlpha"
    )
    Row(
        verticalAlignment = Alignment.CenterVertically,
        modifier = Modifier
            .fillMaxWidth()
            .background(
                Brush.horizontalGradient(
                    listOf(NeonCyan.copy(alpha = 0.12f), Color.Transparent)
                )
            )
            .border(
                width = 1.dp,
                brush = Brush.horizontalGradient(listOf(NeonCyan.copy(alpha = 0.5f), Color.Transparent)),
                shape = RectangleShape
            )
            .padding(horizontal = 20.dp, vertical = 14.dp)
    ) {
        // Animated logo dot
        Box(
            modifier = Modifier
                .size(10.dp)
                .background(NeonCyan.copy(alpha = pulse), CircleShape)
        )
        Spacer(Modifier.width(12.dp))
        Column(modifier = Modifier.weight(1f)) {
            Text(
                "BUDDYBOT",
                color = NeonCyan,
                fontSize = 20.sp,
                fontWeight = FontWeight.Black,
                letterSpacing = 4.sp,
                fontFamily = FontFamily.Monospace
            )
            Text(
                "CONTROL MATRIX v2.9",
                color = NeonCyan.copy(alpha = 0.5f),
                fontSize = 10.sp,
                letterSpacing = 2.sp,
                fontFamily = FontFamily.Monospace
            )
        }
        IconButton(
            onClick = onClose,
            modifier = Modifier
                .size(40.dp)
                .background(GlassWhite, CircleShape)
                .border(1.dp, GlassBorder, CircleShape)
        ) {
            Icon(Icons.Default.Close, "Close", tint = NeonCyan, modifier = Modifier.size(20.dp))
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// GLASS CARD WRAPPER
// ═══════════════════════════════════════════════════════════════════════════════
@Composable
private fun GlassCard(
    accentColor: Color = NeonCyan,
    modifier: Modifier = Modifier,
    content: @Composable ColumnScope.() -> Unit
) {
    Column(
        modifier = modifier
            .fillMaxWidth()
            .background(
                brush = Brush.verticalGradient(
                    listOf(GlassWhite, Color(0x08FFFFFF))
                ),
                shape = RoundedCornerShape(16.dp)
            )
            .border(
                width = 1.dp,
                brush = Brush.linearGradient(
                    listOf(accentColor.copy(alpha = 0.6f), accentColor.copy(alpha = 0.1f))
                ),
                shape = RoundedCornerShape(16.dp)
            )
            .padding(16.dp),
        content = content
    )
}

// ── Section header inside a card ─────────────────────────────────────────────
@Composable
private fun CardHeader(title: String, icon: ImageVector, accentColor: Color = NeonCyan) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Icon(icon, contentDescription = null, tint = accentColor, modifier = Modifier.size(18.dp))
        Spacer(Modifier.width(8.dp))
        Text(
            title.uppercase(),
            color = accentColor,
            fontSize = 12.sp,
            fontWeight = FontWeight.Bold,
            letterSpacing = 2.sp,
            fontFamily = FontFamily.Monospace
        )
    }
    Spacer(Modifier.height(12.dp))
    Divider(color = accentColor.copy(alpha = 0.25f), thickness = 1.dp)
    Spacer(Modifier.height(12.dp))
}

// ═══════════════════════════════════════════════════════════════════════════════
// STATUS CARD  (Serial + WebSocket status + Test buttons)
// ═══════════════════════════════════════════════════════════════════════════════
@Composable
private fun StatusCard(
    robotState: RobotState,
    telemetry: TelemetryData,
    onTestSerial: (() -> Unit)?,
    onTestWebSocket: (() -> Unit)?,
    // Phase 3: mic state for glowing indicator
    isListening: Boolean = false,
    isSpeaking: Boolean = false
) {
    // Feedback states for test buttons
    var serialFeedback by remember { mutableStateOf<String?>(null) }
    var wsFeedback     by remember { mutableStateOf<String?>(null) }

    GlassCard(accentColor = NeonGreen) {
        CardHeader("System Status", Icons.Default.Dashboard, NeonGreen)

        // Communication mode pill
        val (modeLabel, modeColor) = when (robotState.communicationMode) {
            CommunicationMode.USB_SERIAL  -> "USB SERIAL" to NeonGreen
            CommunicationMode.WEBSOCKET   -> "WEBSOCKET"  to NeonCyan
            CommunicationMode.DISCONNECTED -> "OFFLINE"   to NeonRed
        }
        val pulse by rememberInfiniteTransition(label = "modePulse").animateFloat(
            initialValue = 0.5f, targetValue = 1f,
            animationSpec = infiniteRepeatable(tween(900), RepeatMode.Reverse),
            label = "modeAlpha"
        )

        Row(
            verticalAlignment = Alignment.CenterVertically,
            modifier = Modifier
                .fillMaxWidth()
                .background(modeColor.copy(alpha = 0.12f), RoundedCornerShape(8.dp))
                .border(1.dp, modeColor.copy(alpha = 0.4f), RoundedCornerShape(8.dp))
                .padding(horizontal = 14.dp, vertical = 10.dp)
        ) {
            Box(
                modifier = Modifier
                    .size(10.dp)
                    .background(modeColor.copy(alpha = pulse), CircleShape)
            )
            Spacer(Modifier.width(10.dp))
            Text(
                modeLabel,
                color = modeColor,
                fontSize = 13.sp,
                fontWeight = FontWeight.Bold,
                fontFamily = FontFamily.Monospace,
                letterSpacing = 1.sp
            )
            Spacer(Modifier.weight(1f))
            Text(
                "🔋 ${telemetry.batteryPercent}%",
                color = if (telemetry.batteryPercent < 20) NeonRed else NeonGreen,
                fontSize = 12.sp,
                fontFamily = FontFamily.Monospace
            )
        }

        Spacer(Modifier.height(12.dp))

        // Phase 3: Glowing mic status indicator
        GlowingMicIndicator(isListening = isListening, isSpeaking = isSpeaking)

        Spacer(Modifier.height(12.dp))

        // Test buttons row
        Row(
            horizontalArrangement = Arrangement.spacedBy(10.dp),
            modifier = Modifier.fillMaxWidth()
        ) {
            // Test Serial
            NeonTestButton(
                label = "TEST SERIAL",
                icon = Icons.Default.Usb,
                accentColor = NeonGreen,
                feedback = serialFeedback,
                modifier = Modifier.weight(1f),
                onClick = {
                    serialFeedback = "⏳ Testing..."
                    onTestSerial?.invoke()
                    // Auto-clear feedback after 3s
                }
            )
            // Test WebSocket
            NeonTestButton(
                label = "TEST WS",
                icon = Icons.Default.Wifi,
                accentColor = NeonCyan,
                feedback = wsFeedback,
                modifier = Modifier.weight(1f),
                onClick = {
                    wsFeedback = "⏳ Testing..."
                    onTestWebSocket?.invoke()
                }
            )
        }

        // Auto-clear feedback
        LaunchedEffect(serialFeedback) {
            if (serialFeedback != null && serialFeedback != "⏳ Testing...") {
                delay(3000); serialFeedback = null
            }
        }
        LaunchedEffect(wsFeedback) {
            if (wsFeedback != null && wsFeedback != "⏳ Testing...") {
                delay(3000); wsFeedback = null
            }
        }

        // Update feedback based on actual mode
        LaunchedEffect(robotState.communicationMode) {
            when (robotState.communicationMode) {
                CommunicationMode.USB_SERIAL -> {
                    if (serialFeedback == "⏳ Testing...") serialFeedback = "✅ Serial LIVE"
                }
                CommunicationMode.WEBSOCKET -> {
                    if (wsFeedback == "⏳ Testing...") wsFeedback = "✅ WebSocket LIVE"
                }
                CommunicationMode.DISCONNECTED -> {
                    if (serialFeedback == "⏳ Testing...") serialFeedback = "❌ Not found"
                    if (wsFeedback == "⏳ Testing...") wsFeedback = "❌ Unreachable"
                }
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 3: Glowing mic icon — shows listening/speaking state with neon glow
// ─────────────────────────────────────────────────────────────────────────────
@Composable
private fun GlowingMicIndicator(isListening: Boolean, isSpeaking: Boolean) {
    val transition = rememberInfiniteTransition(label = "micGlow")

    // Pulsing scale when active
    val scale by transition.animateFloat(
        initialValue = 1f,
        targetValue = if (isListening || isSpeaking) 1.25f else 1f,
        animationSpec = infiniteRepeatable(
            animation = tween(600, easing = FastOutSlowInEasing),
            repeatMode = RepeatMode.Reverse
        ),
        label = "micScale"
    )

    // Glow alpha pulse
    val glowAlpha by transition.animateFloat(
        initialValue = 0.2f,
        targetValue = if (isListening || isSpeaking) 0.8f else 0.2f,
        animationSpec = infiniteRepeatable(
            animation = tween(600, easing = FastOutSlowInEasing),
            repeatMode = RepeatMode.Reverse
        ),
        label = "micGlowAlpha"
    )

    val micColor = when {
        isSpeaking  -> NeonOrange   // speaking = orange glow
        isListening -> NeonCyan     // listening = cyan glow
        else        -> Color.Gray   // idle = grey
    }

    val label = when {
        isSpeaking  -> "SPEAKING"
        isListening -> "LISTENING"
        else        -> "IDLE"
    }

    Row(
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.Center,
        modifier = Modifier
            .fillMaxWidth()
            .background(micColor.copy(alpha = 0.08f), RoundedCornerShape(8.dp))
            .border(1.dp, micColor.copy(alpha = 0.3f), RoundedCornerShape(8.dp))
            .padding(horizontal = 14.dp, vertical = 8.dp)
    ) {
        // Outer glow ring
        Box(
            contentAlignment = Alignment.Center,
            modifier = Modifier
                .size(36.dp)
                .background(micColor.copy(alpha = glowAlpha * 0.3f), CircleShape)
        ) {
            // Inner mic icon with scale
            Icon(
                imageVector = Icons.Default.Mic,
                contentDescription = "Microphone $label",
                tint = micColor,
                modifier = Modifier
                    .size(22.dp)
                    .graphicsLayer(scaleX = scale, scaleY = scale)
                    .neonGlow(micColor, radius = if (isListening || isSpeaking) 18f else 0f)
            )
        }
        Spacer(Modifier.width(10.dp))
        Column {
            Text(
                "Hey Buddy",
                color = micColor,
                fontSize = 11.sp,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold,
                letterSpacing = 1.sp
            )
            Text(
                label,
                color = micColor.copy(alpha = 0.7f),
                fontSize = 10.sp,
                fontFamily = FontFamily.Monospace
            )
        }
        Spacer(Modifier.weight(1f))
        // Waveform bars (3 animated bars when active)
        if (isListening || isSpeaking) {
            Row(horizontalArrangement = Arrangement.spacedBy(3.dp), verticalAlignment = Alignment.CenterVertically) {
                listOf(0, 150, 300).forEach { delayMs ->
                    val barH by rememberInfiniteTransition(label = "bar$delayMs").animateFloat(
                        initialValue = 4f, targetValue = 16f,
                        animationSpec = infiniteRepeatable(
                            tween(400, delayMillis = delayMs, easing = FastOutSlowInEasing),
                            RepeatMode.Reverse
                        ),
                        label = "barH$delayMs"
                    )
                    Box(
                        modifier = Modifier
                            .width(3.dp)
                            .height(barH.dp)
                            .background(micColor, RoundedCornerShape(2.dp))
                    )
                }
            }
        }
    }
}

@Composable
private fun NeonTestButton(
    label: String,
    icon: ImageVector,
    accentColor: Color,
    feedback: String?,
    modifier: Modifier = Modifier,
    onClick: () -> Unit
) {
    val scale by animateFloatAsState(
        targetValue = if (feedback != null) 0.97f else 1f,
        animationSpec = spring(stiffness = Spring.StiffnessMedium),
        label = "btnScale"
    )
    Column(
        modifier = modifier,
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Button(
            onClick = onClick,
            modifier = Modifier
                .fillMaxWidth()
                .scale(scale)
                .height(44.dp),
            shape = RoundedCornerShape(10.dp),
            colors = ButtonDefaults.buttonColors(
                containerColor = accentColor.copy(alpha = 0.15f),
                contentColor = accentColor
            ),
            border = BorderStroke(1.dp, accentColor.copy(alpha = 0.6f))
        ) {
            Icon(icon, null, modifier = Modifier.size(14.dp))
            Spacer(Modifier.width(6.dp))
            Text(label, fontSize = 10.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace, letterSpacing = 1.sp)
        }
        AnimatedVisibility(visible = feedback != null) {
            Text(
                feedback ?: "",
                color = accentColor,
                fontSize = 10.sp,
                fontFamily = FontFamily.Monospace,
                modifier = Modifier.padding(top = 4.dp)
            )
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// CONNECTION CARD
// ═══════════════════════════════════════════════════════════════════════════════
@Composable
private fun ConnectionCard(
    robotState: RobotState,
    onToggleCommunication: () -> Unit,
    onIPChange: (String) -> Unit
) {
    var ipInput by remember { mutableStateOf(robotState.buddybotIP) }
    var ipSaved by remember { mutableStateOf(false) }

    GlassCard(accentColor = NeonCyan) {
        CardHeader("Connection", Icons.Default.SettingsEthernet, NeonCyan)

        // USB Serial toggle
        NeonToggleRow(
            icon = Icons.Default.Usb,
            label = "USB Serial",
            sublabel = "115200 baud · Mega 2560",
            isActive = robotState.communicationMode == CommunicationMode.USB_SERIAL,
            accentColor = NeonGreen,
            onClick = onToggleCommunication
        )

        Spacer(Modifier.height(8.dp))

        // WebSocket toggle
        NeonToggleRow(
            icon = Icons.Default.Wifi,
            label = "WebSocket",
            sublabel = "Port ${BuddyBotConfig.WEBSOCKET_PORT} · ESP32 bridge",
            isActive = robotState.communicationMode == CommunicationMode.WEBSOCKET,
            accentColor = NeonCyan,
            onClick = onToggleCommunication
        )

        Spacer(Modifier.height(14.dp))

        // IP input
        Text(
            "ROBOT IP ADDRESS",
            color = NeonCyan.copy(alpha = 0.6f),
            fontSize = 10.sp,
            fontFamily = FontFamily.Monospace,
            letterSpacing = 1.sp
        )
        Spacer(Modifier.height(6.dp))
        Row(
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            OutlinedTextField(
                value = ipInput,
                onValueChange = { ipInput = it; ipSaved = false },
                placeholder = {
                    Text("192.168.1.100", color = NeonCyan.copy(alpha = 0.3f),
                        fontFamily = FontFamily.Monospace, fontSize = 13.sp)
                },
                singleLine = true,
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri),
                modifier = Modifier.weight(1f),
                colors = OutlinedTextFieldDefaults.colors(
                    focusedBorderColor = NeonCyan,
                    unfocusedBorderColor = NeonCyan.copy(alpha = 0.3f),
                    focusedTextColor = Color.White,
                    unfocusedTextColor = Color.White,
                    cursorColor = NeonCyan,
                    focusedContainerColor = DarkCard,
                    unfocusedContainerColor = DarkCard
                ),
                textStyle = LocalTextStyle.current.copy(
                    fontFamily = FontFamily.Monospace,
                    fontSize = 13.sp
                ),
                shape = RoundedCornerShape(8.dp)
            )
            Button(
                onClick = {
                    onIPChange(ipInput)
                    ipSaved = true
                },
                shape = RoundedCornerShape(8.dp),
                colors = ButtonDefaults.buttonColors(
                    containerColor = if (ipSaved) NeonGreen.copy(alpha = 0.2f) else NeonCyan.copy(alpha = 0.2f),
                    contentColor = if (ipSaved) NeonGreen else NeonCyan
                ),
                border = BorderStroke(1.dp, if (ipSaved) NeonGreen.copy(alpha = 0.6f) else NeonCyan.copy(alpha = 0.6f)),
                modifier = Modifier.height(56.dp)
            ) {
                Text(
                    if (ipSaved) "✓" else "SAVE",
                    fontFamily = FontFamily.Monospace,
                    fontWeight = FontWeight.Bold,
                    fontSize = 12.sp
                )
            }
        }
    }
}

// ── Neon toggle row ───────────────────────────────────────────────────────────
@Composable
private fun NeonToggleRow(
    icon: ImageVector,
    label: String,
    sublabel: String,
    isActive: Boolean,
    accentColor: Color,
    onClick: () -> Unit
) {
    val glowAlpha by animateFloatAsState(
        targetValue = if (isActive) 0.25f else 0f,
        animationSpec = tween(400),
        label = "glow"
    )
    Row(
        verticalAlignment = Alignment.CenterVertically,
        modifier = Modifier
            .fillMaxWidth()
            .background(
                if (isActive) accentColor.copy(alpha = 0.1f) else Color.Transparent,
                RoundedCornerShape(10.dp)
            )
            .border(
                1.dp,
                if (isActive) accentColor.copy(alpha = 0.5f) else GlassBorder,
                RoundedCornerShape(10.dp)
            )
            .clickable(onClick = onClick)
            .padding(horizontal = 14.dp, vertical = 10.dp)
    ) {
        Box(
            modifier = Modifier
                .size(36.dp)
                .background(accentColor.copy(alpha = 0.15f), CircleShape)
                .border(1.dp, accentColor.copy(alpha = 0.4f), CircleShape),
            contentAlignment = Alignment.Center
        ) {
            Icon(icon, null, tint = accentColor, modifier = Modifier.size(18.dp))
        }
        Spacer(Modifier.width(12.dp))
        Column(modifier = Modifier.weight(1f)) {
            Text(label, color = Color.White, fontSize = 14.sp, fontWeight = FontWeight.SemiBold)
            Text(sublabel, color = Color.White.copy(alpha = 0.4f), fontSize = 10.sp, fontFamily = FontFamily.Monospace)
        }
        // Glow switch
        NeonSwitch(checked = isActive, onCheckedChange = { onClick() }, accentColor = accentColor)
    }
}

// ── Glowing switch ────────────────────────────────────────────────────────────
@Composable
private fun NeonSwitch(checked: Boolean, onCheckedChange: (Boolean) -> Unit, accentColor: Color) {
    val thumbColor by animateColorAsState(
        targetValue = if (checked) accentColor else Color.Gray,
        animationSpec = tween(300),
        label = "thumbColor"
    )
    val trackColor by animateColorAsState(
        targetValue = if (checked) accentColor.copy(alpha = 0.3f) else Color.Gray.copy(alpha = 0.2f),
        animationSpec = tween(300),
        label = "trackColor"
    )
    Switch(
        checked = checked,
        onCheckedChange = onCheckedChange,
        colors = SwitchDefaults.colors(
            checkedThumbColor = thumbColor,
            checkedTrackColor = trackColor,
            uncheckedThumbColor = Color.Gray,
            uncheckedTrackColor = Color.Gray.copy(alpha = 0.2f),
            checkedBorderColor = accentColor.copy(alpha = 0.6f),
            uncheckedBorderColor = Color.Gray.copy(alpha = 0.3f)
        )
    )
}

// ═══════════════════════════════════════════════════════════════════════════════
// AI SETTINGS CARD
// ═══════════════════════════════════════════════════════════════════════════════
@Composable
private fun AISettingsCard(robotState: RobotState) {
    GlassCard(accentColor = NeonPurple) {
        CardHeader("AI Settings", Icons.Default.Psychology, NeonPurple)

        // Active AI service indicator
        val (aiLabel, aiColor) = when (robotState.aiService) {
            AIService.CLAUDE  -> "Claude 3 Sonnet" to NeonPurple
            AIService.GEMINI  -> "Gemini 1.5 Flash" to NeonCyan
            AIService.OFFLINE -> "Offline Mode" to NeonOrange
        }

        Row(
            verticalAlignment = Alignment.CenterVertically,
            modifier = Modifier
                .fillMaxWidth()
                .background(aiColor.copy(alpha = 0.1f), RoundedCornerShape(8.dp))
                .border(1.dp, aiColor.copy(alpha = 0.4f), RoundedCornerShape(8.dp))
                .padding(12.dp)
        ) {
            Icon(Icons.Default.AutoAwesome, null, tint = aiColor, modifier = Modifier.size(20.dp))
            Spacer(Modifier.width(10.dp))
            Column {
                Text("Active AI Engine", color = Color.White.copy(alpha = 0.5f), fontSize = 10.sp, fontFamily = FontFamily.Monospace)
                Text(aiLabel, color = aiColor, fontSize = 14.sp, fontWeight = FontWeight.Bold)
            }
        }

        Spacer(Modifier.height(12.dp))

        // Info rows
        NeonInfoRow("Wake Word", BuddyBotConfig.WAKE_WORD.uppercase(), NeonPurple)
        NeonInfoRow("Voice Engine", "ElevenLabs TTS", NeonPurple)
        NeonInfoRow("Silence Threshold", "${BuddyBotConfig.SILENCE_THRESHOLD_MS}ms", NeonPurple)
        NeonInfoRow("Child Profile", "${BuddyBotConfig.CHILD_NAME} · Age ${BuddyBotConfig.CHILD_AGE}", NeonPurple)
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// ROBOT MODES CARD
// ═══════════════════════════════════════════════════════════════════════════════
@Composable
private fun RobotModesCard(currentMode: RobotMode, onModeChange: (RobotMode) -> Unit) {
    GlassCard(accentColor = NeonOrange) {
        CardHeader("Robot Modes", Icons.Default.SmartToy, NeonOrange)

        val modeConfig = listOf(
            Triple(RobotMode.NORMAL,    "🤖", NeonCyan),
            Triple(RobotMode.DOG,       "🐕", NeonGreen),
            Triple(RobotMode.BODYGUARD, "🛡️", NeonOrange),
            Triple(RobotMode.UNHINGED,  "🌀", NeonPurple),
            Triple(RobotMode.PARTY,     "🎉", NeonYellow)
        )

        // 2-column grid
        val rows = modeConfig.chunked(2)
        rows.forEach { rowItems ->
            Row(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                modifier = Modifier.fillMaxWidth()
            ) {
                rowItems.forEach { (mode, emoji, color) ->
                    ModeChip(
                        mode = mode,
                        emoji = emoji,
                        accentColor = color,
                        isSelected = mode == currentMode,
                        onClick = { onModeChange(mode) },
                        modifier = Modifier.weight(1f)
                    )
                }
                // Fill empty slot if odd number
                if (rowItems.size == 1) Spacer(Modifier.weight(1f))
            }
            Spacer(Modifier.height(8.dp))
        }
    }
}

@Composable
private fun ModeChip(
    mode: RobotMode,
    emoji: String,
    accentColor: Color,
    isSelected: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier
) {
    val scale by animateFloatAsState(
        targetValue = if (isSelected) 1.04f else 1f,
        animationSpec = spring(stiffness = Spring.StiffnessMedium),
        label = "modeScale"
    )
    Box(
        modifier = modifier
            .scale(scale)
            .background(
                if (isSelected) accentColor.copy(alpha = 0.2f) else GlassWhite,
                RoundedCornerShape(12.dp)
            )
            .border(
                width = if (isSelected) 1.5.dp else 1.dp,
                color = if (isSelected) accentColor else GlassBorder,
                shape = RoundedCornerShape(12.dp)
            )
            .clickable(onClick = onClick)
            .padding(vertical = 12.dp),
        contentAlignment = Alignment.Center
    ) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Text(emoji, fontSize = 22.sp)
            Spacer(Modifier.height(4.dp))
            Text(
                mode.name,
                color = if (isSelected) accentColor else Color.White.copy(alpha = 0.6f),
                fontSize = 10.sp,
                fontWeight = if (isSelected) FontWeight.Bold else FontWeight.Normal,
                fontFamily = FontFamily.Monospace,
                letterSpacing = 1.sp
            )
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// MOTOR CONTROLS CARD
// ═══════════════════════════════════════════════════════════════════════════════
@Composable
private fun MotorControlsCard(onMotorCommand: (String) -> Unit) {
    GlassCard(accentColor = NeonYellow) {
        CardHeader("Motor Controls", Icons.Default.DirectionsCar, NeonYellow)

        // D-pad layout
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            modifier = Modifier.fillMaxWidth()
        ) {
            // Forward
            MotorBtn("▲", "MOTOR:F", NeonYellow, onMotorCommand, Modifier.size(56.dp))
            Spacer(Modifier.height(6.dp))
            // Left / Stop / Right
            Row(
                horizontalArrangement = Arrangement.spacedBy(6.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                MotorBtn("◄", "MOTOR:L", NeonYellow, onMotorCommand, Modifier.size(56.dp))
                MotorBtn("■", "MOTOR:S", NeonRed, onMotorCommand, Modifier.size(56.dp))
                MotorBtn("►", "MOTOR:R", NeonYellow, onMotorCommand, Modifier.size(56.dp))
            }
            Spacer(Modifier.height(6.dp))
            // Backward
            MotorBtn("▼", "MOTOR:B", NeonYellow, onMotorCommand, Modifier.size(56.dp))
            Spacer(Modifier.height(10.dp))
            // Dance
            Button(
                onClick = { onMotorCommand("MOTOR:DANCE") },
                shape = RoundedCornerShape(10.dp),
                colors = ButtonDefaults.buttonColors(
                    containerColor = NeonPurple.copy(alpha = 0.2f),
                    contentColor = NeonPurple
                ),
                border = BorderStroke(1.dp, NeonPurple.copy(alpha = 0.6f)),
                modifier = Modifier.fillMaxWidth(0.6f).height(40.dp)
            ) {
                Text("🎉 DANCE", fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold, fontSize = 12.sp)
            }
        }
    }
}

@Composable
private fun MotorBtn(
    label: String,
    command: String,
    color: Color,
    onMotorCommand: (String) -> Unit,
    modifier: Modifier = Modifier
) {
    var pressed by remember { mutableStateOf(false) }
    val scale by animateFloatAsState(
        targetValue = if (pressed) 0.88f else 1f,
        animationSpec = spring(stiffness = Spring.StiffnessHigh),
        label = "motorBtnScale"
    )
    Box(
        modifier = modifier
            .scale(scale)
            .background(color.copy(alpha = 0.15f), RoundedCornerShape(10.dp))
            .border(1.dp, color.copy(alpha = 0.6f), RoundedCornerShape(10.dp))
            .clickable {
                pressed = true
                onMotorCommand(command)
            },
        contentAlignment = Alignment.Center
    ) {
        Text(label, color = color, fontSize = 20.sp, fontWeight = FontWeight.Bold)
    }
    LaunchedEffect(pressed) {
        if (pressed) { delay(150); pressed = false }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// CAMERA CARD
// ═══════════════════════════════════════════════════════════════════════════════
@Composable
private fun CameraCard(webcamClient: CameraClient?) {
    var showPreview by remember { mutableStateOf(false) }

    GlassCard(accentColor = NeonCyan) {
        CardHeader("Camera", Icons.Default.Videocam, NeonCyan)

        NeonToggleRow(
            icon = Icons.Default.Videocam,
            label = "Live Preview",
            sublabel = if (webcamClient != null) "USB Webcam · 640×480" else "No camera detected",
            isActive = showPreview && webcamClient != null,
            accentColor = NeonCyan,
            onClick = { if (webcamClient != null) showPreview = !showPreview }
        )

        AnimatedVisibility(
            visible = showPreview && webcamClient != null,
            enter = expandVertically(tween(400)) + fadeIn(tween(300)),
            exit = shrinkVertically(tween(300)) + fadeOut(tween(200))
        ) {
            val lifecycleOwner = LocalLifecycleOwner.current
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(200.dp)
                    .padding(top = 12.dp)
                    .clip(RoundedCornerShape(12.dp))
                    .background(Color.Black)
                    .border(1.dp, NeonCyan.copy(alpha = 0.4f), RoundedCornerShape(12.dp))
            ) {
                AndroidView(
                    factory = { context ->
                        com.jiangdg.ausbc.widget.AspectRatioTextureView(context).also { tv ->
                            lifecycleOwner.lifecycleScope.launch(Dispatchers.IO) {
                                webcamClient?.openCamera(tv)
                            }
                        }
                    },
                    modifier = Modifier.fillMaxSize()
                )
                // Scan-line overlay for futuristic look
                Box(
                    modifier = Modifier
                        .fillMaxSize()
                        .background(
                            Brush.verticalGradient(
                                listOf(
                                    NeonCyan.copy(alpha = 0.03f),
                                    Color.Transparent,
                                    NeonCyan.copy(alpha = 0.03f)
                                )
                            )
                        )
                )
                Text(
                    "● REC",
                    color = NeonRed,
                    fontSize = 10.sp,
                    fontFamily = FontFamily.Monospace,
                    modifier = Modifier
                        .align(Alignment.TopEnd)
                        .padding(8.dp)
                        .background(Color.Black.copy(alpha = 0.6f), RoundedCornerShape(4.dp))
                        .padding(horizontal = 6.dp, vertical = 2.dp)
                )
            }
        }

        if (webcamClient == null) {
            Spacer(Modifier.height(8.dp))
            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier
                    .fillMaxWidth()
                    .background(NeonRed.copy(alpha = 0.08f), RoundedCornerShape(8.dp))
                    .border(1.dp, NeonRed.copy(alpha = 0.3f), RoundedCornerShape(8.dp))
                    .padding(10.dp)
            ) {
                Icon(Icons.Default.Warning, null, tint = NeonRed, modifier = Modifier.size(16.dp))
                Spacer(Modifier.width(8.dp))
                Text("USB webcam not detected", color = NeonRed, fontSize = 12.sp, fontFamily = FontFamily.Monospace)
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// TELEMETRY CARD
// ═══════════════════════════════════════════════════════════════════════════════
@Composable
private fun TelemetryCard(telemetry: TelemetryData) {
    GlassCard(accentColor = NeonOrange) {
        CardHeader("Telemetry", Icons.Default.Analytics, NeonOrange)

        // Battery bar
        val battColor = when {
            telemetry.batteryPercent < 15 -> NeonRed
            telemetry.batteryPercent < 40 -> NeonOrange
            else -> NeonGreen
        }
        Column {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("BATTERY", color = Color.White.copy(alpha = 0.5f), fontSize = 10.sp, fontFamily = FontFamily.Monospace)
                Spacer(Modifier.weight(1f))
                Text(
                    "${telemetry.batteryPercent}% · ${String.format("%.2f", telemetry.batteryVoltage)}V",
                    color = battColor,
                    fontSize = 11.sp,
                    fontFamily = FontFamily.Monospace,
                    fontWeight = FontWeight.Bold
                )
            }
            Spacer(Modifier.height(4.dp))
            LinearProgressIndicator(
                progress = (telemetry.batteryPercent / 100f).coerceIn(0f, 1f),
                modifier = Modifier.fillMaxWidth().height(6.dp).clip(RoundedCornerShape(3.dp)),
                color = battColor,
                trackColor = battColor.copy(alpha = 0.15f)
            )
        }

        Spacer(Modifier.height(12.dp))

        // Ultrasonic distances grid
        Text("ULTRASONIC SENSORS", color = Color.White.copy(alpha = 0.5f), fontSize = 10.sp, fontFamily = FontFamily.Monospace)
        Spacer(Modifier.height(6.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            DistanceChip("F", telemetry.frontDistance, Modifier.weight(1f))
            DistanceChip("R", telemetry.rearDistance, Modifier.weight(1f))
            DistanceChip("L", telemetry.leftDistance, Modifier.weight(1f))
            DistanceChip("Ri", telemetry.rightDistance, Modifier.weight(1f))
        }

        Spacer(Modifier.height(10.dp))

        // Other telemetry
        NeonInfoRow("Moving", if (telemetry.isMoving) "YES" else "NO", if (telemetry.isMoving) NeonGreen else NeonOrange)
        NeonInfoRow("Hazard", if (telemetry.hazardDetected) "DETECTED" else "CLEAR", if (telemetry.hazardDetected) NeonRed else NeonGreen)
    }
}

@Composable
private fun DistanceChip(label: String, value: Int, modifier: Modifier = Modifier) {
    val color = when {
        value == -1 -> Color.Gray
        value < 20  -> NeonRed
        value < 50  -> NeonOrange
        else        -> NeonGreen
    }
    Column(
        modifier = modifier
            .background(color.copy(alpha = 0.1f), RoundedCornerShape(8.dp))
            .border(1.dp, color.copy(alpha = 0.4f), RoundedCornerShape(8.dp))
            .padding(vertical = 8.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text(label, color = color.copy(alpha = 0.7f), fontSize = 9.sp, fontFamily = FontFamily.Monospace)
        Text(
            if (value == -1) "--" else "${value}cm",
            color = color,
            fontSize = 12.sp,
            fontWeight = FontWeight.Bold,
            fontFamily = FontFamily.Monospace
        )
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// LOGS CARD
// ═══════════════════════════════════════════════════════════════════════════════
@Composable
private fun LogsCard(logs: List<String>) {
    var expanded by remember { mutableStateOf(false) }

    GlassCard(accentColor = Color.Gray) {
        Row(
            verticalAlignment = Alignment.CenterVertically,
            modifier = Modifier
                .fillMaxWidth()
                .clickable { expanded = !expanded }
        ) {
            Icon(Icons.Default.Terminal, null, tint = NeonGreen, modifier = Modifier.size(18.dp))
            Spacer(Modifier.width(8.dp))
            Text(
                "COMM LOGS",
                color = NeonGreen,
                fontSize = 12.sp,
                fontWeight = FontWeight.Bold,
                letterSpacing = 2.sp,
                fontFamily = FontFamily.Monospace,
                modifier = Modifier.weight(1f)
            )
            Text(
                "${logs.size} entries",
                color = Color.Gray,
                fontSize = 10.sp,
                fontFamily = FontFamily.Monospace
            )
            Spacer(Modifier.width(8.dp))
            Icon(
                if (expanded) Icons.Default.ExpandLess else Icons.Default.ExpandMore,
                null,
                tint = Color.Gray,
                modifier = Modifier.size(18.dp)
            )
        }

        AnimatedVisibility(
            visible = expanded,
            enter = expandVertically(tween(300)) + fadeIn(tween(200)),
            exit = shrinkVertically(tween(200)) + fadeOut(tween(150))
        ) {
            Column {
                Spacer(Modifier.height(10.dp))
                Divider(color = NeonGreen.copy(alpha = 0.2f))
                Spacer(Modifier.height(8.dp))
                val displayLogs = logs.take(30)
                displayLogs.forEach { log ->
                    val logColor = when {
                        log.contains("ERROR") || log.contains("❌") -> NeonRed
                        log.contains("✅") || log.contains("LIVE")  -> NeonGreen
                        log.contains("⚠️") || log.contains("WARN")  -> NeonOrange
                        log.contains("COMM") || log.contains("ARD") -> NeonCyan
                        else -> Color.Gray
                    }
                    Text(
                        text = log,
                        fontSize = 9.sp,
                        color = logColor,
                        fontFamily = FontFamily.Monospace,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        modifier = Modifier.padding(vertical = 1.dp)
                    )
                }
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// SHARED HELPERS
// ═══════════════════════════════════════════════════════════════════════════════
@Composable
private fun NeonInfoRow(label: String, value: String, accentColor: Color = NeonCyan) {
    Row(
        verticalAlignment = Alignment.CenterVertically,
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp)
    ) {
        Text(
            label,
            color = Color.White.copy(alpha = 0.45f),
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace,
            modifier = Modifier.weight(1f)
        )
        Text(
            value,
            color = accentColor,
            fontSize = 11.sp,
            fontWeight = FontWeight.Bold,
            fontFamily = FontFamily.Monospace
        )
    }
}
