# hack.chat V2 — client 3DS avec interface graphique

Fichier produit : `hackchat3dsV2.3dsx` (à copier sur la carte SD dans `/3ds/hackchat3dsV2/`).

## Interface

- **Écran du haut** : journal du salon, style proche de hack.chat (pseudo à gauche, message à droite).
  **X** ouvre/ferme le panneau des utilisateurs du salon à droite.
- **Écran du bas** : salon courant et nombre de personnes, liste des salons publics,
  boutons **Actualiser**, **Quitter salon**, **Reconnecter**, et zone de saisie.

## Commandes

| Touche | Action |
|---|---|
| A | écrire un message (clavier natif) |
| X | afficher/masquer les utilisateurs du salon |
| HAUT / BAS | choisir un salon dans la liste |
| Y | rejoindre le salon sélectionné |
| B | quitter le salon courant |
| L / R | faire défiler le journal |
| Écran tactile | toucher un salon pour le rejoindre, les boutons et la zone de saisie |
| START | quitter |

## Build

GitHub Actions : `.github/workflows/build-hackchat-v2.yml` compile et publie
`hackchat3dsV2.3dsx` dans les releases (préfixe `hackchat-v2-build-N`).

## Limites

- Le rendu du texte utilise la police système de la console : la taille est réglée
  pour 320×240 / 400×240, à ajuster après un premier test sur la console.
- Les salons sont listés via la commande `getchannels` du serveur.
- Pas de modération, pas de whispers, pas de changement de pseudo.
- Testé à la compilation seulement ; le comportement réel reste à valider sur la console.
