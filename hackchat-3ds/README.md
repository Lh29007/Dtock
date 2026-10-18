# hack.chat pour Nintendo 3DS

Client minimal de [hack.chat](https://hack.chat) pour la 3DS (homebrew). Il se connecte
au serveur officiel en `wss://hack.chat/chat-ws`, rejoint un salon, affiche les messages
sur l'écran du haut et permet d'écrire avec le clavier logiciel.

Produit : `out/hackchat.3dsx` (format Homebrew Launcher).

## Build (une seule commande)

```bash
chmod +x build.sh
./build.sh
```

Le script :

1. **Sur Termux** : installe `proot-distro` + Debian, copie le projet dedans, lance le build
   là-bas, et ramène `out/hackchat.3dsx` sur ton téléphone (et dans `~/storage/downloads`
   si `termux-setup-storage` a été fait).
2. **Sur Debian/Ubuntu (x86_64 ou aarch64)** : installe les paquets système, puis devkitPro
   (pacman) avec `3ds-dev` et `3ds-mbedtls`, puis compile.

Options : `./build.sh --clean` (recompile de zéro), `./build.sh --help`.

## Installer sur la 3DS

1. Copie `out/hackchat.3dsx` sur la carte SD, dans `/3ds/hackchat/hackchat.3dsx`.
2. Lance le **Homebrew Launcher** (nécessite une 3DS déjà homebrew, ex. via Luma3DS).
3. Choisis « hack.chat ».

## Utilisation

- Saisis un pseudo (lettres, chiffres, `_`), puis un salon (par défaut `programming`).
- **A** : écrire un message, **START** : quitter.

## Ce qui est géré / limites

- Protocole : `join`, `chat`, et réception de `session`, `chat`, `onlineSet`, `onlineAdd`,
  `onlineRemove`, `emote`, `info`, `warn`, `captcha` (affiché, non résolu).
- Pas de commandes de modération, pas de whisper, pas de changement de pseudo/couleur.
- Affichage texte uniquement (console libctru) : ASCII. Les accents UTF-8 passent mais la
  police de la console ne les rend pas toujours bien.
- Le certificat : le script embarque le magasin CA **de Debian** (`romfs/cacert.pem`)
  pour que la 3DS vérifie `hack.chat`. Si Let's Encrypt/la chaîne du site change, relance
  `./build.sh` pour rafraîchir.
- Pas de `.cia` : le script produit le `.3dsx`. Le `.cia` demande `makerom` + une icône
  et un banner ; c'est une étape à part.

## Structure

```
build.sh        script de build (Termux, Debian, devkitPro)
Makefile        règles devkitPro (3ds_rules)
src/main.c      boucle principale, UI console + clavier logiciel
src/ws.c/.h     WebSocket sur TLS (mbedTLS), handshake, trames masquées, ping/pong
src/hc.c/.h     protocole hack.chat (JSON), liste des utilisateurs
romfs/          cacert.pem généré par build.sh
```

## À savoir

Ce code a été vérifié à la compilation (syntaxe contre les headers libctru et mbedTLS),
mais **il n'a pas encore été exécuté sur une vraie 3DS** : le premier test réel
(connexion, handshake, réception) peut révéler des détails à ajuster. Si tu as une erreur
au lancement, note le message affiché à l'écran du haut.
