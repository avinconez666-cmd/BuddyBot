package com.buddybot.kids

import android.content.Context
import android.net.Uri
import android.util.Log
import android.view.View
import android.webkit.JavascriptInterface
import android.webkit.WebSettings
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.media3.common.MediaItem
import androidx.media3.common.Player
import androidx.media3.common.PlaybackException
import androidx.media3.exoplayer.ExoPlayer
import androidx.media3.ui.PlayerView
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

/**
 * FaceCoordinator V2 — WebView lipsync + ExoPlayer transitions
 *
 * Rendering strategy:
 *   • Idle / speaking states  → WebView (buddybot_face.html, real-time lipsync)
 *   • Intro / splash videos   → ExoPlayer (WITH audio for intro, muted for splash)
 *   • Mode transition videos  → ExoPlayer (muted, switches back to WebView on end)
 *   • Missing video assets    → silently falls back to WebView face
 */
class FaceCoordinator(
    private val context: Context,
    private val playerView: PlayerView,
    private val scope: CoroutineScope,
    private val faceWebView: WebView? = null
) {
    companion object {
        private const val TAG = "FaceCoordinator"
    }

    // ── ExoPlayer ────────────────────────────────────────────────────────────
    private val player: ExoPlayer = ExoPlayer.Builder(context).build().apply {
        playerView.player = this
        volume = 0f
        repeatMode = Player.REPEAT_MODE_ONE
    }

    // ── State ────────────────────────────────────────────────────────────────
    private var currentMode: RobotMode = RobotMode.NORMAL

    /**
     * Invoked (on the main thread) when the WebView audio element fires 'ended'.
     * MainActivity sets this before calling notifyAudioFile() and uses it to
     * know when ElevenLabs speech has truly finished so isSpeaking can be cleared.
     */
    var onAudioEnded: (() -> Unit)? = null

    private var isSpeakingState: Boolean = false
    private var isTransitioning: Boolean = false
    private var isWarningTriggered: Boolean = false
    private var onCompleteCallback: (() -> Unit)? = null
    private var webViewReady: Boolean = false

    // ────────────────────────────────────────────────────────────────────────
    //  INIT
    // ────────────────────────────────────────────────────────────────────────
    init {
        setupWebView()
        setupPlayerListeners()
        // Start in WebView mode (show face immediately)
        showWebViewFace()
    }

    // ────────────────────────────────────────────────────────────────────────
    //  WEBVIEW SETUP
    // ────────────────────────────────────────────────────────────────────────
    private fun setupWebView() {
        faceWebView ?: return
        faceWebView.settings.apply {
            javaScriptEnabled = true
            mediaPlaybackRequiresUserGesture = false
            allowFileAccess = true
            allowFileAccessFromFileURLs = true
            allowUniversalAccessFromFileURLs = true
            mixedContentMode = WebSettings.MIXED_CONTENT_ALWAYS_ALLOW
            domStorageEnabled = true
            cacheMode = WebSettings.LOAD_NO_CACHE
            builtInZoomControls = false
            displayZoomControls = false
            setSupportZoom(false)
        }
        faceWebView.setBackgroundColor(0x00000000) // transparent
        faceWebView.addJavascriptInterface(WebViewBridge(), "AndroidBridge")
        faceWebView.webViewClient = object : WebViewClient() {
            override fun onPageFinished(view: WebView?, url: String?) {
                webViewReady = true
                Log.d(TAG, "WebView face loaded — setting mode ${currentMode.name}")
                // Apply current mode colours as soon as page is ready
                setWebViewMode(currentMode)
            }
        }
        faceWebView.loadUrl("file:///android_asset/buddybot_face.html")
    }

    /** JavaScript interface — receives callbacks FROM the WebView HTML */
    inner class WebViewBridge {
        @JavascriptInterface
        fun onWebViewReady() {
            Log.d(TAG, "WebView reported ready via JS bridge")
            scope.launch(Dispatchers.Main) {
                webViewReady = true
                setWebViewMode(currentMode)
            }
        }

        /** Called if Wawa-Lipsync is bundled and fires viseme events */
        @JavascriptInterface
        fun onViseme(viseme: String, weight: Float) {
            scope.launch(Dispatchers.Main) {
                faceWebView?.evaluateJavascript(
                    "window.BuddyBot.setViseme('$viseme','$weight')", null)
            }
        }

        /**
         * Called by buddybot_face.html when the <audio> element fires 'ended'.
         * Triggers the onAudioEnded callback so MainActivity knows speech is done.
         */
        @JavascriptInterface
        fun onAudioEnded() {
            scope.launch(Dispatchers.Main) {
                Log.d(TAG, "WebView audio ended — invoking onAudioEnded callback")
                val cb = onAudioEnded
                onAudioEnded = null   // clear so it doesn't fire twice
                cb?.invoke()
            }
        }
    }

    // ────────────────────────────────────────────────────────────────────────
    //  PLAYER LISTENERS
    // ────────────────────────────────────────────────────────────────────────
    private fun setupPlayerListeners() {
        player.addListener(object : Player.Listener {
            override fun onRenderedFirstFrame() {
                scope.launch(Dispatchers.Main) { playerView.alpha = 1f }
                Log.d(TAG, "ExoPlayer: first frame rendered")
            }

            override fun onPlaybackStateChanged(state: Int) {
                if (state == Player.STATE_ENDED) {
                    val cb = onCompleteCallback
                    onCompleteCallback = null
                    when {
                        cb != null -> {
                            Log.d(TAG, "Video ended — running completion callback")
                            cb()
                        }
                        isTransitioning -> {
                            isTransitioning = false
                            Log.d(TAG, "Mode transition video ended — returning to WebView")
                            showWebViewFace()
                        }
                        isWarningTriggered -> {
                            isWarningTriggered = false
                            Log.d(TAG, "Warning video ended — returning to WebView")
                            showWebViewFace()
                        }
                        else -> showWebViewFace()
                    }
                }
            }

            override fun onPlayerError(error: PlaybackException) {
                Log.e(TAG, "ExoPlayer error: ${error.message} — falling back to WebView")
                val cb = onCompleteCallback
                onCompleteCallback = null
                cb?.invoke() ?: showWebViewFace()
            }
        })
    }

    // ────────────────────────────────────────────────────────────────────────
    //  DISPLAY SWITCHING
    // ────────────────────────────────────────────────────────────────────────
    private fun showWebViewFace() {
        scope.launch(Dispatchers.Main) {
            if (faceWebView != null && webViewReady) {
                playerView.visibility = View.GONE
                faceWebView.visibility = View.VISIBLE
                Log.d(TAG, "Showing WebView face")
            } else if (faceWebView == null) {
                // No WebView provided — keep ExoPlayer visible with idle loop
                playerView.visibility = View.VISIBLE
                playVideo("${currentMode.name.lowercase()}_idle", loop = true)
            } else {
                // WebView not ready yet — briefly wait
                scope.launch(Dispatchers.Main) {
                    kotlinx.coroutines.delay(500)
                    if (webViewReady) {
                        playerView.visibility = View.GONE
                        faceWebView.visibility = View.VISIBLE
                    }
                }
            }
        }
    }

    private fun showVideoFace() {
        scope.launch(Dispatchers.Main) {
            playerView.visibility = View.VISIBLE
            playerView.alpha = 0f // will snap visible on onRenderedFirstFrame
            faceWebView?.visibility = View.GONE
            Log.d(TAG, "Showing ExoPlayer face")
        }
    }

    // ────────────────────────────────────────────────────────────────────────
    //  WEBVIEW JAVASCRIPT CALLS
    // ────────────────────────────────────────────────────────────────────────
    private fun setWebViewMode(mode: RobotMode) {
        if (!webViewReady || faceWebView == null) return
        faceWebView.evaluateJavascript(
            "window.BuddyBot.setMode('${mode.name}')", null)
        Log.d(TAG, "WebView mode set to ${mode.name}")
    }

    private fun setWebViewSpeaking(speaking: Boolean) {
        if (!webViewReady || faceWebView == null) return
        faceWebView.evaluateJavascript(
            "window.BuddyBot.setSpeaking($speaking)", null)
    }

    /**
     * Pass the local file path of the ElevenLabs MP3 to the WebView so the
     * amplitude analyser can animate the mouth in sync with speech.
     *
     * Call this just BEFORE MediaPlayer.prepareAsync() in MainActivity.
     */
    fun notifyAudioFile(localFilePath: String) {
        if (!webViewReady || faceWebView == null) return
        val fileUrl = "file://$localFilePath"
        faceWebView.evaluateJavascript(
            "window.BuddyBot.playAudio('$fileUrl')", null)
        Log.d(TAG, "WebView audio URL: $fileUrl")
    }

    /**
     * Set amplitude directly (0.0–1.0) for platforms that capture their own
     * audio amplitude rather than using the WebView audio element.
     */
    fun setAmplitude(amp: Float) {
        if (!webViewReady || faceWebView == null) return
        faceWebView.evaluateJavascript(
            "window.BuddyBot.setAmplitude(${amp.coerceIn(0f, 1f)})", null)
    }

    // ────────────────────────────────────────────────────────────────────────
    //  PUBLIC API
    // ────────────────────────────────────────────────────────────────────────
    fun setRobotMode(newMode: RobotMode, oldMode: RobotMode, force: Boolean = false) {
        if (!force && currentMode == newMode && !isTransitioning) return
        Log.d(TAG, "setRobotMode: $oldMode -> $newMode (force=$force)")
        currentMode = newMode
        setWebViewMode(newMode)

        // ── Resolve which transition video to play ──────────────────────────
        //
        // Priority:
        //   1. Exact out-transition:  dog_to_normal, bodyguard_to_normal, party_to_normal
        //   2. Exact in-transition:   dog_transition, bodyguard_transition, party_transition
        //   3. Special case:          normal_to_unhinged  (normal → unhinged)
        //   4. No video found         → snap directly to WebView idle face

        val transName: String? = when {
            // Going back to NORMAL: prefer the "X_to_normal" exit video
            newMode == RobotMode.NORMAL && oldMode != RobotMode.NORMAL ->
                "${oldMode.name.lowercase()}_to_normal"

            // Going TO UNHINGED from NORMAL: dedicated video
            newMode == RobotMode.UNHINGED && oldMode == RobotMode.NORMAL ->
                "normal_to_unhinged"

            // Going INTO a non-normal mode: use that mode's _transition video
            newMode != RobotMode.NORMAL ->
                "${newMode.name.lowercase()}_transition"

            else -> null
        }

        val resId = if (transName != null)
            context.resources.getIdentifier(transName, "raw", context.packageName)
        else 0

        if (resId != 0) {
            Log.d(TAG, "Playing mode transition video: $transName")
            isTransitioning = true
            showVideoFace()
            playVideo(transName!!, loop = false)
            // After the video ends, playbackStateChanged → showWebViewFace() is called
            // automatically by the existing player listener, which shows the SVG face
            // with the correct mode colours already applied above via setWebViewMode().
        } else {
            Log.d(TAG, "No transition video for $oldMode→$newMode — showing WebView face")
            showWebViewFace()
        }
    }

    fun setSpeaking(speaking: Boolean) {
        if (isSpeakingState == speaking) return
        isSpeakingState = speaking
        setWebViewSpeaking(speaking)
        Log.d(TAG, "setSpeaking: $speaking")
    }

    fun triggerWarning() {
        if (isWarningTriggered || isTransitioning || onCompleteCallback != null) return
        val resId = context.resources.getIdentifier(
            "bodyguard_warning", "raw", context.packageName)
        if (resId != 0) {
            isWarningTriggered = true
            showVideoFace()
            playVideo("bodyguard_warning", loop = false)
        }
    }

    /** Plays intro.mp4 WITH full audio — called when user taps "Yes" on first launch */
    fun playIntroVideo(onComplete: () -> Unit) {
        Log.d(TAG, "playIntroVideo: playing WITH audio")
        onCompleteCallback = onComplete
        showVideoFace()
        playVideoFromAsset("intro.mp4", withAudio = true)
    }

    /** Plays splash video (muted) — called after intro or directly when "No" tapped */
    fun playSplashVideo(onComplete: () -> Unit) {
        Log.d(TAG, "playSplashVideo")
        onCompleteCallback = onComplete
        showVideoFace()
        playVideo("splash", loop = false)
    }

    /**
     * Plays a one-shot video from raw resources (muted).
     * If the resource doesn't exist the callback fires immediately so the app
     * doesn't freeze waiting for a missing animation.
     */
    fun playVideoOnce(assetName: String, onComplete: () -> Unit) {
        val resId = context.resources.getIdentifier(
            assetName, "raw", context.packageName)
        if (resId != 0) {
            Log.d(TAG, "playVideoOnce: $assetName")
            onCompleteCallback = onComplete
            showVideoFace()
            playVideo(assetName, loop = false)
        } else {
            Log.w(TAG, "playVideoOnce: asset '$assetName' missing — running callback directly")
            onComplete()
        }
    }

    // ────────────────────────────────────────────────────────────────────────
    //  INTERNAL PLAYBACK
    // ────────────────────────────────────────────────────────────────────────
    private fun playVideoFromAsset(fileName: String, withAudio: Boolean = false) {
        scope.launch(Dispatchers.Main) {
            try {
                playerView.alpha = 0f
                val mediaItem = MediaItem.fromUri("asset:///$fileName")
                player.setMediaItem(mediaItem)
                player.volume = if (withAudio) 1f else 0f
                player.repeatMode = Player.REPEAT_MODE_OFF
                player.prepare()
                player.play()
                Log.d(TAG, "playVideoFromAsset: $fileName (audio=$withAudio)")
            } catch (e: Exception) {
                Log.e(TAG, "playVideoFromAsset error for $fileName: ${e.message}")
                val cb = onCompleteCallback; onCompleteCallback = null
                cb?.invoke() ?: showWebViewFace()
            }
        }
    }

    private fun playVideo(assetName: String, loop: Boolean) {
        scope.launch(Dispatchers.Main) {
            Log.d(TAG, "playVideo: $assetName (loop=$loop)")
            val resId = context.resources.getIdentifier(
                assetName, "raw", context.packageName)
            if (resId != 0) {
                try {
                    playerView.alpha = 0f
                    val uri = Uri.parse(
                        "android.resource://${context.packageName}/$resId")
                    player.setMediaItem(MediaItem.fromUri(uri))
                    player.volume = 0f   // all in-app face videos are muted
                    player.repeatMode =
                        if (loop) Player.REPEAT_MODE_ONE else Player.REPEAT_MODE_OFF
                    player.prepare()
                    player.play()
                    Log.d(TAG, "playVideo playing: $assetName")
                } catch (e: Exception) {
                    Log.e(TAG, "playVideo error for $assetName: ${e.message}")
                    val cb = onCompleteCallback; onCompleteCallback = null
                    cb?.invoke() ?: showWebViewFace()
                }
            } else {
                Log.e(TAG, "playVideo: asset '$assetName' not found — using WebView")
                val cb = onCompleteCallback; onCompleteCallback = null
                cb?.invoke() ?: showWebViewFace()
            }
        }
    }

    // ────────────────────────────────────────────────────────────────────────
    //  LIFECYCLE
    // ────────────────────────────────────────────────────────────────────────
    fun release() {
        Log.d(TAG, "FaceCoordinator released")
        player.release()
    }
}
