package com.buddybot.parent

import android.os.Bundle
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.viewModels
import androidx.compose.runtime.*
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.buddybot.parent.data.*
import com.buddybot.parent.ui.screens.*
import com.buddybot.parent.ui.theme.BuddyBotParentTheme
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.launch

// ─── ViewModel ────────────────────────────────────────────────────────────
class MainViewModel : ViewModel() {
    private val repo = BotRepository()

    val telemetry  = repo.telemetry
    val connected  = repo.connected
    val events     = repo.events

    private var currentIp = ""

    fun connect(ip: String) {
        currentIp = ip
        repo.startPolling(ip, viewModelScope)
    }

    fun sendCmd(cmd: String) {
        viewModelScope.launch { repo.sendCmd(currentIp, cmd) }
    }

    override fun onCleared() {
        super.onCleared()
        repo.stopPolling()
    }
}

// ─── Activity ──────────────────────────────────────────────────────────────
class MainActivity : ComponentActivity() {

    private val vm: MainViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Full-screen immersive
        WindowCompat.setDecorFitsSystemWindows(window, false)
        WindowInsetsControllerCompat(window, window.decorView).let {
            it.hide(WindowInsetsCompat.Type.systemBars())
            it.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        setContent {
            BuddyBotParentTheme {
                var connected by remember { mutableStateOf(false) }
                var ip        by remember { mutableStateOf("") }
                var stream    by remember { mutableStateOf("") }
                var showSettings by remember { mutableStateOf(false) }

                val telem by vm.telemetry.collectAsState()
                val isConn by vm.connected.collectAsState()

                if (!connected) {
                    ConnectScreen { espIp, streamUrl ->
                        ip = espIp; stream = streamUrl
                        vm.connect(ip); connected = true
                    }
                } else {
                    DashboardScreen(
                        telem       = telem,
                        connected   = isConn,
                        streamUrl   = stream,
                        events      = vm.events,
                        onCmd       = { vm.sendCmd(it) },
                        onSettings  = { showSettings = true },
                    )

                    if (showSettings) {
                        SettingsOverlay(
                            currentIp     = ip,
                            currentStream = stream,
                            onApply = { newIp, newStream ->
                                ip = newIp; stream = newStream
                                vm.connect(ip); showSettings = false
                            },
                            onDismiss = { showSettings = false }
                        )
                    }
                }
            }
        }
    }
}
