package com.buddybot.kids

import android.content.Context
import android.graphics.Bitmap
import android.util.Log
import com.google.mlkit.vision.common.InputImage
import com.google.mlkit.vision.face.Face
import com.google.mlkit.vision.face.FaceDetection
import com.google.mlkit.vision.face.FaceDetectorOptions
import kotlinx.coroutines.tasks.await
import org.tensorflow.lite.Interpreter
import java.io.File
import java.io.FileInputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.channels.FileChannel
import kotlin.math.sqrt

/**
 * Face Recognition Manager
 * 
 * Handles face detection and recognition using:
 * - ML Kit for face detection
 * - TensorFlow Lite for face recognition/embedding
 * - Local database of known faces
 */
class FaceRecognitionManager(private val context: Context) {

    companion object {
        private const val TAG = "FaceRecognition"
        private const val MODEL_FILE = "facenet.tflite"
        private const val SIMILARITY_THRESHOLD = 0.6f
        private const val IMAGE_SIZE = 160
    }

    private var interpreter: Interpreter? = null
    private val faceDetector = FaceDetection.getClient(
        FaceDetectorOptions.Builder()
            .setPerformanceMode(FaceDetectorOptions.PERFORMANCE_MODE_FAST)
            .setLandmarkMode(FaceDetectorOptions.LANDMARK_MODE_NONE)
            .setClassificationMode(FaceDetectorOptions.CLASSIFICATION_MODE_NONE)
            .setMinFaceSize(0.15f)
            .build()
    )

    // Known faces database (in production, use Room database)
    private val knownFaces = mutableMapOf<String, FloatArray>()

    init {
        loadModel()
        loadKnownFaces()
    }

    private fun loadModel() {
        try {
            val assetFileDescriptor = context.assets.openFd(MODEL_FILE)
            val fileChannel = FileInputStream(assetFileDescriptor.fileDescriptor).channel
            val startOffset = assetFileDescriptor.startOffset
            val declaredLength = assetFileDescriptor.declaredLength
            
            val modelBuffer = fileChannel.map(FileChannel.MapMode.READ_ONLY, startOffset, declaredLength)
            interpreter = Interpreter(modelBuffer)
            
            Log.d(TAG, "Face recognition model loaded successfully")
        } catch (e: Exception) {
            Log.e(TAG, "Error loading face recognition model", e)
        }
    }

