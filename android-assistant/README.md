# XonaAssist

Assistant Android léger, compatible OpenRouter (streaming), avec saisie vocale hors ligne et mode texte.

## Fonctionnalités

- Ouverture en fenêtre modale (feuille semi-transparente) via `ASSIST` / `VOICE_ASSIST`.
- Reconnaissance vocale native `fr-FR`, préférence hors ligne (`EXTRA_PREFER_OFFLINE`), résultats partiels affichés.
- Saisie texte permanente en bas de la fenêtre.
- Aucune synthèse vocale : les réponses sont uniquement affichées.
- Réponses en streaming via `https://openrouter.ai/api/v1/chat/completions`.
- Paramètres : clé API (mémorisée localement), modèle (menu + ID libre, par défaut `openrouter/free`), prompt système, journal local.

## Compilation

1. Ouvrir le dossier `android-assistant` dans Android Studio.
2. Attendre la synchronisation Gradle, puis `Build > Build APK(s)`.
3. Ou en ligne de commande : `gradle assembleDebug`.

L'APK est généré dans `app/build/outputs/apk/debug/app-debug.apk`.
