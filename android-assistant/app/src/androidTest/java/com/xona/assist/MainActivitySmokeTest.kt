package com.xona.assist

import android.Manifest
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.test.rule.GrantPermissionRule
import org.junit.Rule
import org.junit.Test
import org.junit.rules.RuleChain

/**
 * Test de fumée : l'activité doit démarrer sans planter et afficher l'interface.
 */
class MainActivitySmokeTest {

    private val composeRule = createAndroidComposeRule<MainActivity>()

    @get:Rule
    val chain: RuleChain = RuleChain
        .outerRule(GrantPermissionRule.grant(Manifest.permission.RECORD_AUDIO))
        .around(composeRule)

    @Test
    fun launchesAndShowsMainUi() {
        composeRule.onNodeWithText("XonaAssist").assertIsDisplayed()
        composeRule.onNodeWithText("⚙ Paramètres").assertIsDisplayed()
    }
}