    private fun loadKnownFaces() {
        val facesDir = File(context.filesDir, "known_faces")
        if (!facesDir.exists()) {
            facesDir.mkdirs()
            Log.d(TAG, "No known faces found. Please register faces during setup.")
            return
        }

        facesDir.listFiles()?.forEach { file ->
            if (file.name.endsWith(".dat")) {
                val name = file.name.removeSuffix(".dat")
                try {
                    val bytes = file.readBytes()
                    val floatArray = FloatArray(bytes.size / 4)
                    ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).asFloatBuffer().get(floatArray)
                    knownFaces[name] = floatArray
                    Log.d(TAG, "Loaded face: $name")
                } catch (e: Exception) {
                    Log.e(TAG, "Error loading face: $name", e)
                }
            }
        }
    }

    suspend fun recognizeFace(image: InputImage): String? {
        return try {
            val faces = faceDetector.process(image).await()
            
            if (faces.isEmpty()) {
                return null
            }
            
            val face = faces.maxByOrNull { it.boundingBox.width() * it.boundingBox.height() }
                ?: return null
            
            val embedding = extractFaceEmbedding(image, face)
            
            findMatch(embedding)
        } catch (e: Exception) {
            Log.e(TAG, "Error recognizing face", e)
            null
        }
    }

    private fun extractFaceEmbedding(image: InputImage, face: Face): FloatArray {
        val currentInterpreter = interpreter ?: return FloatArray(128)
        
        try {
            val bitmap = image.bitmapInternal ?: return FloatArray(128)
            val faceBitmap = cropFace(bitmap, face)
            val input = preprocessImage(faceBitmap)
            val output = Array(1) { FloatArray(128) }
            currentInterpreter.run(input, output)
            
            return output[0]
        } catch (e: Exception) {
            Log.e(TAG, "Error extracting face embedding", e)
            return FloatArray(128)
        }
    }

    private fun cropFace(bitmap: Bitmap, face: Face): Bitmap {
        val bounds = face.boundingBox
        val padding = 20
        val left = (bounds.left - padding).coerceAtLeast(0)
        val top = (bounds.top - padding).coerceAtLeast(0)
        val width = (bounds.width() + padding * 2).coerceAtMost(bitmap.width - left)
        val height = (bounds.height() + padding * 2).coerceAtMost(bitmap.height - top)
        
        return Bitmap.createBitmap(bitmap, left, top, width, height)
    }

    private fun preprocessImage(bitmap: Bitmap): ByteBuffer {
        val resizedBitmap = Bitmap.createScaledBitmap(bitmap, IMAGE_SIZE, IMAGE_SIZE, true)
        val inputBuffer = ByteBuffer.allocateDirect(4 * IMAGE_SIZE * IMAGE_SIZE * 3)
        inputBuffer.order(ByteOrder.nativeOrder())
        
        val intValues = IntArray(IMAGE_SIZE * IMAGE_SIZE)
        resizedBitmap.getPixels(intValues, 0, IMAGE_SIZE, 0, 0, IMAGE_SIZE, IMAGE_SIZE)
        
        var pixel = 0
        repeat(IMAGE_SIZE) {
            repeat(IMAGE_SIZE) {
                val value = intValues[pixel++]
                inputBuffer.putFloat(((value shr 16 and 0xFF) - 127.5f) / 127.5f)
                inputBuffer.putFloat(((value shr 8 and 0xFF) - 127.5f) / 127.5f)
                inputBuffer.putFloat(((value and 0xFF) - 127.5f) / 127.5f)
            }
        }
        
        return inputBuffer
    }

    private fun findMatch(embedding: FloatArray): String? {
        var bestMatch: String? = null
        var bestSimilarity = 0f
        
        for ((name, knownEmbedding) in knownFaces) {
            val similarity = cosineSimilarity(embedding, knownEmbedding)
            if (similarity > bestSimilarity && similarity > SIMILARITY_THRESHOLD) {
                bestSimilarity = similarity
                bestMatch = name
            }
        }
        return bestMatch
    }

    private fun cosineSimilarity(a: FloatArray, b: FloatArray): Float {
        var dotProduct = 0f
        var normA = 0f
        var normB = 0f
        for (i in a.indices) {
            dotProduct += a[i] * b[i]
            normA += a[i] * a[i]
            normB += b[i] * b[i]
        }
        return if (normA > 0 && normB > 0) dotProduct / (sqrt(normA) * sqrt(normB)) else 0f
    }

    fun registerFace(name: String, image: InputImage) {
        try {
            faceDetector.process(image).addOnSuccessListener { detectedFaces ->
                if (detectedFaces.isNotEmpty()) {
                    val face = detectedFaces[0]
                    val embedding = extractFaceEmbedding(image, face)
                    knownFaces[name] = embedding
                    saveFaceEmbedding(name, embedding)
                    Log.d(TAG, "Face registered for: $name")
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error registering face", e)
        }
    }

    private fun saveFaceEmbedding(name: String, embedding: FloatArray) {
        val facesDir = File(context.filesDir, "known_faces")
        if (!facesDir.exists()) facesDir.mkdirs()
        
        val faceFile = File(facesDir, "$name.dat")
        val byteBuffer = ByteBuffer.allocate(embedding.size * 4)
        byteBuffer.order(ByteOrder.LITTLE_ENDIAN)
        byteBuffer.asFloatBuffer().put(embedding)
        faceFile.writeBytes(byteBuffer.array())
    }

    fun close() {
        interpreter?.close()
        faceDetector.close()
    }
}
