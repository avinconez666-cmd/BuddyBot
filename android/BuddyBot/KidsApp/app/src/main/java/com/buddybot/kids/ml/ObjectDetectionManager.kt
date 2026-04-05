package com.buddybot.kids.ml

import android.content.Context
import android.util.Log
import com.google.mlkit.vision.common.InputImage
import com.google.mlkit.vision.objects.DetectedObject
import com.google.mlkit.vision.objects.ObjectDetection
import com.google.mlkit.vision.objects.ObjectDetector
import com.google.mlkit.vision.objects.defaults.ObjectDetectorOptions
import kotlinx.coroutines.tasks.await
import org.tensorflow.lite.task.vision.detector.ObjectDetector as TFLiteObjectDetector

/**
 * Object Detection Manager
 * 
 * Uses ML Kit and TFLite for real-time object detection
 */
class ObjectDetectionManager(private val context: Context) {

    companion object {
        private const val TAG = "ObjectDetection"
        private const val MODEL_FILE = "mobilenet_ssd.tflite"
    }

    private val mlKitDetector: ObjectDetector
    private var tfLiteDetector: TFLiteObjectDetector? = null

    init {
        val options = ObjectDetectorOptions.Builder()
            .setDetectorMode(ObjectDetectorOptions.STREAM_MODE)
            .enableMultipleObjects()
            .enableClassification()
            .build()

        mlKitDetector = ObjectDetection.getClient(options)
        
        try {
            val tfLiteOptions = TFLiteObjectDetector.ObjectDetectorOptions.builder()
                .setMaxResults(5)
                .setScoreThreshold(0.5f)
                .build()
            // Note: This requires mobilenet_ssd.tflite in assets
            tfLiteDetector = TFLiteObjectDetector.createFromFileAndOptions(context, MODEL_FILE, tfLiteOptions)
            Log.d(TAG, "TFLite Object detector initialized")
        } catch (e: Exception) {
            Log.e(TAG, "TFLite model not found or error loading: ${e.message}")
        }
        
        Log.d(TAG, "ML Kit Object detector initialized")
    }

    suspend fun detectObjects(image: InputImage): List<DetectedObject> {
        return try {
            mlKitDetector.process(image).await()
        } catch (e: Exception) {
            Log.e(TAG, "Error detecting objects with ML Kit", e)
            emptyList()
        }
    }

    fun close() {
        mlKitDetector.close()
        tfLiteDetector?.close()
    }
}
