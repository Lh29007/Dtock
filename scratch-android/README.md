# Scratch Android (armv7a)

Port de l'éditeur Scratch pour téléphone Android 32 bits (armeabi-v7a).

## Principe

- L'interface est **scratch-gui** (LLK, licence BSD-3-Clause), le même éditeur que sur scratch.mit.edu, compilé pendant la CI.
- L'application Android est une coquille minimale (WebView plein écran) qui sert ces fichiers depuis `https://appassets.androidplatform.net/` grâce à `WebViewAssetLoader`.
- Aucun code natif : l'APK fonctionne sur armv7a comme sur les autres architectures, et ne contient aucune bibliothèque `.so`.
- Dépendances ouvertes uniquement : AndroidX WebKit (Apache 2.0), Gradle, AGP.

## Compiler via GitHub Actions

Le workflow `.github/workflows/build-scratch-android-armv7a.yml` :

1. récupère `LLK/scratch-gui` (branche `develop` par défaut, modifiable à la demande) ;
2. compile l'éditeur web ;
3. le copie dans `app/src/main/assets/` ;
4. construit l'APK debug avec Gradle (JDK 17, SDK Android 34) ;
5. vérifie l'APK (aucune bibliothèque native attendue), et publie l'APK comme artefact `scratch-android-armv7a`.

Lancement manuel : onglet **Actions** → *Scratch Android (armv7a)* → *Run workflow*.

## Installer

Télécharger l'artefact, puis installer l'APK sur le téléphone (autoriser les sources inconnues). L'APK est signé avec la clé debug : il est prévu pour un usage personnel, pas pour le Play Store.

## Limites connues (v0.1)

- Non testé sur un appareil physique depuis cette première version : le premier build CI sert de validation.
- L'interface de scratch-gui est conçue pour le bureau ; sur petit écran elle reste dense. Les ajustements d'interface mobile (barre d'outils, taille des blocs) sont l'étape suivante.
- Les costumes et sons de la bibliothèque Scratch nécessitent une connexion Internet.
- Les projets sont sauvegardés dans le stockage local de la WebView : pas encore d'export/import de fichier `.sb3` dans le système Android.
