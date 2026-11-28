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

/** Message affiché : role = "user", "assistant" ou "error". */
data class UiMessage(val role: String, val content: String)

class AssistantViewModel(application: Application) : AndroidViewModel(application) {

    val settings = SettingsStore(application)

    val messages = MutableStateFlow<List<UiMessage>>(emptyList())
    val partialText = MutableStateFlow("")
    val listening = MutableStateFlow(false)
    val busy = MutableStateFlow(false)

    private val history = mutableListOf<ChatMessage>()
    private var streamJob: Job? = null

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
        val systemPrompt = settings.systemPrompt.ifBlank { SettingsStore.DEFAULT_SYSTEM_PROMPT }

        history += ChatMessage("user", text)
        addMessage("user", text)
        addMessage("assistant", "")
        busy.value = true

        val answer = StringBuilder()

        streamJob = viewModelScope.launch {
            AppLog.i("Requête vers $model (${history.size} message(s) dans le contexte)")
            try {
                OpenRouterClient
                    .stream(apiKey, model, systemPrompt, history.toList())
                    .collect { piece ->
                        answer.append(piece)
                        replaceLast("assistant", answer.toString())
                    }

                if (answer.isEmpty()) {
                    replaceLast("assistant", "(réponse vide)")
                }
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

    private fun addMessage(role: String, content: String) {
        messages.value = messages.value + UiMessage(role, content)
    }

    private fun replaceLast(role: String, content: String) {
        val list = messages.value.toMutableList()
        if (list.isNotEmpty()) list[list.lastIndex] = UiMessage(role, content)
        messages.value = list
    }

    override fun onCleared() {
        speech.destroy()
        super.onCleared()
    }
}
