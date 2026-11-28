package com.xona.assist.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Button
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.unit.dp

@Composable
fun AssistantScreen(
    vm: AssistantViewModel,
    onDismiss: () -> Unit,
    onMicClick: () -> Unit,
) {
    val messages by vm.messages.collectAsState()
    val partial by vm.partialText.collectAsState()
    val listening by vm.listening.collectAsState()
    val busy by vm.busy.collectAsState()

    var showSettings by rememberSaveable { mutableStateOf(false) }
    var input by rememberSaveable { mutableStateOf("") }
    val listState = rememberLazyListState()
    val keyboard = LocalSoftwareKeyboardController.current

    val lastContent = messages.lastOrNull()?.content
    // Défilement sans animation : une animation par token ferait ramer l'interface.
    LaunchedEffect(messages.size, lastContent) {
        if (messages.isNotEmpty()) listState.scrollToItem(messages.lastIndex)
    }

    fun submit() {
        val text = input.trim()
        if (text.isEmpty()) return
        input = ""
        keyboard?.hide()
        vm.send(text)
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(Color(0x66000000))
            .clickable(onClick = onDismiss),
        contentAlignment = Alignment.BottomCenter,
    ) {
        Surface(
            modifier = Modifier
                .fillMaxWidth()
                .fillMaxHeight(0.85f)
                .clickable(onClick = {}),
            shape = RoundedCornerShape(topStart = 24.dp, topEnd = 24.dp),
            color = MaterialTheme.colorScheme.surface,
        ) {
            Column(modifier = Modifier.fillMaxSize().padding(16.dp)) {

                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        text = "XonaAssist",
                        style = MaterialTheme.typography.titleMedium,
                        modifier = Modifier.weight(1f),
                    )
                    TextButton(onClick = { showSettings = !showSettings }) {
                        Text(if (showSettings) "Retour" else "⚙ Paramètres")
                    }
                    TextButton(onClick = onDismiss) {
                        Text("✕")
                    }
                }

                HorizontalDivider()

                if (showSettings) {
                    SettingsPanel(vm = vm, modifier = Modifier.weight(1f))
                } else {
                    LazyColumn(
                        state = listState,
                        modifier = Modifier
                            .weight(1f)
                            .fillMaxWidth(),
                        verticalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        items(messages, key = { it.id }) { message ->
                            MessageBubble(message)
                        }
                    }

                    if (listening) {
                        Text(
                            text = "🎤 " + partial.ifBlank { "Je vous écoute…" },
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.primary,
                            modifier = Modifier.padding(vertical = 6.dp),
                        )
                    }

                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.Bottom,
                    ) {
                        OutlinedTextField(
                            value = input,
                            onValueChange = { input = it },
                            modifier = Modifier.weight(1f),
                            placeholder = { Text("Tapez votre question…") },
                            maxLines = 4,
                            keyboardOptions = KeyboardOptions(imeAction = ImeAction.Send),
                            keyboardActions = KeyboardActions(onSend = { submit() }),
                        )
                        Spacer(Modifier.width(8.dp))
                        Button(
                            onClick = onMicClick,
                            enabled = !busy,
                        ) {
                            Text(if (listening) "⏹" else "🎤")
                        }
                        Spacer(Modifier.width(8.dp))
                        Button(
                            onClick = { submit() },
                            enabled = input.isNotBlank() && !busy,
                        ) {
                            Text("➤")
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun MessageBubble(message: UiMessage) {
    val isUser = message.role == "user"
    val isError = message.role == "error"
    val background = when {
        isError -> MaterialTheme.colorScheme.errorContainer
        isUser -> MaterialTheme.colorScheme.primaryContainer
        else -> MaterialTheme.colorScheme.surfaceVariant
    }

    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = if (isUser) Arrangement.End else Arrangement.Start,
    ) {
        Surface(
            color = background,
            shape = RoundedCornerShape(16.dp),
            modifier = Modifier.widthIn(max = 320.dp),
        ) {
            Text(
                text = message.content.ifEmpty { "…" },
                style = MaterialTheme.typography.bodyMedium,
                modifier = Modifier.padding(12.dp),
            )
        }
    }
}
