package com.xona.assist

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.material3.MaterialTheme
import com.xona.assist.log.AppLog
import com.xona.assist.ui.AssistantScreen
import com.xona.assist.ui.AssistantViewModel

class MainActivity : ComponentActivity() {

    private val vm: AssistantViewModel by viewModels()

    private val micPermission = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted ->
        if (granted) {
            vm.startListening()
        } else {
            vm.reportError("Permission micro refusée : saisie clavier uniquement.")
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        AppLog.init(applicationContext)

        setContent {
            MaterialTheme {
                AssistantScreen(
                    vm = vm,
                    onDismiss = { finish() },
                    onMicClick = { toggleMic() },
                )
            }
        }

        if (savedInstanceState == null) {
            listenIfPossible()
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        listenIfPossible()
    }

    private fun hasMicPermission(): Boolean =
        checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED

    private fun listenIfPossible() {
        if (vm.busy.value) return
        if (hasMicPermission()) {
            vm.startListening()
        } else {
            micPermission.launch(Manifest.permission.RECORD_AUDIO)
        }
    }

    private fun toggleMic() {
        if (vm.listening.value) {
            vm.stopListening()
        } else if (hasMicPermission()) {
            vm.startListening()
        } else {
            micPermission.launch(Manifest.permission.RECORD_AUDIO)
        }
    }
}
