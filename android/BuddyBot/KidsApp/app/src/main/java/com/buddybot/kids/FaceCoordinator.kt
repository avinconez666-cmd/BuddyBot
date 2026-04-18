package com.buddybot.kids

import android.content.Context
import android.net.Uri
import android.util.Log
import androidx.media3.common.MediaItem
import androidx.media3.common.Player
import androidx.media3.common.PlaybackException
import androidx.media3.exoplayer.ExoPlayer
import androidx.media3.ui.PlayerView
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

class FaceCoordinator(
    private val context: Context,
    private val playerView: PlayerView,
    private val scope: CoroutineScope
) {
    private var player: ExoPlayer = ExoPlayer.Builder(context).build().apply {
        playerView.player = this
        volume = 1f // Full volume for video playback (intro, splash, animations)
        repeatMode = Player.REPEAT_MODE_ONE
    }

    private var currentMode: RobotMode = RobotMode.NORMAL
    private var isSpeaking: Boolean = false
    private var isTransitioning: Boolean = false
    private var isWarningTriggered: Boolean = false
    private var onCompleteCallback: (() -> Unit)? = null

    init {
        // [REF: Phase 3] Ensure the view is visible only when video actually appears
        player.addListener(object : Player.Listener {
            override fun onRenderedFirstFrame() {
                scope.launch(Dispatchers.Main) {
                    playerView.alpha = 1f 
                }
                Log.d("FaceCoordinator", "Surface ready: First frame rendered")
            }

            override fun onPlaybackStateChanged(playbackState: Int) {
                if (playbackState == Player.STATE_ENDED) {
                    val callback = onCompleteCallback
                    onCompleteCallback = null
                    
                    if (callback != null) {
                        Log.d("FaceCoordinator", "Transition sequence complete")
                        callback()
                    } else if (isTransitioning) {
                        isTransitioning = false
                        playCurrentLoop()
                    } else if (isWarningTriggered) {
                        isWarningTriggered = false
                        playCurrentLoop()
                    }
                }
            }

            override fun onPlayerError(error: PlaybackException) {
                Log.e("FaceCoordinator", "ExoPlayer error: ${error.message}")
                scope.launch(Dispatchers.Main) {
                    playerView.alpha = 1f
                }
                val callback = onCompleteCallback
                onCompleteCallback = null
                callback?.invoke() ?: playCurrentLoop()
            }
        })
    }

    fun setRobotMode(newMode: RobotMode, oldMode: RobotMode, force: Boolean = false) {
        if (!force && currentMode == newMode && !isTransitioning) return
        currentMode = newMode
        
        if (newMode == RobotMode.NORMAL && oldMode != RobotMode.NORMAL) {
            isTransitioning = true
            playVideo("${oldMode.name.lowercase()}_to_normal", loop = false)
        } else {
            playCurrentLoop()
        }
    }

    fun setSpeaking(speaking: Boolean) {
        if (isSpeaking == speaking) return
        isSpeaking = speaking
        if (!isTransitioning && !isWarningTriggered && onCompleteCallback == null) {
            playCurrentLoop()
        }
    }

    fun triggerWarning() {
        if (isWarningTriggered || isTransitioning || onCompleteCallback != null) return
        isWarningTriggered = true
        playVideo("bodyguard_warning", loop = false)
    }

    fun playIntroVideo(onComplete: () -> Unit) {
        Log.d("FaceCoordinator", "Playing intro video")
        onCompleteCallback = onComplete
        // [REF: Recommended Fixes] Use assets for high-stakes intro
        playVideoFromAsset("intro.mp4")
    }

    fun playSplashVideo(onComplete: () -> Unit) {
        onCompleteCallback = onComplete
        playVideo("splash", loop = false)
    }

    fun playVideoOnce(assetName: String, onComplete: () -> Unit) {
        onCompleteCallback = onComplete
        playVideo(assetName, loop = false)
    }

    private fun playCurrentLoop() {
        val modeName = currentMode.name.lowercase()
        val assetName = if (isSpeaking) "${modeName}_talk" else "${modeName}_idle"
        playVideo(assetName, loop = true)
    }

    private fun playVideoFromAsset(fileName: String) {
        scope.launch(Dispatchers.Main) {
            playerView.alpha = 0f
            val mediaItem = MediaItem.fromUri("asset:///$fileName")
            player.setMediaItem(mediaItem)
            player.volume = 1f  // intro.mp4 plays WITH sound
            player.repeatMode = Player.REPEAT_MODE_OFF
            player.prepare()
            player.play()
        }
    }

    private fun playVideo(assetName: String, loop: Boolean) {
        scope.launch(Dispatchers.Main) {
            val resId = context.resources.getIdentifier(assetName, "raw", context.packageName)
            if (resId != 0) {
                playerView.alpha = 0f 
                val uri = Uri.parse("android.resource://${context.packageName}/$resId")
                val mediaItem = MediaItem.fromUri(uri)
                player.setMediaItem(mediaItem)
                // Mute face videos (idle, talk, animations) so ElevenLabs TTS plays over them
                player.volume = 0f
                player.repeatMode = if (loop) Player.REPEAT_MODE_ONE else Player.REPEAT_MODE_OFF
                player.prepare()
                player.play()
            } else {
                Log.e("FaceCoordinator", "Asset missing: $assetName")
                val callback = onCompleteCallback
                onCompleteCallback = null
                if (callback != null) {
                    callback()
                } else {
                    if (assetName != "normal_idle") playVideo("normal_idle", true)
                }
            }
        }
    }

    fun release() {
        player.release()
    }
}
