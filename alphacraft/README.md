# AlphaCraft (3DS)

Prototype de voxel style Minecraft Alpha pour Nintendo 3DS.

- Menu : JOUER (A ou toucher haut) / QUITTER (START ou toucher bas)
- Déplacement : Circle Pad
- Caméra : glisser le doigt sur l'écran du bas
- Croix gauche/droite : changer de bloc (pierre, terre, bois)
- A : sauter · R : poser · L : casser · X : retour au menu · START : quitter

Textures : tuiles 16×16 de l'atlas `terrain.png` de Minecraft Alpha 1.2.6
(colonne 1 ligne 0 = pierre, colonne 2 ligne 0 = terre, colonne 4 ligne 0 = planches).

## Build

- Automatique : GitHub Actions (`.github/workflows/build-alphacraft.yml`) produit `AlphaCraft.3dsx`.
  En cas d'échec, le résumé du run affiche les dernières lignes du log, et le log complet est dans l'artefact `AlphaCraft-logs`.
- Local (devkitPro requis) : `cd alphacraft && make`.

Pour régénérer `source/textures.h` après avoir changé `textures/` : `python3 tools/gen_textures.py`.
