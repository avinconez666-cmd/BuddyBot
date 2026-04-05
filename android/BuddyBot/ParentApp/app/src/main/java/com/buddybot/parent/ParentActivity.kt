package com.buddybot.parent

import android.os.Bundle
import android.util.Log
import android.view.View
import android.widget.*
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import kotlinx.coroutines.*
import okhttp3.*
import org.json.JSONObject
import java.text.SimpleDateFormat
import java.util.*
import java.util.concurrent.TimeUnit

/**
 * BUDDYBOT PARENT MONITORING APP
 * 
 * Features:
 * - Real-time telemetry from Mega
 * - Manual robot control
 * - Activity log monitoring
 * - Mode switching
 * - Safety alerts
 * - Battery monitoring
 * - GPS tracking
 */

class ParentActivity : AppCompatActivity() {
    
    // UI Components
    private lateinit var connectionStatus: TextView
    private lateinit var batteryStatus: TextView
    private lateinit var temperatureText: TextView
    private lateinit var gpsText: TextView
    private lateinit var modeSpinner: Spinner
    private lateinit var moveForwardBtn: Button
    private lateinit var moveBackBtn: Button
    private lateinit var moveLeftBtn: Button
    private lateinit var moveRightBtn: Button
    private lateinit var stopBtn: Button
    private lateinit var radarCanvas: RadarView
    private lateinit var activityLog: RecyclerView
    private lateinit var hazardWarning: TextView
    
    // Network
    private val client = OkHttpClient.Builder()
        .connectTimeout(10, TimeUnit.SECONDS)
        .readTimeout(30, TimeUnit.SECONDS)
        .writeTimeout(10, TimeUnit.SECONDS)
        .build()
    
    private var webSocket: WebSocket? = null
    
    // State
    private val activityLogAdapter = ActivityLogAdapter()
    private var isConnected = false
    private var currentTelemetry = Telemetry()
    
    companion object {
        private const val TAG = "ParentMonitor"
        private const val MEGA_IP = "192.168.0.10" // REPLACE WITH YOUR MEGA IP
        private const val WEBSOCKET_PORT = 81
    }
    
    // Data Classes
    data class Telemetry(
        var battery12V: Float = 0f,
        var battery8V: Float = 0f,
        var temperature: Float = 0f,
        var humidity: Float = 0f,
        var latitude: Double = 0.0,
        var longitude: Double = 0.0,
        var speed: Float = 0f,
        var satellites: Int = 0,
        var radarAngle: Int = 90,
        var radarDistance: Int = 0,
        var gasLevel: Int = 0,
        var mode: String = "Manual",
        var hazard: Boolean = false,
        var motionDetected: Boolean = false
    )
    
