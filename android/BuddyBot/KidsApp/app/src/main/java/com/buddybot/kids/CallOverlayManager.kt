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
 * CallOverlayManager — Phase 4: Call Daddy overlay
 *
 * Shows a full-screen "Calling Daddy..." overlay on top of Messenger so that
 * a 3-year-old can see the call is in progress and tap "End Call" to return
 * to BuddyBot.
 *
 * Requires SYSTEM_ALERT_WINDOW permission (declared in AndroidManifest.xml).
 * The caller must check Settings.canDrawOverlays(context) before calling show().
 *
 * The overlay monitors whether Messenger is still in the foreground every 2s.
 * When Messenger is dismissed (call ended / user navigated away), the overlay
 * is automatically dismissed and BuddyBot is brought back to the front.
 */
class CallOverlayManager(private val context: Context) {

    private var windowManager: WindowManager? = null
    private var overlayView: LinearLayout? = null
    private val handler = Handler(Looper.getMainLooper())
    private var isShowing = false

    // Polls every 2s to detect when Messenger leaves the foreground
    private val callMonitor = object : Runnable {
        override fun run() {
            if (!isMessengerInForeground()) {
                dismiss()
                bringBuddyBotToFront()
            } else {
                handler.postDelayed(this, 2000)
            }
        }
    }

    fun show() {
        if (isShowing) return
        windowManager = context.getSystemService(Context.WINDOW_SERVICE) as WindowManager

        val layout = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
            setBackgroundColor(Color.argb(200, 0, 0, 0))
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.MATCH_PARENT
            )
        }

        val callingText = TextView(context).apply {
            text = "📞 Calling Daddy..."
            textSize = 32f
            setTextColor(Color.WHITE)
            gravity = Gravity.CENTER
            setPadding(0, 0, 0, 60)
        }

        val endCallButton = Button(context).apply {
            text = "❌ End Call"
            textSize = 24f
            setTextColor(Color.WHITE)
            setBackgroundColor(Color.RED)
            setPadding(60, 40, 60, 40)
            setOnClickListener {
                dismiss()
                bringBuddyBotToFront()
            }
        }

        layout.addView(callingText)
        layout.addView(endCallButton)

        val params = WindowManager.LayoutParams(
            WindowManager.LayoutParams.MATCH_PARENT,
            WindowManager.LayoutParams.MATCH_PARENT,
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                    WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
            PixelFormat.TRANSLUCENT
        )

        overlayView = layout
        try {
            windowManager?.addView(layout, params)
            isShowing = true
            // Start monitoring Messenger foreground state after 2s
            handler.postDelayed(callMonitor, 2000)
        } catch (e: Exception) {
            // Overlay permission may have been revoked — fail silently
            overlayView = null
        }
    }

    fun dismiss() {
        handler.removeCallbacks(callMonitor)
        overlayView?.let {
            try {
                windowManager?.removeView(it)
            } catch (e: Exception) {
                // Already removed — ignore
            }
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
            val intent = Intent(context, MainActivity::class.java).apply {
                addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT or Intent.FLAG_ACTIVITY_NEW_TASK)
            }
            context.startActivity(intent)
        } catch (e: Exception) {
            // Activity may already be in front — ignore
        }
    }
}
