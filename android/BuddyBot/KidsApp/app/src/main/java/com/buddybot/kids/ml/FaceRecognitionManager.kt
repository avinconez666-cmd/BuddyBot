package com.buddybot.kids.ml

import android.content.Context
import android.graphics.*
import android.util.Log
import com.google.mlkit.vision.common.InputImage
import com.google.mlkit.vision.face.Face
import com.google.mlkit.vision.face.FaceDetection
import com.google.mlkit.vision.face.FaceDetectorOptions
import kotlinx.coroutines.tasks.await
import org.tensorflow.lite.Interpreter
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileInputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.channels.FileChannel
import kotlin.math.sqrt

/**
 * Result returned by detectAndRecognizeFaces().
 */
data class FaceResult(
    val bounds: Rect,
    val name: String?
)

/**
 * Face Recognition Manager
 *
 * Handles face detection via ML Kit and recognition via TFLite (FaceNet).
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

    private val knownFaces = mutableMapOf<String, FloatArray>()

    init {
        loadModel()
        loadKnownFaces()
    }

    private fun loadModel() {
        try {
            val afd = context.assets.openFd(MODEL_FILE)
            val fileChannel = FileInputStream(afd.fileDescriptor).channel
            val modelBuffer = fileChannel.map(FileChannel.MapMode.READ_ONLY, afd.startOffset, afd.declaredLength)
            interpreter = Interpreter(modelBuffer)
            Log.d(TAG, "Face recognition model loaded")
        } catch (e: Exception) {
            Log.e(TAG, "Model load error", e)
        }
    }

    private fun loadKnownFaces() {
        val facesDir = File(context.filesDir, "known_faces")
        if (!facesDir.exists()) { facesDir.mkdirs(); return }
        facesDir.listFiles()?.forEach { file ->
            if (file.name.endsWith(".dat")) {
                try {
                    val bytes = file.readBytes()
                    val floats = FloatArray(bytes.size / 4)
                    ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).asFloatBuffer().get(floats)
                    knownFaces[file.name.removeSuffix(".dat")] = floats
                } catch (e: Exception) {
                    Log.e(TAG, "Could not load face file: ${file.name}", e)
                }
            }
        }
    }

    suspend fun detectAndRecognizeFaces(image: InputImage): List<FaceResult> {
        return try {
            val faces = faceDetector.process(image).await()
            if (faces.isEmpty()) return emptyList()
            
            val bitmap = imageToBitmap(image) ?: return faces.map { FaceResult(it.boundingBox, null) }
            
            faces.map { face ->
                val embedding = extractFaceEmbedding(bitmap, face)
                val name = findMatch(embedding)
                FaceResult(bounds = face.boundingBox, name = name)
            }
        } catch (e: Exception) {
            Log.e(TAG, "detectAndRecognizeFaces error", e)
            emptyList()
        }
    }

    private fun extractFaceEmbedding(bitmap: Bitmap, face: Face): FloatArray {
        val interp = interpreter ?: return FloatArray(128)
        return try {
            val faceBitmap = cropFace(bitmap, face)
            val input = preprocessImage(faceBitmap)
            val output = Array(1) { FloatArray(128) }
            interp.run(input, output)
            output[0]
        } catch (e: Exception) {
            Log.e(TAG, "Embedding extraction error", e)
            FloatArray(128)
        }
    }

    private fun cropFace(bitmap: Bitmap, face: Face): Bitmap {
        val b = face.boundingBox
        val padding = 20
        val left = (b.left - padding).coerceAtLeast(0)
        val top = (b.top - padding).coerceAtLeast(0)
        val width = (b.width() + padding * 2).coerceAtMost(bitmap.width - left)
        val height = (b.height() + padding * 2).coerceAtMost(bitmap.height - top)
        return Bitmap.createBitmap(bitmap, left, top, width, height)
    }

    private fun preprocessImage(bitmap: Bitmap): ByteBuffer {
        val scaled = Bitmap.createScaledBitmap(bitmap, IMAGE_SIZE, IMAGE_SIZE, true)
        val buf = ByteBuffer.allocateDirect(4 * IMAGE_SIZE * IMAGE_SIZE * 3).order(ByteOrder.nativeOrder())
        val pixels = IntArray(IMAGE_SIZE * IMAGE_SIZE)
        scaled.getPixels(pixels, 0, IMAGE_SIZE, 0, 0, IMAGE_SIZE, IMAGE_SIZE)
        for (pixel in pixels) {
            buf.putFloat(((pixel shr 16 and 0xFF) - 127.5f) / 127.5f)
            buf.putFloat(((pixel shr 8 and 0xFF) - 127.5f) / 127.5f)
            buf.putFloat(((pixel and 0xFF) - 127.5f) / 127.5f)
        }
        return buf
    }

    private fun findMatch(embedding: FloatArray): String? {
        var bestName: String? = null
        var bestScore = 0f
        for ((name, known) in knownFaces) {
            val score = cosineSimilarity(embedding, known)
            if (score > bestScore && score > SIMILARITY_THRESHOLD) { 
                bestScore = score
                bestName = name 
            }
        }
        return bestName
    }

    private fun cosineSimilarity(a: FloatArray, b: FloatArray): Float {
        var dot = 0f; var na = 0f; var nb = 0f
        for (i in a.indices) { dot += a[i] * b[i]; na += a[i] * a[i]; nb += b[i] * b[i] }
        return if (na > 0 && nb > 0) dot / (sqrt(na) * sqrt(nb)) else 0f
    }

    private fun imageToBitmap(image: InputImage): Bitmap? {
        // Handle InputImage from byte array (NV21)
        val byteBuffer = image.byteBuffer ?: return null
        val bytes = if (byteBuffer.hasArray()) {
            byteBuffer.array()
        } else {
            val b = ByteArray(byteBuffer.remaining())
            byteBuffer.duplicate().get(b)
            b
        }
        
        return try {
            val yuvImage = YuvImage(bytes, ImageFormat.NV21, image.width, image.height, null)
            val out = ByteArrayOutputStream()
            yuvImage.compressToJpeg(Rect(0, 0, image.width, image.height), 100, out)
            val imageBytes = out.toByteArray()
            BitmapFactory.decodeByteArray(imageBytes, 0, imageBytes.size)
        } catch (e: Exception) {
            Log.e(TAG, "imageToBitmap conversion error", e)
            null
        }
    }

    fun registerFace(name: String, image: InputImage) {
        val bitmap = imageToBitmap(image) ?: return
        try {
            faceDetector.process(image).addOnSuccessListener { detectedFaces ->
                if (detectedFaces.isNotEmpty()) {
                    val face = detectedFaces[0]
                    val embedding = extractFaceEmbedding(bitmap, face)
                    knownFaces[name] = embedding
                    saveFaceEmbedding(name, embedding)
                    Log.d(TAG, "Face registered for: $name")
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "registerFace error", e)
        }
    }

    private fun saveFaceEmbedding(name: String, embedding: FloatArray) {
        val dir = File(context.filesDir, "known_faces").also { it.mkdirs() }
        val buf = ByteBuffer.allocate(embedding.size * 4).order(ByteOrder.LITTLE_ENDIAN)
        buf.asFloatBuffer().put(embedding)
        File(dir, "$name.dat").writeBytes(buf.array())
    }

    fun close() {
        interpreter?.close()
        faceDetector.close()
    }
}
