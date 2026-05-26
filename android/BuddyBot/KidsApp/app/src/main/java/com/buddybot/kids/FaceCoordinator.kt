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
        // Start PlayerView visible playing normal_idle immediately.
        // WebView stays GONE — it is only used as an invisible audio player
        // for ElevenLabs amplitude analysis, never as the primary face.
        playerView.visibility = android.view.View.VISIBLE
        playerView.alpha = 1f
        startIdleLoop("normal_idle")
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
                    isTransitioning = false
                    when {
                        cb != null -> {
                            Log.d(TAG, "Video ended — running completion callback")
                            cb()
                        }
                        isWarningTriggered -> {
                            isWarningTriggered = false
                            Log.d(TAG, "Warning video ended — resuming idle loop")
                            startIdleLoop(when (currentMode) {
                                RobotMode.NORMAL    -> "normal_idle"
                                RobotMode.DOG       -> "dog_idle"
                                RobotMode.BODYGUARD -> "bodyguard_looking"
                                RobotMode.UNHINGED  -> "unhinged_idle"
                                RobotMode.PARTY     -> "party_transition"
                            })
                        }
                        else -> {
                            Log.d(TAG, "Video ended — no callback, returning to idle")
                            startIdleLoop(when (currentMode) {
                                RobotMode.NORMAL    -> "normal_idle"
                                RobotMode.DOG       -> "dog_idle"
                                RobotMode.BODYGUARD -> "bodyguard_looking"
                                RobotMode.UNHINGED  -> "unhinged_idle"
                                RobotMode.PARTY     -> "party_transition"
                            })
                        }
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
    // ────────────────────────────────────────────────────────────────────────
    //  DISPLAY SWITCHING
    //
    //  PlayerView is ALWAYS the visible face — videos loop full-screen.
    //  WebView is ALWAYS GONE — it exists only as an invisible audio host
    //  for ElevenLabs amplitude analysis. Never shown as a face.
    // ────────────────────────────────────────────────────────────────────────
    private fun showWebViewFace() {
        // Route to idle video — WebView is never the visible face
        Log.d(TAG, "showWebViewFace() -> routing to idle video")
        val idleName = when (currentMode) {
            RobotMode.NORMAL    -> "normal_idle"
            RobotMode.DOG       -> "dog_idle"
            RobotMode.BODYGUARD -> "bodyguard_looking"
            RobotMode.UNHINGED  -> "unhinged_idle"
            RobotMode.PARTY     -> "party_transition"
        }
        val resId = context.resources.getIdentifier(idleName, "raw", context.packageName)
        if (resId != 0) playVideo(idleName, loop = true)
        // else: stay on whatever is currently showing
    }

    private fun showVideoFace() {
        scope.launch(Dispatchers.Main) {
            playerView.visibility = View.VISIBLE
            faceWebView?.visibility = View.GONE
            Log.d(TAG, "showVideoFace: PlayerView visible")
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

        // ── Resolve transition video name ──────────────────────────────────
        val transName: String? = when {
            newMode == RobotMode.NORMAL && oldMode != RobotMode.NORMAL ->
                "${oldMode.name.lowercase()}_to_normal"
            newMode == RobotMode.UNHINGED && oldMode == RobotMode.NORMAL ->
                "normal_to_unhinged"
            newMode != RobotMode.NORMAL ->
                "${newMode.name.lowercase()}_transition"
            else -> null
        }

        val transResId = if (transName != null)
            context.resources.getIdentifier(transName, "raw", context.packageName)
        else 0

        // ── Resolve idle loop video name for this mode ─────────────────────
        // After the transition finishes we loop the idle face for this mode.
        // normal_idle, dog_idle, bodyguard_looking, unhinged_idle, party_transition(loop)
        val idleName = when (newMode) {
            RobotMode.NORMAL    -> "normal_idle"
            RobotMode.DOG       -> "dog_idle"
            RobotMode.BODYGUARD -> "bodyguard_looking"  // scanning animation
            RobotMode.UNHINGED  -> "unhinged_idle"
            RobotMode.PARTY     -> "party_transition"
        }

        if (transResId != 0) {
            Log.d(TAG, "Mode transition: playing $transName then looping $idleName")
            isTransitioning = true
            showVideoFace()
            playVideo(transName!!, loop = false)
            // When the transition ends, onPlaybackStateChanged fires STATE_ENDED
            // → the callback below starts the idle loop for the new mode.
            onCompleteCallback = { startIdleLoop(idleName) }
        } else {
            Log.d(TAG, "No transition video — jumping straight to idle: $idleName")
            startIdleLoop(idleName)
        }
    }

    /**
     * Loops [idleName] full-screen as the resting face for the current mode.
     * The WebView is hidden while the idle video plays.
     * Speaking (ElevenLabs) interrupts the idle with a _talk video, then resumes here.
     */
    fun startIdleLoop(idleName: String) {
        val resId = context.resources.getIdentifier(idleName, "raw", context.packageName)
        if (resId != 0) {
            Log.d(TAG, "Starting idle loop: $idleName")
            showVideoFace()
            playVideo(idleName, loop = true)
        } else {
            // Fallback — idle video missing, use WebView face with mode colours
            Log.w(TAG, "Idle video '$idleName' missing — falling back to WebView")
            setWebViewMode(currentMode)
            showWebViewFace()
        }
    }

    fun setSpeaking(speaking: Boolean) {
        if (isSpeakingState == speaking) return
        isSpeakingState = speaking
        Log.d(TAG, "setSpeaking: $speaking")

        if (speaking) {
            // Play the talk/bark video for this mode while ElevenLabs audio plays
            // through MediaPlayer in MainActivity. The video loops until setSpeaking(false).
            val talkName = when (currentMode) {
                RobotMode.NORMAL    -> "normal_talk"
                RobotMode.DOG       -> "dog_barking"
                RobotMode.BODYGUARD -> "bodyguard_talk"
                RobotMode.UNHINGED  -> "normal_talk"
                RobotMode.PARTY     -> "normal_talk"
            }
            val resId = context.resources.getIdentifier(talkName, "raw", context.packageName)
            if (resId != 0) {
                Log.d(TAG, "Speaking — looping talk video: $talkName")
                showVideoFace()
                // Loop the talk video while speaking — MainActivity calls
                // setSpeaking(false) when MediaPlayer finishes, which stops the loop.
                playVideo(talkName, loop = true)
            } else {
                Log.w(TAG, "Talk video missing for $currentMode — staying on idle")
            }
        } else {
            // Audio finished — stop the talk loop and return to idle
            Log.d(TAG, "Speaking ended — returning to idle loop")
            val idleName = when (currentMode) {
                RobotMode.NORMAL    -> "normal_idle"
                RobotMode.DOG       -> "dog_idle"
                RobotMode.BODYGUARD -> "bodyguard_looking"
                RobotMode.UNHINGED  -> "unhinged_idle"
                RobotMode.PARTY     -> "party_transition"
            }
            startIdleLoop(idleName)
        }
    }

    fun triggerWarning() {
        if (isWarningTriggered || isTransitioning || onCompleteCallback != null) return
        val resId = context.resources.getIdentifier(
            "bodyguard_warning", "raw", context.packageName)
        if (resId != 0) {
            isWarningTriggered = true
            showVideoFace()
            playVideo("bodyguard_warning", loop = false)
            // After warning, resume bodyguard scanning loop
            onCompleteCallback = {
                isWarningTriggered = false
                startIdleLoop("bodyguard_looking")
            }
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
        onCompleteCallback = {
            onComplete()
            // After splash, start normal idle face loop
            startIdleLoop("normal_idle")
        }
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
                                        val uri = Uri.parse(
                        "android.resource://${context.packageName}/$resId")
                    // Explicitly set MIME type — ExoPlayer cannot sniff format
                    // from android.resource:// URIs with no file extension
                    val mediaItem = MediaItem.Builder()
                        .setUri(uri)
                        .setMimeType("video/mp4")
                        .build()
                    player.setMediaItem(mediaItem)
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
