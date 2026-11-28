package com.xona.assist.speech

import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.speech.RecognitionListener
import android.speech.RecognizerIntent
import android.speech.SpeechRecognizer

/**
 * Reconnaissance vocale native d'Android en français, préférence hors ligne.
 * Doit être utilisé depuis le thread principal.
 */
class SpeechInput(
    private val context: Context,
    private val onPartialText: (String) -> Unit,
    private val onFinalText: (String) -> Unit,
    private val onErrorText: (String) -> Unit,
    private val onListeningChanged: (Boolean) -> Unit,
) {

    private var recognizer: SpeechRecognizer? = null

    fun isAvailable(): Boolean = SpeechRecognizer.isRecognitionAvailable(context)

    fun start() {
        val sr = recognizer ?: SpeechRecognizer.createSpeechRecognizer(context).also {
            it.setRecognitionListener(listener)
            recognizer = it
        }
        sr.startListening(buildIntent())
        onListeningChanged(true)
    }

    fun stop() {
        recognizer?.stopListening()
        onListeningChanged(false)
    }

    fun destroy() {
        recognizer?.destroy()
        recognizer = null
    }

    private fun buildIntent(): Intent =
        Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH).apply {
            putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM)
            putExtra(RecognizerIntent.EXTRA_LANGUAGE, "fr-FR")
            putExtra(RecognizerIntent.EXTRA_PREFER_OFFLINE, true)
            putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, true)
            putExtra(RecognizerIntent.EXTRA_MAX_RESULTS, 1)
        }

    private fun messageFor(code: Int): String = when (code) {
        SpeechRecognizer.ERROR_NO_MATCH,
        SpeechRecognizer.ERROR_SPEECH_TIMEOUT -> "Je n'ai rien entendu, réessayez."
        SpeechRecognizer.ERROR_INSUFFICIENT_PERMISSIONS -> "Permission micro manquante."
        13, 14 -> "Pack de langue français hors ligne absent : installez-le dans les réglages de reconnaissance vocale."
        SpeechRecognizer.ERROR_NETWORK,
        SpeechRecognizer.ERROR_NETWORK_TIMEOUT -> "Réseau requis : le pack hors ligne n'est pas disponible."
        SpeechRecognizer.ERROR_RECOGNIZER_BUSY -> "Reconnaissance déjà occupée, réessayez."
        else -> "Erreur de reconnaissance vocale (code $code)."
    }

    private val listener = object : RecognitionListener {
        override fun onReadyForSpeech(params: Bundle?) {}
        override fun onBeginningOfSpeech() {}
        override fun onRmsChanged(rmsdB: Float) {}
        override fun onBufferReceived(buffer: ByteArray?) {}
        override fun onEvent(eventType: Int, params: Bundle?) {}

        override fun onEndOfSpeech() {
            onListeningChanged(false)
        }

        override fun onError(error: Int) {
            onListeningChanged(false)
            onErrorText(messageFor(error))
        }

        override fun onResults(results: Bundle?) {
            onListeningChanged(false)
            val text = results
                ?.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION)
                ?.firstOrNull()
                ?.trim()
                .orEmpty()
            if (text.isNotEmpty()) {
                onFinalText(text)
            } else {
                onErrorText("Je n'ai rien entendu, réessayez.")
            }
        }

        override fun onPartialResults(partialResults: Bundle?) {
            val text = partialResults
                ?.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION)
                ?.firstOrNull()
                .orEmpty()
            if (text.isNotEmpty()) onPartialText(text)
        }
    }
}
