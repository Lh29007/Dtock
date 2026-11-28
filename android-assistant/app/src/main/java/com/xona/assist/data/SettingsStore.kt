package com.xona.assist.data

import android.content.Context
import android.content.SharedPreferences

/**
 * Stockage local des réglages (clé API, modèle, prompt système, options).
 * Tout reste sur l'appareil, dans les SharedPreferences privées de l'application.
 */
class SettingsStore(context: Context) {

    private val prefs: SharedPreferences =
        context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    var apiKey: String
        get() = prefs.getString(KEY_API_KEY, "").orEmpty()
        set(value) {
            prefs.edit().putString(KEY_API_KEY, value.trim()).apply()
        }

    var model: String
        get() = prefs.getString(KEY_MODEL, DEFAULT_MODEL).orEmpty().ifBlank { DEFAULT_MODEL }
        set(value) {
            prefs.edit().putString(KEY_MODEL, value.trim().ifBlank { DEFAULT_MODEL }).apply()
        }

    var systemPrompt: String
        get() = prefs.getString(KEY_SYSTEM_PROMPT, DEFAULT_SYSTEM_PROMPT).orEmpty()
        set(value) {
            prefs.edit().putString(KEY_SYSTEM_PROMPT, value).apply()
        }

    /** Ajoute la date et l'heure actuelles au contexte envoyé au modèle. */
    var includeDateTime: Boolean
        get() = prefs.getBoolean(KEY_DATETIME, true)
        set(value) {
            prefs.edit().putBoolean(KEY_DATETIME, value).apply()
        }

    /** Active la recherche web (plugin "web" d'OpenRouter). */
    var webSearch: Boolean
        get() = prefs.getBoolean(KEY_WEB_SEARCH, false)
        set(value) {
            prefs.edit().putBoolean(KEY_WEB_SEARCH, value).apply()
        }

    companion object {
        const val DEFAULT_MODEL = "openrouter/free"

        const val DEFAULT_SYSTEM_PROMPT =
            "Tu es un assistant personnel sur Android. Sois direct, utile et concis."

        val PRESET_MODELS: List<String> = listOf(
            "openrouter/free",
            "anthropic/claude-haiku-4.5",
            "anthropic/claude-sonnet-4.5",
            "google/gemini-2.5-flash",
            "meta-llama/llama-3.3-70b-instruct:free",
        )

        private const val PREFS_NAME = "xona_settings"
        private const val KEY_API_KEY = "api_key"
        private const val KEY_MODEL = "model"
        private const val KEY_SYSTEM_PROMPT = "system_prompt"
        private const val KEY_DATETIME = "include_datetime"
        private const val KEY_WEB_SEARCH = "web_search"
    }
}
