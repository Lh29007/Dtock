# LH2 Loader (Minecraft 1.8.9)

Ce dossier contient le squelette de LH2 Loader, branche `lh2-loader`.

## Ce qui est fait

- **Installeur Go** (`installer/`) : écrit le profil `1.8.9-LH2` (hérite de 1.8.9), copie `lh2-loader.jar` dans `libraries/` et crée `config/lh2loader.properties`.
- **Réglages Java** (`mod/`) : lecture de `lh2loader.properties` (culling, limite de chunks, vsync, profil OpenGL), bornes et valeurs par défaut vérifiées.
- **CI** (`.github/workflows/build-lh2-loader.yml`) : tests Go et Java, puis build du JAR, de l'installeur (Windows/Linux/macOS) et d'un ZIP pour Android.

## Ce qui n'est PAS encore fait

- Les hooks de rendu (culling réel des chunks, réduction des allocations, limite de cache) : ils dépendent des mappings du client 1.8.9 et doivent être écrits contre le JAR client. Les réglages existent déjà pour les brancher.
- OpenGL 3.3 : le réglage `gl_profile=gl33` est accepté, mais le chemin de rendu 3.3 n'est pas écrit. Le défaut reste OpenGL 2.1 pour rester compatible avec les appareils low-end.
- Compatibilité Amethyst : non testée sur un appareil. Le profil suit le format JSON de version de Minecraft ; il reste à vérifier qu'Amethyst le lit tel quel.

## Installation

PC : `lh2installer -dir <dossier .minecraft> -jar lh2-loader.jar`

Android (Amethyst) : extraire `lh2-loader-amethyst.zip`, puis placer `lh2-loader-1.0.0.jar` dans le dossier `libraries/lh2/lh2-loader/1.0.0/` du dossier de jeu Amethyst (1.8.9 doit y être installé), et créer le profil `versions/1.8.9-LH2/1.8.9-LH2.json` (généré par l'installeur sur PC, à copier).
