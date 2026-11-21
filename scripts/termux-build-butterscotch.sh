#!/data/data/com.termux/files/usr/bin/sh
# Compile Butterscotch directement dans Termux (armv7a, bionic).
#
# Termux a son propre toolchain natif : pas de cross-compilation, pas de NDK.
# Le binaire obtenu tourne dans Termux. L'affichage passe par SDL2 :
# pour voir la fenêtre, il faut l'app termux-x11 (voir README en bas du script).
#
# Usage : sh termux-build-butterscotch.sh
# Sortie : ~/butterscotch-termux-armv7a
set -eu

SRC="$HOME/butterscotch-src"
BUILD="$HOME/butterscotch-build"
OUT="$HOME/butterscotch-termux-armv7a"

echo "[1/4] Paquets Termux"
pkg install -y git cmake make clang pkg-config sdl2

echo "[2/4] Sources Butterscotch"
if [ -d "$SRC/.git" ]; then
  git -C "$SRC" pull --ff-only
else
  git clone --depth 1 https://github.com/MrPowerGamerBR/Butterscotch.git "$SRC"
fi

echo "[3/4] Configuration et compilation (plusieurs dizaines de minutes sur téléphone)"
cmake -S "$SRC" -B "$BUILD" \
  -DCMAKE_BUILD_TYPE=Release \
  -DPLATFORM=cli \
  -DBACKEND=sdl2 \
  -DAUDIO_BACKEND=miniaudio \
  -DVIDEO_BACKEND=none \
  -DENABLE_LEGACY_GL=ON \
  -DENABLE_MODERN_GL=OFF \
  -DENABLE_PHYSICS=OFF \
  -DENABLE_FMOD=OFF
cmake --build "$BUILD" -j"$(nproc)"

echo "[4/4] Copie du binaire"
cp "$BUILD/butterscotch" "$OUT"
chmod +x "$OUT"
"$OUT" --help | head -n 5
echo "OK : $OUT"

# Pour afficher la fenêtre : installer l'app Termux:X11, puis
#   pkg install x11-repo termux-x11-nightly
#   termux-x11 :1 &
#   DISPLAY=:1 $OUT /chemin/vers/data.win
# Le rendu legacy OpenGL demande un GL de bureau ; sur téléphone ARM ce n'est
# pas garanti. Si l'affichage échoue, envoyez le log.
