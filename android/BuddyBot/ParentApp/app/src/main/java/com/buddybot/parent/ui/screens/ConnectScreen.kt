package com.buddybot.parent.ui.screens

import androidx.compose.foundation.*
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.*
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.*
import com.buddybot.parent.ui.components.*
import com.buddybot.parent.ui.theme.*

@Composable
fun ConnectScreen(onConnect: (ip: String, streamUrl: String) -> Unit) {
    var ip by remember { mutableStateOf("192.168.1.100") }
    var stream by remember { mutableStateOf("") }

    Box(Modifier.fillMaxSize().background(BgDeep)) {
        Column(
            Modifier.fillMaxSize().padding(40.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center
        ) {
            BuddyBotLogo()
            Spacer(Modifier.height(6.dp))
            ReinsmLogo()
            Spacer(Modifier.height(40.dp))

            // Connect card
            Box(Modifier.width(420.dp)) {
                Column(
                    Modifier.background(Card, RoundedCornerShape(6.dp))
                        .border(BorderStroke(1.dp, Border), RoundedCornerShape(6.dp))
                        .padding(24.dp),
                    verticalArrangement = Arrangement.spacedBy(16.dp)
                ) {
                    Text("CONNECT TO BUDDYBOT", fontSize = 11.sp, color = Cyan,
                        fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold,
                        letterSpacing = 2.sp)

                    Column {
                        Text("ESP32 IP ADDRESS", fontSize = 8.sp, color = GrayLight,
                            fontFamily = FontFamily.Monospace, letterSpacing = 2.sp)
                        Spacer(Modifier.height(4.dp))
                        OutlinedTextField(
                            value = ip, onValueChange = { ip = it },
                            modifier = Modifier.fillMaxWidth(),
                            singleLine = true,
                            placeholder = { Text("192.168.1.100", fontSize = 11.sp,
                                color = GrayDark, fontFamily = FontFamily.Monospace) },
                            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri),
                            colors = OutlinedTextFieldDefaults.colors(
                                focusedBorderColor = Cyan, unfocusedBorderColor = Border,
                                focusedTextColor = Cyan, unfocusedTextColor = GrayLight,
                                cursorColor = Cyan, focusedContainerColor = Surface,
                                unfocusedContainerColor = Surface,
                            ),
                            textStyle = LocalTextStyle.current.copy(
                                fontFamily = FontFamily.Monospace, fontSize = 13.sp)
                        )
                    }

                    Column {
                        Text("CAMERA STREAM URL  (optional)", fontSize = 8.sp, color = GrayLight,
                            fontFamily = FontFamily.Monospace, letterSpacing = 2.sp)
                        Spacer(Modifier.height(4.dp))
                        OutlinedTextField(
                            value = stream, onValueChange = { stream = it },
                            modifier = Modifier.fillMaxWidth(),
                            singleLine = true,
                            placeholder = { Text("http://192.168.1.x:8080/video", fontSize = 11.sp,
                                color = GrayDark, fontFamily = FontFamily.Monospace) },
                            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri),
                            colors = OutlinedTextFieldDefaults.colors(
                                focusedBorderColor = Cyan, unfocusedBorderColor = Border,
                                focusedTextColor = Cyan, unfocusedTextColor = GrayLight,
                                cursorColor = Cyan, focusedContainerColor = Surface,
                                unfocusedContainerColor = Surface,
                            ),
                            textStyle = LocalTextStyle.current.copy(
                                fontFamily = FontFamily.Monospace, fontSize = 11.sp)
                        )
                        Text("Install 'IP Webcam' on the S9 and enter the stream URL",
                            fontSize = 7.sp, color = GrayDark, fontFamily = FontFamily.Monospace,
                            modifier = Modifier.padding(top = 3.dp))
                    }

                    Button(
                        onClick = { if (ip.isNotBlank()) onConnect(ip.trim(), stream.trim()) },
                        modifier = Modifier.fillMaxWidth().height(46.dp),
                        colors = ButtonDefaults.buttonColors(containerColor = CyanDeep,
                            contentColor = Cyan),
                        border = BorderStroke(1.dp, Cyan),
                        shape = RoundedCornerShape(4.dp)
                    ) {
                        Text("▶  CONNECT", fontSize = 12.sp, fontFamily = FontFamily.Monospace,
                            fontWeight = FontWeight.Bold, letterSpacing = 3.sp)
                    }
                }
                HudCorners(Cyan)
            }
        }
    }
}
