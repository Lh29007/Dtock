package com.xona.assist.ui

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.xona.assist.data.SettingsStore
import com.xona.assist.log.AppLog
import com.xona.assist.network.ChatMessage
import com.xona.assist.network.OpenRouterClient
import com.xona.assist.speech.SpeechInput
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.launch
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/** Message affiché : role = "user", "assistant" ou "error". */
data class UiMessage(val id: Long, val role: String, val content: String)

class AssistantViewModel(application: Application) : AndroidViewModel(application) {

    val settings = SettingsStore(application)

    val messages = MutableStateFlow<List<UiMessage>>(emptyList())
    val partialText = MutableStateFlow("")
    val listening = MutableStateFlow(false)
    val busy = MutableStateFlow(false)

    private val history = mutableListOf<ChatMessage>()
    private var streamJob: Job? = null
    private var nextId = 0L

    private val speech = SpeechInput(
        context = application,
        onPartialText = { partialText.value = it },
        onFinalText = { text ->
            partialText.value = ""
            send(text)
        },
        onErrorText = { msg ->
            partialText.value = ""
            reportError(msg)
        },
        onListeningChanged = { listening.value = it },
    )

    init {
        AppLog.init(application)
    }

    fun startListening() {
        if (!speech.isAvailable()) {
            reportError("Reconnaissance vocale indisponible sur cet appareil.")
            return
        }
        AppLog.i("Écoute vocale démarrée (fr-FR, hors ligne préféré)")
        speech.start()
    }

    fun stopListening() {
        speech.stop()
    }

    fun reportError(message: String) {
        AppLog.e(message)
        addMessage("error", message)
    }

    fun clearChat() {
        streamJob?.cancel()
        history.clear()
        messages.value = emptyList()
        busy.value = false
    }

    fun send(raw: String) {
        val text = raw.trim()
        if (text.isEmpty() || busy.value) return

        val apiKey = settings.apiKey
        if (apiKey.isBlank()) {
            reportError("Clé API OpenRouter manquante : ouvrez les paramètres ⚙.")
            return
        }

        val model = settings.model
        val systemPrompt = buildSystemPrompt()
        val webSearch = settings.webSearch

        history += ChatMessage("user", text)
        addMessage("user", text)
        addMessage("assistant", "")
        busy.value = true

        val answer = StringBuilder()

        streamJob = viewModelScope.launch {
            AppLog.i("Requête vers $model (web=${if (webSearch) "oui" else "non"}, ${history.size} message(s))")
            var lastUiUpdate = 0L
            try {
                OpenRouterClient
                    .stream(apiKey, model, systemPrompt, history.toList(), webSearch)
                    .collect { piece ->
                        answer.append(piece)
                        // Mise à jour de l'écran au plus toutes les 80 ms : évite le lag pendant le streaming.
                        val now = System.currentTimeMillis()
                        if (now - lastUiUpdate >= 80) {
                            lastUiUpdate = now
                            replaceLast("assistant", answer.toString())
                        }
                    }

                replaceLast("assistant", answer.ifEmpty { "(réponse vide)" }.toString())
                history += ChatMessage("assistant", answer.toString())
                AppLog.i("Réponse reçue (${answer.length} caractères) de $model")
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                val message = e.message ?: e.javaClass.simpleName
                AppLog.e("Échec de la requête : $message")
                if (history.isNotEmpty()) history.removeAt(history.lastIndex)
                replaceLast("error", "Erreur : $message")
            } finally {
                busy.value = false
            }
        }
    }

    /** Prompt système, complété par la date et l'heure si l'option est active. */
    private fun buildSystemPrompt(): String {
        val base = settings.systemPrompt.ifBlank { SettingsStore.DEFAULT_SYSTEM_PROMPT }
        if (!settings.includeDateTime) return base
        val now = SimpleDateFormat("EEEE d MMMM yyyy 'à' HH:mm (z)", Locale.FRENCH).format(Date())
        return "$base\n\nDate et heure actuelles sur l'appareil : $now."
    }

    private fun addMessage(role: String, content: String) {
        messages.value = messages.value + UiMessage(nextId++, role, content)
    }

    private fun replaceLast(role: String, content: String) {
        val list = messages.value.toMutableList()
        if (list.isNotEmpty()) {
            val last = list[list.lastIndex]
            list[list.lastIndex] = UiMessage(last.id, role, content)
        }
        messages.value = list
    }

    override fun onCleared() {
        speech.destroy()
        super.onCleared()
    }
}
