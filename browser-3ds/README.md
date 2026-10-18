# Navigateur web 3DS (texte)

Navigateur minimal pour Nintendo 3DS : il charge les pages en HTTPS et les affiche en texte.

**Limites honnêtes** : pas de JavaScript, pas de CSS, pas d'images, pas de cookies. Les sites
qui ne fonctionnent qu'avec JavaScript s'afficheront vides. Seul HTTPS est supporté (les
adresses `http://` sont passées en `https://`).

## Commandes

| Touche | Action |
|---|---|
| HAUT / BAS | défiler d'une ligne |
| L / R | défiler d'une page |
| X | saisir une adresse |
| A | ouvrir le lien numéro N (saisie du numéro) |
| B | retour à la page précédente |
| Y | recharger |
| START | quitter |

Les liens sont numérotés `[1]`, `[2]`… dans le texte.

## Build

Le binaire est produit par GitHub Actions (`.github/workflows/build-browser-3ds.yml`) et
publié dans les releases à chaque push sur `main` qui touche ce dossier.

Local (devkitPro installé) : `make` dans ce dossier produit `browser.3dsx`.

## Installation

Copie `browser.3dsx` sur la carte SD dans `/3ds/browser/browser.3dsx`, puis lance-le depuis
le Homebrew Launcher.

## Structure

- `src/http.c` : client HTTPS (mbedTLS), redirections, décodage chunked, résolution d'URL
- `src/html.c` : HTML -> texte (liens, entités, coupure des lignes à 48 colonnes)
- `src/main.c` : interface console, défilement, historique, saisie
