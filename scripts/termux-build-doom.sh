#!/data/data/com.termux/files/usr/bin/sh
# Compile doomgeneric (port X11) directement dans Termux (armv7a, bionic).
# Contrôles AZERTY : Z/Q/S/D = avancer/gauche/reculer/droite, Espace = ouvrir/utiliser,
# Ctrl = tirer, Shift = course. Les flèches restent actives.
#
# Affichage : app Termux:X11 (F-Droid ou GitHub, à ouvrir avant de lancer le jeu).
#
# Usage : sh termux-build-doom.sh
# Sortie : ~/doom/doomgeneric  (+ mettre doom1.wad, ou DOOM.WAD, dans ~/doom)
set -eu

SRC="$HOME/doomgeneric-src"
OUT_DIR="$HOME/doom"
PATCH="$(cd "$(dirname "$0")" && pwd)/doom-azerty-zqsd.patch"

echo "[1/5] Paquets Termux"
pkg install -y git clang make x11-repo
pkg install -y libx11

echo "[2/5] Sources doomgeneric"
if [ -d "$SRC/.git" ]; then
  git -C "$SRC" pull --ff-only
else
  git clone --depth 1 https://github.com/ozkl/doomgeneric.git "$SRC"
fi

echo "[3/5] Patch ZQSD (AZERTY)"
if [ -f "$PATCH" ]; then
  cp "$PATCH" "$SRC/zqsd.patch"
  git -C "$SRC" checkout -- doomgeneric/doomgeneric_xlib.c
  git -C "$SRC" apply zqsd.patch
else
  echo "Patch absent : $PATCH (le jeu sera compilé sans ZQSD)"
fi

echo "[4/5] Compilation"
make -C "$SRC/doomgeneric" -f Makefile

echo "[5/5] Copie"
mkdir -p "$OUT_DIR"
cp "$SRC/doomgeneric/doomgeneric" "$OUT_DIR/doomgeneric"
chmod +x "$OUT_DIR/doomgeneric"
echo "OK : $OUT_DIR/doomgeneric"
echo
echo "Lancer :"
echo "  pkg install termux-x11-nightly   # une seule fois"
echo "  termux-x11 :1 &                  # puis ouvrir l'app Termux:X11"
echo "  cd ~/doom && DISPLAY=:1 ./doomgeneric -iwad doom1.wad"
