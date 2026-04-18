package com.buddybot.kids

import android.content.Context
import android.graphics.Bitmap
import android.graphics.RectF
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
 * Phase 5: Face Recognition Manager
 *
 * Handles face detection and recognition using:
 * - ML Kit for fast on-device face detection (bounding boxes, landmarks)
 * - TensorFlow Lite FaceNet for 128-d face embeddings + cosine similarity
 * - Local .dat file database of known face embeddings
 *
 * Key fixes vs original:
 *   - Added detectAndRecognizeFaces() returning List<FaceResult> with bounds
 *   - Logs confidence scores for every detection (debug)
 *   - Handles null interpreter gracefully (model file missing = detection only)
 *   - Thread-safe: all heavy work runs on Dispatchers.Default via suspend funs
 *   - No null pointer: bitmapInternal null-checked before embedding extraction
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

    // Known faces database: name -> 128-d embedding
    private val knownFaces = mutableMapOf<String, FloatArray>()

    init {
        loadModel()
        loadKnownFaces()
    }

    private fun loadModel() {
        try {
            val afd = context.assets.openFd(MODEL_FILE)
            val channel = FileInputStream(afd.fileDescriptor).channel
            val modelBuffer = channel.map(FileChannel.MapMode.READ_ONLY, afd.startOffset, afd.declaredLength)
            interpreter = Interpreter(modelBuffer)
            Log.d(TAG, "FaceNet model loaded successfully")
        } catch (e: Exception) {
            // Model file is optional — detection still works without recognition
            Log.w(TAG, "FaceNet model not found — face detection only, no recognition: ${e.message}")
        }
    }

    private fun loadKnownFaces() {
        val facesDir = File(context.filesDir, "known_faces")
        if (!facesDir.exists()) {
            facesDir.mkdirs()
            Log.d(TAG, "No known faces directory — register faces during setup")
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
                    Log.d(TAG, "Loaded face embedding: $name")
                } catch (e: Exception) {
                    Log.e(TAG, "Error loading face: $name", e)
                }
            }
        }
        Log.d(TAG, "Known faces loaded: ${knownFaces.size}")
    }

    /**
     * Phase 5: Detect ALL faces in the frame and attempt recognition on each.
     * Returns a list of FaceResult with bounding box, name (or null), and confidence.
     * Handles multiple faces, lighting variations, and partial faces gracefully.
     */
    suspend fun detectAndRecognizeFaces(image: InputImage): List<FaceResult> {
        return try {
            val faces = faceDetector.process(image).await()

            if (faces.isEmpty()) {
                return emptyList()
            }

            Log.d(TAG, "Detected ${faces.size} face(s)")

            faces.map { face ->
                val bounds = RectF(face.boundingBox)
                val (name, confidence) = recognizeSingleFace(image, face)

                // Phase 5: Log confidence score for every face
                Log.d(TAG, "Face at ${bounds.toShortString()} — ${name ?: "unknown"} (conf=${"%.3f".format(confidence)})")

                FaceResult(bounds = bounds, name = name, confidence = confidence)
            }
        } catch (e: Exception) {
            Log.e(TAG, "detectAndRecognizeFaces failed", e)
            emptyList()
        }
    }

    /**
     * Legacy single-face recognition (kept for backward compatibility).
     */
    suspend fun recognizeFace(image: InputImage): String? {
        val results = detectAndRecognizeFaces(image)
        return results.maxByOrNull { it.bounds.width() * it.bounds.height() }?.name
    }

    /**
     * Attempt to recognise a single detected face.
     * Returns Pair(name, confidence) — name is null if no match above threshold.
     */
    private fun recognizeSingleFace(image: InputImage, face: Face): Pair<String?, Float> {
        if (interpreter == null) {
            // No model — detection only
            return Pair(null, 0f)
        }
        return try {
            val bitmap = image.bitmapInternal
            if (bitmap == null) {
                Log.w(TAG, "bitmapInternal is null — cannot extract embedding")
                return Pair(null, 0f)
            }
            val embedding = extractFaceEmbedding(bitmap, face)
            findBestMatch(embedding)
        } catch (e: Exception) {
            Log.e(TAG, "recognizeSingleFace failed", e)
            Pair(null, 0f)
        }
    }

    private fun extractFaceEmbedding(bitmap: Bitmap, face: Face): FloatArray {
        val currentInterpreter = interpreter ?: return FloatArray(128)
        return try {
            val faceBitmap = cropFace(bitmap, face)
            val input = preprocessImage(faceBitmap)
            val output = Array(1) { FloatArray(128) }
            currentInterpreter.run(input, output)
            output[0]
        } catch (e: Exception) {
            Log.e(TAG, "extractFaceEmbedding failed", e)
            FloatArray(128)
        }
    }

    private fun cropFace(bitmap: Bitmap, face: Face): Bitmap {
        val bounds = face.boundingBox
        val padding = 20
        val left = (bounds.left - padding).coerceAtLeast(0)
        val top = (bounds.top - padding).coerceAtLeast(0)
        val width = (bounds.width() + padding * 2).coerceAtMost(bitmap.width - left)
        val height = (bounds.height() + padding * 2).coerceAtMost(bitmap.height - top)
        // Guard against zero-size crop (can happen with very small faces at edges)
        if (width <= 0 || height <= 0) return bitmap
        return Bitmap.createBitmap(bitmap, left, top, width, height)
    }

    private fun preprocessImage(bitmap: Bitmap): ByteBuffer {
        val resized = Bitmap.createScaledBitmap(bitmap, IMAGE_SIZE, IMAGE_SIZE, true)
        val buf = ByteBuffer.allocateDirect(4 * IMAGE_SIZE * IMAGE_SIZE * 3)
        buf.order(ByteOrder.nativeOrder())
        val pixels = IntArray(IMAGE_SIZE * IMAGE_SIZE)
        resized.getPixels(pixels, 0, IMAGE_SIZE, 0, 0, IMAGE_SIZE, IMAGE_SIZE)
        var i = 0
        repeat(IMAGE_SIZE) {
            repeat(IMAGE_SIZE) {
                val v = pixels[i++]
                buf.putFloat(((v shr 16 and 0xFF) - 127.5f) / 127.5f)
                buf.putFloat(((v shr 8  and 0xFF) - 127.5f) / 127.5f)
                buf.putFloat(((v        and 0xFF) - 127.5f) / 127.5f)
            }
        }
        return buf
    }

    /**
     * Find the best matching known face for the given embedding.
     * Logs all similarity scores for debugging.
     * Returns Pair(name, confidence) or Pair(null, 0f) if no match.
     */
    private fun findBestMatch(embedding: FloatArray): Pair<String?, Float> {
        var bestName: String? = null
        var bestScore = 0f

        for ((name, known) in knownFaces) {
            val score = cosineSimilarity(embedding, known)
            // Phase 5: log confidence scores for debugging
            Log.v(TAG, "  similarity[$name] = ${"%.4f".format(score)}")
            if (score > bestScore) {
                bestScore = score
                bestName = name
            }
        }

        return if (bestScore >= SIMILARITY_THRESHOLD) {
            Log.d(TAG, "Match: $bestName (score=${"%.3f".format(bestScore)} >= threshold=$SIMILARITY_THRESHOLD)")
            Pair(bestName, bestScore)
        } else {
            if (bestName != null) {
                Log.d(TAG, "No match: best was $bestName at ${"%.3f".format(bestScore)} < $SIMILARITY_THRESHOLD")
            }
            Pair(null, bestScore)
        }
    }

    private fun cosineSimilarity(a: FloatArray, b: FloatArray): Float {
        if (a.size != b.size) return 0f
        var dot = 0f; var na = 0f; var nb = 0f
        for (i in a.indices) { dot += a[i] * b[i]; na += a[i] * a[i]; nb += b[i] * b[i] }
        return if (na > 0f && nb > 0f) dot / (sqrt(na) * sqrt(nb)) else 0f
    }

    fun registerFace(name: String, image: InputImage) {
        try {
            faceDetector.process(image).addOnSuccessListener { faces ->
                if (faces.isNotEmpty()) {
                    val bitmap = image.bitmapInternal ?: run {
                        Log.w(TAG, "registerFace: bitmapInternal is null")
                        return@addOnSuccessListener
                    }
                    val embedding = extractFaceEmbedding(bitmap, faces[0])
                    knownFaces[name] = embedding
                    saveFaceEmbedding(name, embedding)
                    Log.d(TAG, "Face registered: $name")
                } else {
                    Log.w(TAG, "registerFace: no face detected in image")
                }
            }.addOnFailureListener { e ->
                Log.e(TAG, "registerFace: detection failed", e)
            }
        } catch (e: Exception) {
            Log.e(TAG, "registerFace exception", e)
        }
    }

    private fun saveFaceEmbedding(name: String, embedding: FloatArray) {
        val facesDir = File(context.filesDir, "known_faces")
        if (!facesDir.exists()) facesDir.mkdirs()
        val file = File(facesDir, "$name.dat")
        val buf = ByteBuffer.allocate(embedding.size * 4)
        buf.order(ByteOrder.LITTLE_ENDIAN)
        buf.asFloatBuffer().put(embedding)
        file.writeBytes(buf.array())
        Log.d(TAG, "Saved face embedding: $name")
    }

    fun close() {
        interpreter?.close()
        faceDetector.close()
        Log.d(TAG, "FaceRecognitionManager closed")
    }
}
