package com.buddybot.kids

import android.content.Context
import android.graphics.Bitmap
import android.util.Log
import com.google.mlkit.vision.common.InputImage
import com.google.mlkit.vision.objects.DetectedObject
import com.google.mlkit.vision.objects.ObjectDetection
import com.google.mlkit.vision.objects.ObjectDetector
import com.google.mlkit.vision.objects.defaults.ObjectDetectorOptions
import kotlinx.coroutines.tasks.await

/**
 * ObjectDetectionManager — ML Kit real-time object detection.
 * V37: Rebuilt from stub. Uses ML Kit streaming mode for live camera feed.
 */
class ObjectDetectionManager(private val context: Context) {

    companion object { private const val TAG = "ObjDetect" }

    private val options = ObjectDetectorOptions.Builder()
        .setDetectorMode(ObjectDetectorOptions.STREAM_MODE)
        .enableClassification()
        .enableMultipleObjects()
        .build()

    private val detector: ObjectDetector = ObjectDetection.getClient(options)

    /**
     * Runs object detection on a camera frame (InputImage).
     * Returns ML Kit DetectedObject list — caller maps to DetectedObjectResult.
     */
    suspend fun detectObjects(image: InputImage): List<DetectedObject> {
        return try {
            detector.process(image).await()
        } catch (e: Exception) {
            Log.w(TAG, "Detection failed: ${e.message}")
            emptyList()
        }
    }

    /**
     * Convenience overload for Bitmap input.
     */
    suspend fun detectObjects(bitmap: Bitmap, rotationDegrees: Int = 0): List<DetectedObject> {
        val image = InputImage.fromBitmap(bitmap, rotationDegrees)
        return detectObjects(image)
    }

    fun close() {
        try { detector.close() } catch (e: Exception) { Log.w(TAG, "Close error: ${e.message}") }
    }
}