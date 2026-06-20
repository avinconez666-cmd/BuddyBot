package com.buddybot.kids

// SettingsMenu has been moved to SettingsScreen.kt (Phase 2 – futuristic UI).
// This file retains all other shared UI composables.

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
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
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.lifecycle.lifecycleScope
import com.jiangdg.ausbc.CameraClient
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.spring
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.drawIntoCanvas
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.foundation.Canvas
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Paint
import androidx.compose.ui.graphics.toArgb
import android.graphics.Paint as NativePaint

@Composable
fun ProcessingOverlay() {
    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(Color.Black.copy(alpha = 0.7f)),
        contentAlignment = Alignment.Center
    ) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            CircularProgressIndicator(color = Color.White)
            Spacer(modifier = Modifier.height(16.dp))
            Text("Processing...", color = Color.White, fontSize = 18.sp)
        }
    }
}

@Composable
fun EmergencyOverlay() {
    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(Color.Red.copy(alpha = 0.3f))
            .border(16.dp, Color.Red)
    ) {
        Text(
            "⚠️ EMERGENCY STOP ⚠️",
            color = Color.White,
            fontSize = 32.sp,
            fontWeight = FontWeight.Bold,
            modifier = Modifier.align(Alignment.Center)
        )
    }
}

@Composable
fun BuddyBotOverlay(
    robotState: RobotState,
    telemetry: TelemetryData,
    onCallDaddy: () -> Unit,
    onEmergency: () -> Unit,
    onOpenMenu: () -> Unit,
    onTap: () -> Unit
) {
    Box(
        modifier = Modifier
            .fillMaxSize()
            .pointerInput(Unit) {
                detectTapGestures(onTap = { onTap() })
            }
    ) {
        // ── Invisible settings trigger — top-left 72×72 dp corner ─────────
        // No icon, no background, no hint it exists. Tap it to open settings.
        Box(
            modifier = Modifier
                .align(Alignment.TopStart)
                .size(72.dp)
                .clickable { onOpenMenu() }
        )

        // ── Battery indicator — top centre ────────────────────────────────
        Text(
            text = "🔋 ${telemetry.batteryPercent}% (${String.format("%.1f", telemetry.batteryVoltage)}V)",
            color = if (telemetry.batteryPercent < 20) Color.Red else Color.Green,
            fontWeight = FontWeight.Bold,
            modifier = Modifier
                .align(Alignment.TopCenter)
                .padding(top = 16.dp)
                .background(Color.Black.copy(alpha = 0.6f), RoundedCornerShape(8.dp))
                .padding(horizontal = 12.dp, vertical = 4.dp)
        )

        // ── ML detection overlay ──────────────────────────────────────────
        DetectionOverlay(
            faces = robotState.detectedFaces,
            objects = robotState.detectedObjects
        )

        // ── Call Daddy button — top right ─────────────────────────────────
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            modifier = Modifier
                .align(Alignment.TopEnd)
                .padding(end = 16.dp, top = 12.dp)
        ) {
            Box(
                modifier = Modifier
                    .size(80.dp)
                    .clip(CircleShape)
                    .background(
                        if (robotState.isCameraConnected) Color.Green.copy(alpha = 0.3f)
                        else Color.Yellow.copy(alpha = 0.3f)
                    )
                    .border(
                        4.dp,
                        if (robotState.isCameraConnected) Color.Green else Color.Yellow,
                        CircleShape
                    )
                    .clickable { onCallDaddy() },
                contentAlignment = Alignment.Center
            ) {
                Text("📞", fontSize = 32.sp)
            }
            Spacer(Modifier.height(4.dp))
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.Center,
                modifier = Modifier
                    .background(Color.Black.copy(alpha = 0.6f), RoundedCornerShape(6.dp))
                    .padding(horizontal = 6.dp, vertical = 2.dp)
            ) {
                Box(
                    modifier = Modifier
                        .size(6.dp)
                        .background(
                            if (robotState.isCameraConnected) Color.Green else Color.Yellow,
                            CircleShape
                        )
                )
                Spacer(Modifier.width(4.dp))
                Text(
                    text = if (robotState.isCameraConnected) "CAM OK" else "NO CAM",
                    color = if (robotState.isCameraConnected) Color.Green else Color.Yellow,
                    fontSize = 9.sp,
                    fontWeight = FontWeight.Bold
                )
            }
        }

        // ── Recognised person label ───────────────────────────────────────
        if (robotState.recognizedPerson != null) {
            Text(
                text = "Detected: ${robotState.recognizedPerson}",
                color = Color.Green,
                fontWeight = FontWeight.Bold,
                modifier = Modifier
                    .align(Alignment.CenterStart)
                    .padding(start = 16.dp)
                    .background(Color.Black.copy(alpha = 0.6f), RoundedCornerShape(8.dp))
                    .padding(horizontal = 12.dp, vertical = 4.dp)
            )
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// PasscodeDialog & PinEntryDialog – unchanged
// ─────────────────────────────────────────────────────────────────────────────

@Composable
fun PasscodeDialog(correctPasscode: String, onConfirm: () -> Unit, onDismiss: () -> Unit) {
    var text by remember { mutableStateOf("") }
    var error by remember { mutableStateOf(false) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Exit Authentication") },
        text = {
            Column {
                TextField(
                    value = text,
                    onValueChange = { text = it; error = false },
                    visualTransformation = PasswordVisualTransformation(),
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                    isError = error,
                    modifier = Modifier.fillMaxWidth()
                )
                if (error) Text("Incorrect passcode", color = Color.Red, fontSize = 12.sp)
            }
        },
        confirmButton = {
            Button(onClick = { if (text == correctPasscode) onConfirm() else error = true }) {
                Text("Exit")
            }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } }
    )
}

