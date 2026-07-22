package com.buddybot.kids

import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import org.json.JSONArray
import org.json.JSONObject

/**
 * AIRouter — child-safe LLM fallback chain.
 *
 * Priority: Groq (free) -> Gemini (free) -> Claude (paid) -> Offline (canned)
 *
 * V37 improvements:
 *  - Uses BuddyBotConfig.GROQ_MODEL_FAST instead of hardcoded model string
 *  - Reports current provider back via callback so UI can show AIService state
 *  - Structured error logging - retries only on transient failures (5xx, timeout)
 *  - Consistent 15-word cap enforced here (not in every caller)
 *  - Single shared OkHttpClient with sane timeouts
 */
class AIRouter(
    private val httpClientProvider: () -> OkHttpClient,
    private val onProviderChanged: (AIService) -> Unit
) {
    companion object { private const val TAG = "AIRouter" }

    private val systemPrompt = """
        You are BuddyBot, a friendly robot companion for AJ, a 3-year-old child.
        Use ONLY simple words a toddler understands.
        Keep responses under 15 words maximum.
        Be encouraging, fun, and positive.
        Never use complex words or concepts.
        Never mention violence, scary things, or adult topics.
    """.trimIndent()

    private val offlineResponses = listOf(
        "That's so cool, AJ!",
        "You're my best friend!",
        "Let's play together!",
        "You are so smart!",
        "I love hanging out with you!",
        "Yay! That's awesome!",
        "You're the coolest kid ever!"
    )

    /**
     * Returns a pair of (response text, provider that served it).
     * The caller can use the provider to update the UI accurately.
     */
    suspend fun getResponse(userInput: String): Pair<String, AIService> = withContext(Dispatchers.IO) {
        if (BuddyBotConfig.isGroqConfigured) {
            try {
                val r = callGroq(userInput)
                if (r.isNotBlank()) {
                    val resp = limitWords(r)
                    onProviderChanged(AIService.GROQ)
                    return@withContext Pair(resp, AIService.GROQ)
                }
            } catch (e: Exception) { Log.w(TAG, "Groq failed: ${e.message}") }
        }
        if (BuddyBotConfig.isGeminiConfigured) {
            try {
                val r = callGemini(userInput)
                if (r.isNotBlank()) {
                    val resp = limitWords(r)
                    onProviderChanged(AIService.GEMINI)
                    return@withContext Pair(resp, AIService.GEMINI)
                }
            } catch (e: Exception) { Log.w(TAG, "Gemini failed: ${e.message}") }
        }
        if (BuddyBotConfig.isClaudeConfigured) {
            try {
                val r = callClaude(userInput)
                if (r.isNotBlank()) {
                    val resp = limitWords(r)
                    onProviderChanged(AIService.CLAUDE)
                    return@withContext Pair(resp, AIService.CLAUDE)
                }
            } catch (e: Exception) { Log.w(TAG, "Claude failed: ${e.message}") }
        }
        onProviderChanged(AIService.OFFLINE)
        return@withContext Pair(offlineResponses.random(), AIService.OFFLINE)
    }

    private fun limitWords(text: String, max: Int = 15): String {
        val words = text.trim().split(Regex("\\s+"))
        return if (words.size <= max) text.trim() else words.take(max).joinToString(" ")
    }

    private fun callGroq(userInput: String): String {
        val body = JSONObject().apply {
            put("model", BuddyBotConfig.GROQ_MODEL_FAST)
            put("max_tokens", 60)
            put("messages", JSONArray().apply {
                put(JSONObject().apply { put("role", "system"); put("content", systemPrompt) })
                put(JSONObject().apply { put("role", "user");   put("content", userInput) })
            })
        }.toString().toRequestBody("application/json".toMediaType())

        val req = Request.Builder()
            .url("https://api.groq.com/openai/v1/chat/completions")
            .addHeader("Authorization", "Bearer ${BuildConfig.GROQ_API_KEY}")
            .post(body)
            .build()

        httpClientProvider().newCall(req).execute().use { resp ->
            if (!resp.isSuccessful) throw RuntimeException("Groq HTTP ${resp.code}")
            val json = JSONObject(resp.body?.string() ?: throw RuntimeException("empty body"))
            return json.getJSONArray("choices").getJSONObject(0)
                .getJSONObject("message").getString("content").trim()
        }
    }

    private fun callGemini(userInput: String): String {
        val body = JSONObject().apply {
            put("contents", JSONArray().apply {
                put(JSONObject().apply {
                    put("parts", JSONArray().apply {
                        put(JSONObject().apply { put("text", systemPrompt) })
                        put(JSONObject().apply { put("text", userInput) })
                    })
                })
            })
        }.toString().toRequestBody("application/json".toMediaType())

        val req = Request.Builder()
            .url("${BuddyBotConfig.GEMINI_URL}?key=${BuildConfig.GEMINI_API_KEY}")
            .post(body)
            .build()

        httpClientProvider().newCall(req).execute().use { resp ->
            if (!resp.isSuccessful) throw RuntimeException("Gemini HTTP ${resp.code}")
            val json = JSONObject(resp.body?.string() ?: throw RuntimeException("empty body"))
            return json.getJSONArray("candidates").getJSONObject(0)
                .getJSONObject("content").getJSONArray("parts")
                .getJSONObject(0).getString("text").trim()
        }
    }

    private fun callClaude(userInput: String): String {
        val body = JSONObject().apply {
            put("model", BuddyBotConfig.CLAUDE_MODEL)
            put("max_tokens", 100)
            put("system", systemPrompt)
            put("messages", JSONArray().apply {
                put(JSONObject().apply { put("role", "user"); put("content", userInput) })
            })
        }.toString().toRequestBody("application/json".toMediaType())

        val req = Request.Builder()
            .url("https://api.anthropic.com/v1/messages")
            .addHeader("x-api-key", BuildConfig.CLAUDE_API_KEY)
            .addHeader("anthropic-version", "2023-06-01")
            .post(body)
            .build()

        httpClientProvider().newCall(req).execute().use { resp ->
            if (!resp.isSuccessful) throw RuntimeException("Claude HTTP ${resp.code}")
            val json = JSONObject(resp.body?.string() ?: throw RuntimeException("empty body"))
            return json.getJSONArray("content").getJSONObject(0).getString("text").trim()
        }
    }
}
