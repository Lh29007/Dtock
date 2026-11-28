package com.xona.assist.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.unit.dp
import com.xona.assist.data.SettingsStore
import com.xona.assist.log.AppLog

@Composable
fun SettingsPanel(vm: AssistantViewModel, modifier: Modifier = Modifier) {
    val store = vm.settings

    var apiKey by rememberSaveable { mutableStateOf(store.apiKey) }
    var model by rememberSaveable { mutableStateOf(store.model) }
    var systemPrompt by rememberSaveable { mutableStateOf(store.systemPrompt) }
    var showKey by remember { mutableStateOf(false) }
    var menuOpen by remember { mutableStateOf(false) }
    var status by remember { mutableStateOf("") }
    var logs by remember { mutableStateOf(AppLog.read()) }

    Column(
        modifier = modifier
            .fillMaxWidth()
            .verticalScroll(rememberScrollState()),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("Clé API OpenRouter", style = MaterialTheme.typography.titleSmall)
        OutlinedTextField(
            value = apiKey,
            onValueChange = {
                apiKey = it
                status = ""
            },
            modifier = Modifier.fillMaxWidth(),
            singleLine = true,
            label = { Text("sk-or-…") },
            visualTransformation = if (showKey) VisualTransformation.None else PasswordVisualTransformation(),
        )
        TextButton(onClick = { showKey = !showKey }) {
            Text(if (showKey) "Masquer la clé" else "Afficher la clé")
        }

        Text("Modèle", style = MaterialTheme.typography.titleSmall)
        Box(modifier = Modifier.fillMaxWidth()) {
            OutlinedButton(
                onClick = { menuOpen = true },
                modifier = Modifier.fillMaxWidth(),
            ) {
                Text(model.ifBlank { SettingsStore.DEFAULT_MODEL }, maxLines = 1)
                Spacer(Modifier.width(8.dp))
                Text("▾")
            }
            DropdownMenu(
                expanded = menuOpen,
                onDismissRequest = { menuOpen = false },
            ) {
                SettingsStore.PRESET_MODELS.forEach { preset ->
                    DropdownMenuItem(
                        text = { Text(preset) },
                        onClick = {
                            model = preset
                            menuOpen = false
                            status = ""
                        },
                    )
                }
            }
        }
        OutlinedTextField(
            value = model,
            onValueChange = {
                model = it
                status = ""
            },
            modifier = Modifier.fillMaxWidth(),
            singleLine = true,
            label = { Text("ID de modèle OpenRouter (texte libre)") },
        )

        Text("Prompt système", style = MaterialTheme.typography.titleSmall)
        OutlinedTextField(
            value = systemPrompt,
            onValueChange = {
                systemPrompt = it
                status = ""
            },
            modifier = Modifier.fillMaxWidth(),
            minLines = 3,
        )

        Row(verticalAlignment = Alignment.CenterVertically) {
            Button(onClick = {
                store.apiKey = apiKey
                store.model = model
                store.systemPrompt = systemPrompt
                model = store.model
                AppLog.i("Paramètres enregistrés (modèle : ${store.model})")
                status = "Enregistré"
            }) {
                Text("Enregistrer")
            }
            Spacer(Modifier.width(12.dp))
            Text(status, style = MaterialTheme.typography.bodySmall)
        }

        HorizontalDivider()

        Text("Journal", style = MaterialTheme.typography.titleSmall)
        Row {
            TextButton(onClick = { logs = AppLog.read() }) {
                Text("Rafraîchir")
            }
            TextButton(onClick = {
                AppLog.clear()
                logs = emptyList()
            }) {
                Text("Effacer")
            }
        }
        if (logs.isEmpty()) {
            Text("Aucune entrée.", style = MaterialTheme.typography.bodySmall)
        } else {
            logs.forEach { line ->
                Text(
                    text = line,
                    style = MaterialTheme.typography.bodySmall,
                    fontFamily = FontFamily.Monospace,
                )
            }
        }
    }
}
