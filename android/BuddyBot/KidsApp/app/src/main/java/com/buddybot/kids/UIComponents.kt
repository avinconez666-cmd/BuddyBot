package com.buddybot.kids

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
import com.jiangdg.ausbc.CameraClient

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
        Box(
            modifier = Modifier
                .align(Alignment.TopStart)
                .padding(16.dp)
                .size(60.dp)
                .background(Color.White.copy(alpha = 0.1f), CircleShape)
                .clickable { onOpenMenu() },
            contentAlignment = Alignment.Center
        ) {
            Icon(Icons.Default.Settings, contentDescription = "Menu", tint = Color.White.copy(alpha = 0.5f))
        }

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

        Box(
            modifier = Modifier
                .align(Alignment.TopEnd)
                .padding(32.dp)
                .size(80.dp)
                .clip(CircleShape)
                .background(Color.Green.copy(alpha = 0.3f))
                .border(4.dp, Color.White, CircleShape)
                .clickable { onCallDaddy() },
            contentAlignment = Alignment.Center
        ) {
            Text("📞", fontSize = 32.sp)
        }

        IconButton(onClick = onEmergency, modifier = Modifier.align(Alignment.BottomEnd).padding(16.dp)) {
            Icon(Icons.Default.Warning, "SOS", tint = Color.Red, modifier = Modifier.size(48.dp))
        }

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
    webcamClient: CameraClient? = null 
) {
    var selectedTab by remember { mutableIntStateOf(0) }
    var ipInput by remember { mutableStateOf(robotState.buddybotIP) }
    var showWebcamPreview by remember { mutableStateOf(false) }

    Surface(
        modifier = Modifier.fillMaxSize(),
        color = MaterialTheme.colorScheme.background.copy(alpha = 0.95f)
    ) {
        Column(modifier = Modifier.padding(16.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("BuddyBot Controls", style = MaterialTheme.typography.headlineMedium, modifier = Modifier.weight(1f))
                IconButton(onClick = onClose) { Icon(Icons.Default.Close, "Close") }
            }

            TabRow(selectedTabIndex = selectedTab) {
                Tab(selected = selectedTab == 0, onClick = { selectedTab = 0 }, text = { Text("Modes") })
                Tab(selected = selectedTab == 1, onClick = { selectedTab = 1 }, text = { Text("Webcam") })
                Tab(selected = selectedTab == 2, onClick = { selectedTab = 2 }, text = { Text("Network") })
                Tab(selected = selectedTab == 3, onClick = { selectedTab = 3 }, text = { Text("Logs") })
            }

            when (selectedTab) {
                0 -> {
                    LazyColumn(modifier = Modifier.padding(top = 16.dp)) {
                        items(RobotMode.values().toList()) { mode ->
                            Button(onClick = { onModeChange(mode) }, modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp), enabled = mode != robotState.currentMode) {
                                Text(mode.name)
                            }
                        }
                    }
                }
                1 -> {
                    Column(modifier = Modifier.fillMaxWidth().padding(top = 16.dp)) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Text("Real-time Webcam Processing", modifier = Modifier.weight(1f))
                            Switch(checked = showWebcamPreview, onCheckedChange = { showWebcamPreview = it })
                        }
                        
                        if (showWebcamPreview && webcamClient != null) {
                            Box(modifier = Modifier.fillMaxWidth().height(240.dp).clip(RoundedCornerShape(12.dp)).background(Color.Black)) {
                                AndroidView(
                                    factory = { context ->
                                        com.jiangdg.ausbc.widget.AspectRatioTextureView(context).apply {
                                            webcamClient.openCamera(this)
                                        }
                                    },
                                    modifier = Modifier.fillMaxSize()
                                )
                            }
                        } else if (showWebcamPreview) {
                            Text("Webcam not connected", color = Color.Red)
                        }
                        
                        Spacer(modifier = Modifier.height(16.dp))
                        Text("Motor Overrides:", fontWeight = FontWeight.Bold)
                        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            Button(onClick = { onMotorCommand("MOTOR|F") }) { Text("F") }
                            Button(onClick = { onMotorCommand("MOTOR|L") }) { Text("L") }
                            Button(onClick = { onMotorCommand("MOTOR|S") }) { Text("S") }
                            Button(onClick = { onMotorCommand("MOTOR|R") }) { Text("R") }
                            Button(onClick = { onMotorCommand("MOTOR|B") }) { Text("B") }
                        }
                    }
                }
                2 -> {
                    Column(modifier = Modifier.padding(top = 16.dp)) {
                        TextField(value = ipInput, onValueChange = { ipInput = it }, label = { Text("WebSocket IP") }, modifier = Modifier.fillMaxWidth())
                        Button(onClick = { onIPChange(ipInput) }) { Text("Save IP") }
                    }
                }
                3 -> {
                    LazyColumn(modifier = Modifier.fillMaxWidth().padding(top = 16.dp)) {
                        items(logs) { log -> Text(text = log, fontSize = 10.sp, color = Color.Gray, modifier = Modifier.padding(vertical = 1.dp)) }
                    }
                }
            }
        }
    }
}

@Composable
fun PasscodeDialog(correctPasscode: String, onConfirm: () -> Unit, onDismiss: () -> Unit) {
    var text by remember { mutableStateOf("") }
    var error by remember { mutableStateOf(false) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Exit Authentication") },
        text = {
            Column {
                TextField(value = text, onValueChange = { text = it; error = false }, visualTransformation = PasswordVisualTransformation(), keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number), isError = error, modifier = Modifier.fillMaxWidth())
                if (error) Text("Incorrect passcode", color = Color.Red, fontSize = 12.sp)
            }
        },
        confirmButton = { Button(onClick = { if (text == correctPasscode) onConfirm() else error = true }) { Text("Exit") } },
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
