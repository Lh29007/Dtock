package com.xona.assist.network

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.flowOn
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import org.json.JSONArray
import org.json.JSONObject
import java.io.IOException
import java.util.concurrent.TimeUnit

data class ChatMessage(val role: String, val content: String)

/**
 * Client OpenRouter compatible OpenAI, en streaming (Server-Sent Events).
 * Chaque fragment de texte est émis dès réception.
 */
object OpenRouterClient {

    private const val ENDPOINT = "https://openrouter.ai/api/v1/chat/completions"
    private val JSON_TYPE = "application/json; charset=utf-8".toMediaType()

    private val http: OkHttpClient = OkHttpClient.Builder()
        .connectTimeout(20, TimeUnit.SECONDS)
        .readTimeout(90, TimeUnit.SECONDS)
        .build()

    fun stream(
        apiKey: String,
        model: String,
        systemPrompt: String,
        history: List<ChatMessage>,
    ): Flow<String> = flow {
        val messages = JSONArray().apply {
            put(JSONObject().put("role", "system").put("content", systemPrompt))
            history.forEach { msg ->
                put(JSONObject().put("role", msg.role).put("content", msg.content))
            }
        }

        val payload = JSONObject()
            .put("model", model)
            .put("stream", true)
            .put("messages", messages)

        val request = Request.Builder()
            .url(ENDPOINT)
            .header("Authorization", "Bearer $apiKey")
            .header("Content-Type", "application/json")
            .header("Accept", "text/event-stream")
            .header("HTTP-Referer", "https://github.com/Lh29007/Dtock")
            .header("X-Title", "XonaAssist")
            .post(payload.toString().toRequestBody(JSON_TYPE))
            .build()

        http.newCall(request).execute().use { response ->
            if (!response.isSuccessful) {
                val details = response.body?.string().orEmpty().take(300)
                throw IOException("HTTP ${response.code} : $details")
            }

            val source = response.body?.source()
                ?: throw IOException("Réponse vide du serveur")

            while (!source.exhausted()) {
                val line = source.readUtf8Line() ?: break
                if (!line.startsWith("data:")) continue

                val data = line.removePrefix("data:").trim()
                if (data == "[DONE]") break
                if (data.isEmpty()) continue

                val json = JSONObject(data)

                json.optJSONObject("error")?.let { err ->
                    throw IOException(err.optString("message", "Erreur OpenRouter"))
                }

                val piece = json.optJSONArray("choices")
                    ?.optJSONObject(0)
                    ?.optJSONObject("delta")
                    ?.optString("content", "")
                    .orEmpty()

                if (piece.isNotEmpty()) emit(piece)
            }
        }
    }.flowOn(Dispatchers.IO)
}
