package com.buddybot.kids

import android.app.ActivityManager
import android.content.Context
import android.content.Intent
import android.graphics.Color
import android.graphics.PixelFormat
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.WindowManager
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView

/**
 * CallOverlayManager V2
 *
 * Shows a slim banner at the BOTTOM of the screen while Messenger is running a call.
 * The banner includes an "End Call" button that dismisses the overlay and returns to
 * BuddyBot Kids.
 *
 * Uses TYPE_APPLICATION_OVERLAY (requires SYSTEM_ALERT_WINDOW permission).
 *
 * Lifecycle:
 *   1. show()        → adds window overlay, starts monitoring Messenger
 *   2. callMonitor   → every 2s checks if Messenger is still foreground
 *   3. dismiss()     → removes overlay, returns to BuddyBot
 */
class CallOverlayManager(private val context: Context) {

    private var windowManager: WindowManager? = null
    private var overlayView: LinearLayout? = null
    private val handler = Handler(Looper.getMainLooper())
    var isShowing = false
        private set
    private var callStartTimeMs = 0L

    private val callMonitor = object : Runnable {
        override fun run() {
            val elapsed = System.currentTimeMillis() - callStartTimeMs
            // Wait at least 5 s before checking so Messenger has time to fully open
            if (elapsed > 5_000L && !isMessengerInForeground()) {
                dismiss()
                bringBuddyBotToFront()
            } else {
                handler.postDelayed(this, 2_000L)
            }
        }
    }

    fun show() {
        if (isShowing) return
        windowManager = context.getSystemService(Context.WINDOW_SERVICE) as WindowManager

        // ── Banner layout ─────────────────────────────────────────────────
        val layout = LinearLayout(context).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setBackgroundColor(Color.argb(225, 0, 40, 10))
            setPadding(32, 18, 32, 18)
        }

        val callingText = TextView(context).apply {
            text = "📹  Calling Dad..."
            textSize = 17f
            setTextColor(Color.WHITE)
        }

        val endCallBtn = Button(context).apply {
            text = "✕  End Call"
            textSize = 13f
            setTextColor(Color.WHITE)
            setBackgroundColor(Color.argb(220, 200, 20, 20))
            setPadding(36, 12, 36, 12)
            isAllCaps = false
            setOnClickListener {
                dismiss()
                bringBuddyBotToFront()
            }
        }

        layout.addView(callingText,
            LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
        layout.addView(endCallBtn)

        // ── Window params — slim banner pinned to bottom ──────────────────
        val params = WindowManager.LayoutParams(
            WindowManager.LayoutParams.MATCH_PARENT,
            WindowManager.LayoutParams.WRAP_CONTENT,
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                    WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN or
                    WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL,
            PixelFormat.TRANSLUCENT
        ).apply {
            gravity = Gravity.BOTTOM or Gravity.FILL_HORIZONTAL
        }

        overlayView = layout
        try {
            windowManager?.addView(layout, params)
            isShowing = true
            callStartTimeMs = System.currentTimeMillis()
            handler.postDelayed(callMonitor, 2_000L)
        } catch (e: Exception) {
            // Permission may have been revoked — fail silently
            overlayView = null
            isShowing = false
        }
    }

    fun dismiss() {
        handler.removeCallbacks(callMonitor)
        overlayView?.let {
            try { windowManager?.removeView(it) } catch (e: Exception) { }
        }
        overlayView = null
        isShowing = false
    }

    private fun isMessengerInForeground(): Boolean {
        return try {
            val am = context.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
            @Suppress("DEPRECATION")
            val tasks = am.getRunningTasks(1)
            tasks.isNotEmpty() &&
                    tasks[0].topActivity?.packageName == "com.facebook.orca"
        } catch (e: Exception) {
            false
        }
    }

    private fun bringBuddyBotToFront() {
        try {
            context.startActivity(
                Intent(context, MainActivity::class.java).apply {
                    addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT or
                            Intent.FLAG_ACTIVITY_NEW_TASK)
                })
        } catch (e: Exception) {
            // Already in front — ignore
        }
    }
}
