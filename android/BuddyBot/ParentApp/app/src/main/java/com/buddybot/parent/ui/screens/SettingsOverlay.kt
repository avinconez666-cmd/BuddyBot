package com.buddybot.parent.ui.screens

import androidx.compose.foundation.*
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.*
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.*
import com.buddybot.parent.ui.components.HudCorners
import com.buddybot.parent.ui.theme.*

@Composable
fun SettingsOverlay(
    currentIp: String,
    currentStream: String,
    onApply: (ip: String, stream: String) -> Unit,
    onDismiss: () -> Unit,
) {
    var ip     by remember { mutableStateOf(currentIp) }
    var stream by remember { mutableStateOf(currentStream) }

    Box(Modifier.fillMaxSize().background(Color.Black.copy(0.82f)).clickable { onDismiss() },
        contentAlignment = Alignment.Center) {
        Box(Modifier.width(400.dp).clickable { /* absorb */ }) {
            Column(
                Modifier.background(Card, RoundedCornerShape(6.dp))
                    .border(BorderStroke(1.dp, Cyan.copy(0.5f)), RoundedCornerShape(6.dp))
                    .padding(24.dp),
                verticalArrangement = Arrangement.spacedBy(14.dp)
            ) {
                Text("⚙  SYSTEM CONFIGURATION", fontSize = 11.sp, color = Cyan,
                    fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold, letterSpacing = 2.sp)

                Column {
                    Text("ESP32 IP ADDRESS", fontSize = 8.sp, color = GrayLight,
                        fontFamily = FontFamily.Monospace, letterSpacing = 2.sp)
                    Spacer(Modifier.height(4.dp))
                    OutlinedTextField(ip, { ip = it }, Modifier.fillMaxWidth(), singleLine = true,
                        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri),
                        colors = settingsFieldColors(),
                        textStyle = LocalTextStyle.current.copy(fontFamily = FontFamily.Monospace, fontSize = 13.sp))
                }

                Column {
                    Text("CAMERA STREAM URL", fontSize = 8.sp, color = GrayLight,
                        fontFamily = FontFamily.Monospace, letterSpacing = 2.sp)
                    Spacer(Modifier.height(4.dp))
                    OutlinedTextField(stream, { stream = it }, Modifier.fillMaxWidth(), singleLine = true,
                        placeholder = { Text("http://IP:8080/video", fontSize = 11.sp,
                            color = GrayDark, fontFamily = FontFamily.Monospace) },
                        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri),
                        colors = settingsFieldColors(),
                        textStyle = LocalTextStyle.current.copy(fontFamily = FontFamily.Monospace, fontSize = 11.sp))
                    Text("Install 'IP Webcam' on the S9 and paste the stream URL here",
                        fontSize = 7.sp, color = GrayDark, fontFamily = FontFamily.Monospace,
                        modifier = Modifier.padding(top = 3.dp))
                }

                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(onClick = { onApply(ip.trim(), stream.trim()) },
                        modifier = Modifier.weight(1f).height(42.dp),
                        colors = ButtonDefaults.buttonColors(containerColor = CyanDeep, contentColor = Cyan),
                        border = BorderStroke(1.dp, Cyan), shape = RoundedCornerShape(4.dp)) {
                        Text("APPLY", fontSize = 10.sp, fontFamily = FontFamily.Monospace,
                            fontWeight = FontWeight.Bold, letterSpacing = 3.sp)
                    }
                    OutlinedButton(onClick = onDismiss,
                        modifier = Modifier.weight(1f).height(42.dp),
                        colors = ButtonDefaults.outlinedButtonColors(contentColor = GrayLight),
                        border = BorderStroke(1.dp, Border), shape = RoundedCornerShape(4.dp)) {
                        Text("CANCEL", fontSize = 10.sp, fontFamily = FontFamily.Monospace,
                            letterSpacing = 3.sp)
                    }
                }
            }
            HudCorners(Cyan)
        }
    }
}

@Composable
private fun settingsFieldColors() = OutlinedTextFieldDefaults.colors(
    focusedBorderColor   = Cyan,    unfocusedBorderColor = Border,
    focusedTextColor     = Cyan,    unfocusedTextColor   = GrayLight,
    cursorColor          = Cyan,
    focusedContainerColor   = Surface,
    unfocusedContainerColor = Surface,
)