    data class LogEntry(
        val timestamp: Long,
        val type: String,  // INFO, WARNING, DANGER
        val message: String
    )
    
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_parent)
        
        // Initialize UI
        connectionStatus = findViewById(R.id.connectionStatus)
        batteryStatus = findViewById(R.id.batteryStatus)
        temperatureText = findViewById(R.id.temperatureText)
        gpsText = findViewById(R.id.gpsText)
        modeSpinner = findViewById(R.id.modeSpinner)
        moveForwardBtn = findViewById(R.id.moveForwardBtn)
        moveBackBtn = findViewById(R.id.moveBackBtn)
        moveLeftBtn = findViewById(R.id.moveLeftBtn)
        moveRightBtn = findViewById(R.id.moveRightBtn)
        stopBtn = findViewById(R.id.stopBtn)
        radarCanvas = findViewById(R.id.radarCanvas)
        activityLog = findViewById(R.id.activityLog)
        hazardWarning = findViewById(R.id.hazardWarning)
        
        // Setup Activity Log
        activityLog.layoutManager = LinearLayoutManager(this)
        activityLog.adapter = activityLogAdapter
        
        // Setup Mode Spinner
        val modes = arrayOf("Manual", "Auto", "Sentry", "Disco", "Bodyguard", "Autonomous")
        modeSpinner.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, modes)
        modeSpinner.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: AdapterView<*>?, view: View?, position: Int, id: Long) {
                sendCommand("MODE:$position")
            }
            override fun onNothingSelected(parent: AdapterView<*>?) {}
        }
        
        // Setup Control Buttons
        moveForwardBtn.setOnClickListener { sendCommand("MOVE:F") }
        moveBackBtn.setOnClickListener { sendCommand("MOVE:B") }
        moveLeftBtn.setOnClickListener { sendCommand("MOVE:L") }
        moveRightBtn.setOnClickListener { sendCommand("MOVE:R") }
        stopBtn.setOnClickListener { sendCommand("MOVE:S") }
        
        // Setup touch listeners for continuous movement
        setupContinuousMovement(moveForwardBtn, "MOVE:F")
        setupContinuousMovement(moveBackBtn, "MOVE:B")
        setupContinuousMovement(moveLeftBtn, "MOVE:L")
        setupContinuousMovement(moveRightBtn, "MOVE:R")
        
        // Connect to Mega
        connectToMega()
        
        // Start periodic updates
        startPeriodicUpdates()
    }
    
    // ============== WEBSOCKET CONNECTION ==============
    
    private fun connectToMega() {
        connectionStatus.text = "Connecting..."
        connectionStatus.setTextColor(getColor(android.R.color.holo_orange_light))
        
        val request = Request.Builder()
            .url("ws://$MEGA_IP:$WEBSOCKET_PORT")
            .build()
        
        webSocket = client.newWebSocket(request, object : WebSocketListener() {
            override fun onOpen(webSocket: WebSocket, response: Response) {
                Log.d(TAG, "WebSocket connected")
                isConnected = true
                runOnUiThread {
                    connectionStatus.text = "Connected to BuddyBot"
                    connectionStatus.setTextColor(getColor(android.R.color.holo_green_light))
                    addLogEntry("INFO", "Connected to BuddyBot")
                }
            }
            
            override fun onMessage(webSocket: WebSocket, text: String) {
                handleMegaMessage(text)
            }
            
            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                Log.e(TAG, "WebSocket error", t)
                isConnected = false
                runOnUiThread {
                    connectionStatus.text = "Disconnected - Retrying..."
                    connectionStatus.setTextColor(getColor(android.R.color.holo_red_light))
                    addLogEntry("WARNING", "Connection lost: ${t.message}")
                }
                // Retry after 5 seconds
                lifecycleScope.launch {
                    delay(5000)
                    connectToMega()
                }
            }
            
            override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
                Log.d(TAG, "WebSocket closed: $reason")
                isConnected = false
                lifecycleScope.launch {
                    delay(5000)
                    connectToMega()
                }
            }
        })
    }
    
    private fun sendCommand(command: String) {
        if (isConnected) {
            webSocket?.send(command)
            addLogEntry("INFO", "Sent: $command")
        } else {
            Toast.makeText(this, "Not connected to BuddyBot", Toast.LENGTH_SHORT).show()
        }
    }
    
    // ============== MESSAGE HANDLING ==============
    
    private fun handleMegaMessage(message: String) {
        Log.d(TAG, "Received: $message")
        
        // Parse message format: "TYPE|DATA|END"
        val parts = message.split("|")
        if (parts.size < 2) return
        
        when (parts[0]) {
            "TELEM" -> parseTelemetry(parts[1])
            "RADAR" -> updateRadar(parts[1])
            "EVENT" -> handleEvent(parts[1])
            "HAZARD" -> handleHazard(parts[1])
        }
    }
    
    private fun parseTelemetry(data: String) {
        try {
            // Format: "12V:8V:TEMP:HUM:LAT:LON:SPD:SATS:GAS:MODE:HAZARD"
            val values = data.split(":")
            if (values.size >= 11) {
                currentTelemetry = Telemetry(
                    battery12V = values[0].toFloat(),
                    battery8V = values[1].toFloat(),
                    temperature = values[2].toFloat(),
                    humidity = values[3].toFloat(),
                    latitude = values[4].toDouble(),
                    longitude = values[5].toDouble(),
                    speed = values[6].toFloat(),
                    satellites = values[7].toInt(),
                    gasLevel = values[8].toInt(),
                    mode = values[9],
                    hazard = values[10] == "1"
                )
                
                runOnUiThread {
                    updateTelemetryDisplay()
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error parsing telemetry", e)
        }
    }
    
    private fun updateRadar(data: String) {
        try {
            // Format: "ANGLE:DISTANCE"
            val parts = data.split(":")
            if (parts.size >= 2) {
                currentTelemetry.radarAngle = parts[0].toInt()
                currentTelemetry.radarDistance = parts[1].toInt()
                
                runOnUiThread {
                    radarCanvas.updateRadar(currentTelemetry.radarAngle, currentTelemetry.radarDistance)
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error parsing radar", e)
        }
    }
    
    private fun handleEvent(event: String) {
        runOnUiThread {
            addLogEntry("INFO", event)
        }
    }
    
    private fun handleHazard(hazardType: String) {
        currentTelemetry.hazard = true
        runOnUiThread {
            hazardWarning.visibility = View.VISIBLE
            hazardWarning.text = "⚠️ HAZARD: $hazardType"
            addLogEntry("DANGER", "Hazard detected: $hazardType")
        }
        
        // Auto-hide after 10 seconds
        lifecycleScope.launch {
            delay(10000)
            runOnUiThread {
                hazardWarning.visibility = View.GONE
                currentTelemetry.hazard = false
            }
        }
    }
    
    // ============== UI UPDATES ==============
    
    private fun updateTelemetryDisplay() {
        // Battery Status
        val batteryColor = when {
            currentTelemetry.battery12V < 11.5f -> android.R.color.holo_red_light
            currentTelemetry.battery12V < 12.0f -> android.R.color.holo_orange_light
            else -> android.R.color.holo_green_light
        }
        batteryStatus.text = "🔋 12V: ${String.format("%.1f", currentTelemetry.battery12V)}V | 8V: ${String.format("%.1f", currentTelemetry.battery8V)}V"
        batteryStatus.setTextColor(getColor(batteryColor))
        
        // Temperature & Humidity
        temperatureText.text = "🌡️ ${String.format("%.1f", currentTelemetry.temperature)}°C | 💧 ${String.format("%.0f", currentTelemetry.humidity)}%"
        
        // GPS
        if (currentTelemetry.satellites > 0) {
            gpsText.text = "📍 ${String.format("%.6f", currentTelemetry.latitude)}, ${String.format("%.6f", currentTelemetry.longitude)} | 🛰️ ${currentTelemetry.satellites} sats"
        } else {
            gpsText.text = "📍 GPS searching..."
        }
        
        // Hazard Warning
        if (currentTelemetry.hazard && hazardWarning.visibility != View.VISIBLE) {
            hazardWarning.visibility = View.VISIBLE
        }
    }
    
    private fun addLogEntry(type: String, message: String) {
        val entry = LogEntry(
            timestamp = System.currentTimeMillis(),
            type = type,
            message = message
        )
        activityLogAdapter.addEntry(entry)
    }
    
    // ============== CONTINUOUS MOVEMENT ==============
    
    private fun setupContinuousMovement(button: Button, command: String) {
        var moveJob: Job? = null
        
        button.setOnTouchListener { _, event ->
            when (event.action) {
                android.view.MotionEvent.ACTION_DOWN -> {
                    moveJob = lifecycleScope.launch {
                        while (isActive) {
                            sendCommand(command)
                            delay(100)
                        }
                    }
                }
                android.view.MotionEvent.ACTION_UP,
                android.view.MotionEvent.ACTION_CANCEL -> {
                    moveJob?.cancel()
                    sendCommand("MOVE:S")
                }
            }
            false
        }
    }
    
    // ============== PERIODIC UPDATES ==============
    
    private fun startPeriodicUpdates() {
        lifecycleScope.launch {
            while (isActive) {
                delay(1000)
                
                // Request telemetry update
                if (isConnected) {
                    sendCommand("GET_TELEM")
                }
            }
        }
    }
    
    override fun onDestroy() {
        super.onDestroy()
        webSocket?.close(1000, "App closing")
    }
}

// ============== ACTIVITY LOG ADAPTER ==============

class ActivityLogAdapter : RecyclerView.Adapter<ActivityLogAdapter.ViewHolder>() {
    
    private val entries = mutableListOf<ParentActivity.LogEntry>()
    private val dateFormat = SimpleDateFormat("HH:mm:ss", Locale.getDefault())
    
    class ViewHolder(view: View) : RecyclerView.ViewHolder(view) {
        val timeText: TextView = view.findViewById(R.id.logTime)
        val messageText: TextView = view.findViewById(R.id.logMessage)
        val typeIndicator: View = view.findViewById(R.id.typeIndicator)
    }
    
    override fun onCreateViewHolder(parent: android.view.ViewGroup, viewType: Int): ViewHolder {
        val view = android.view.LayoutInflater.from(parent.context)
            .inflate(R.layout.log_entry_item, parent, false)
        return ViewHolder(view)
    }
    
    override fun onBindViewHolder(holder: ViewHolder, position: Int) {
        val entry = entries[position]
        
        holder.timeText.text = dateFormat.format(Date(entry.timestamp))
        holder.messageText.text = entry.message
        
        val color = when (entry.type) {
            "INFO" -> android.R.color.holo_blue_light
            "WARNING" -> android.R.color.holo_orange_light
            "DANGER" -> android.R.color.holo_red_light
            else -> android.R.color.darker_gray
        }
        holder.typeIndicator.setBackgroundColor(holder.itemView.context.getColor(color))
    }
    
    override fun getItemCount() = entries.size
    
    fun addEntry(entry: ParentActivity.LogEntry) {
        entries.add(0, entry)  // Add to top
        if (entries.size > 100) {
            entries.removeAt(entries.size - 1)  // Keep only 100 entries
        }
        notifyItemInserted(0)
    }
}

// ============== CUSTOM RADAR VIEW ==============

class RadarView @JvmOverloads constructor(
    context: android.content.Context,
    attrs: android.util.AttributeSet? = null,
    defStyleAttr: Int = 0
) : View(context, attrs, defStyleAttr) {
    
    private val paint = android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG)
    private var radarAngle = 90
    private var radarDistance = 0
    
    init {
        paint.strokeWidth = 3f
    }
    
    fun updateRadar(angle: Int, distance: Int) {
        radarAngle = angle
        radarDistance = distance
        invalidate()
    }
    
    override fun onDraw(canvas: android.graphics.Canvas) {
        super.onDraw(canvas)
        
        val cx = width / 2f
        val cy = height / 2f
        val radius = minOf(cx, cy) - 20f
        
        // Draw circles
        paint.color = 0xFF00FF00.toInt()
        paint.style = android.graphics.Paint.Style.STROKE
        for (i in 1..3) {
            canvas.drawCircle(cx, cy, radius * i / 3, paint)
        }
        
        // Draw sweep line
        val angleRad = Math.toRadians(radarAngle.toDouble())
        val endX = cx + (Math.cos(angleRad) * radius).toFloat()
        val endY = cy + (Math.sin(angleRad) * radius).toFloat()
        paint.color = 0xFF00FF00.toInt()
        paint.strokeWidth = 5f
        canvas.drawLine(cx, cy, endX, endY, paint)
        
        // Draw obstacle if detected
        if (radarDistance < 100) {
            val distPercent = radarDistance / 100f
            val obstX = cx + (Math.cos(angleRad) * radius * distPercent).toFloat()
            val obstY = cy + (Math.sin(angleRad) * radius * distPercent).toFloat()
            paint.color = 0xFFFF0000.toInt()
            paint.style = android.graphics.Paint.Style.FILL
            canvas.drawCircle(obstX, obstY, 10f, paint)
        }
    }
}