@Composable
fun PinEntryDialog(
    onConfirm: (String) -> Unit,
    onDismiss: () -> Unit,
    requestedMode: RobotMode?
) {
    var text by remember { mutableStateOf("") }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("PIN Required for Mode Change") },
        text = {
            Column {
                Text("Enter PIN to switch to ${requestedMode?.name ?: "a new mode"}")
                TextField(
                    value = text,
                    onValueChange = { text = it },
                    visualTransformation = PasswordVisualTransformation(),
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                    modifier = Modifier.fillMaxWidth()
                )
            }
        },
        confirmButton = { Button(onClick = { onConfirm(text) }) { Text("Confirm") } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } }
    )
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 5: Detection Overlay — draws bounding boxes + labels for faces/objects
// Rendered on top of the main UI using a Canvas composable.
// ─────────────────────────────────────────────────────────────────────────────
@Composable
fun DetectionOverlay(
    faces: List<FaceResult>,
    objects: List<DetectedObjectResult>,
    // Camera frame dimensions (used to scale boxes to screen)
    frameWidth: Int = 640,
    frameHeight: Int = 480,
    modifier: Modifier = Modifier
) {
    if (faces.isEmpty() && objects.isEmpty()) return

    Canvas(modifier = modifier.fillMaxSize()) {
        val scaleX = size.width / frameWidth.toFloat()
        val scaleY = size.height / frameHeight.toFloat()

        // ── Face boxes (cyan) ────────────────────────────────────────────────
        faces.forEach { face ->
            val left   = face.bounds.left   * scaleX
            val top    = face.bounds.top    * scaleY
            val right  = face.bounds.right  * scaleX
            val bottom = face.bounds.bottom * scaleY

            // Bounding box
            drawRect(
                color = if (face.name != null) Color(0xFF00FFFF) else Color(0xFFFFFF00),
                topLeft = Offset(left, top),
                size = androidx.compose.ui.geometry.Size(right - left, bottom - top),
                style = androidx.compose.ui.graphics.drawscope.Stroke(width = 3f)
            )

            // Label with name + confidence
            val label = if (face.name != null)
                "${face.name} (${"%.0f".format(face.confidence * 100)}%)"
            else
                "Unknown (${"%.0f".format(face.confidence * 100)}%)"

            drawIntoCanvas { canvas ->
                val paint = NativePaint().apply {
                    color = if (face.name != null) 0xFF00FFFF.toInt() else 0xFFFFFF00.toInt()
                    textSize = 32f
                    isAntiAlias = true
                    setShadowLayer(4f, 2f, 2f, 0xFF000000.toInt())
                }
                canvas.nativeCanvas.drawText(label, left, (top - 8f).coerceAtLeast(32f), paint)
            }
        }

        // ── Object boxes (green) ─────────────────────────────────────────────
        objects.forEach { obj ->
            val left   = obj.bounds.left   * scaleX
            val top    = obj.bounds.top    * scaleY
            val right  = obj.bounds.right  * scaleX
            val bottom = obj.bounds.bottom * scaleY

            drawRect(
                color = Color(0xFF00FF88),
                topLeft = Offset(left, top),
                size = androidx.compose.ui.geometry.Size(right - left, bottom - top),
                style = androidx.compose.ui.graphics.drawscope.Stroke(width = 2f)
            )

            val label = "${obj.label} (${"%.0f".format(obj.confidence * 100)}%)"
            drawIntoCanvas { canvas ->
                val paint = NativePaint().apply {
                    color = 0xFF00FF88.toInt()
                    textSize = 28f
                    isAntiAlias = true
                    setShadowLayer(4f, 2f, 2f, 0xFF000000.toInt())
                }
                canvas.nativeCanvas.drawText(label, left, (top - 8f).coerceAtLeast(28f), paint)
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// LipSyncMouthOverlay — animated mouth drawn on top of face videos.
// Only shown during ElevenLabs TTS; amplitude (0..1) drives the jaw open amount.
// Positioned bottom-centre in landscape by the parent Box.
// ─────────────────────────────────────────────────────────────────────────────
@Composable
fun LipSyncMouthOverlay(
    amplitude: Float,
    mode: RobotMode,
    modifier: Modifier = Modifier
) {
    val lipColor = when (mode) {
        RobotMode.NORMAL    -> Color(0xFF00D4FF)
        RobotMode.DOG       -> Color(0xFF33FF33)
        RobotMode.BODYGUARD -> Color(0xFFFF9500)
        RobotMode.UNHINGED  -> Color(0xFFBF00FF)
        RobotMode.PARTY     -> Color(0xFFFFDD00)
    }

    val smoothAmp by animateFloatAsState(
        targetValue = amplitude.coerceIn(0f, 1f),
        animationSpec = spring(stiffness = Spring.StiffnessMediumLow),
        label = "lipSyncAmp"
    )

    Box(
        modifier = modifier
            .background(Color(0xCC000000), RoundedCornerShape(40.dp))
            .padding(horizontal = 28.dp, vertical = 14.dp),
        contentAlignment = Alignment.Center
    ) {
        Canvas(
            modifier = Modifier
                .width(200.dp)
                .height(70.dp)
        ) {
            val cx      = size.width / 2f
            val baseY   = size.height * 0.48f
            val halfW   = size.width * 0.46f
            val lipCurve = size.height * 0.20f
            val openAmt  = smoothAmp * size.height * 0.58f

            val cornerL = Offset(cx - halfW, baseY)
            val cornerR = Offset(cx + halfW, baseY)

            // Dark mouth cavity — only drawn when open
            if (openAmt > 2f) {
                val cavity = Path().apply {
                    moveTo(cornerL.x, cornerL.y)
                    cubicTo(
                        cx - halfW * 0.5f, baseY - lipCurve,
                        cx + halfW * 0.5f, baseY - lipCurve,
                        cornerR.x, cornerR.y
                    )
                    cubicTo(
                        cx + halfW * 0.5f, baseY + openAmt,
                        cx - halfW * 0.5f, baseY + openAmt,
                        cornerL.x, cornerL.y
                    )
                    close()
                }
                drawPath(cavity, color = Color(0xFF0A0010))
            }

            val strokeUpper = Stroke(width = 3.5.dp.toPx(), cap = StrokeCap.Round)
            val strokeLower = Stroke(width = 2.5.dp.toPx(), cap = StrokeCap.Round)

            // Upper lip
            drawPath(
                Path().apply {
                    moveTo(cornerL.x, cornerL.y)
                    cubicTo(
                        cx - halfW * 0.5f, baseY - lipCurve,
                        cx + halfW * 0.5f, baseY - lipCurve,
                        cornerR.x, cornerR.y
                    )
                },
                color = lipColor,
                style = strokeUpper
            )

            // Lower lip — moves down with amplitude
            drawPath(
                Path().apply {
                    moveTo(cornerL.x, cornerL.y)
                    cubicTo(
                        cx - halfW * 0.5f, baseY + openAmt,
                        cx + halfW * 0.5f, baseY + openAmt,
                        cornerR.x, cornerR.y
                    )
                },
                color = lipColor.copy(alpha = 0.70f),
                style = strokeLower
            )

            // Corner dots
            drawCircle(lipColor, radius = 3.5.dp.toPx(), center = cornerL)
            drawCircle(lipColor, radius = 3.5.dp.toPx(), center = cornerR)
        }
    }
}

