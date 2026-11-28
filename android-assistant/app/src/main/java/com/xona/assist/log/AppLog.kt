package com.xona.assist.log

import android.content.Context
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Journal local conservé dans un fichier privé (300 lignes maximum).
 * Ne jamais y écrire la clé API.
 */
object AppLog {

    private const val FILE_NAME = "xona_logs.txt"
    private const val MAX_LINES = 300

    private var file: File? = null
    private val lock = Any()
    private val timeFormat = SimpleDateFormat("dd/MM HH:mm:ss", Locale.FRANCE)

    fun init(context: Context) {
        if (file == null) {
            file = File(context.filesDir, FILE_NAME)
        }
    }

    fun i(message: String) = write("INFO", message)

    fun e(message: String) = write("ERREUR", message)

    fun read(): List<String> {
        val f = file ?: return emptyList()
        return synchronized(lock) {
            if (f.exists()) f.readLines().takeLast(MAX_LINES).reversed() else emptyList()
        }
    }

    fun clear() {
        val f = file ?: return
        synchronized(lock) { f.writeText("") }
    }

    private fun write(level: String, message: String) {
        val f = file ?: return
        val line = "${timeFormat.format(Date())} [$level] $message"
        synchronized(lock) {
            val lines = if (f.exists()) f.readLines().toMutableList() else mutableListOf()
            lines += line
            f.writeText(lines.takeLast(MAX_LINES).joinToString("\n", postfix = "\n"))
        }
    }
}
