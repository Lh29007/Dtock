#!/bin/sh
# Compile Butterscotch pour Alpine (musl), backend GLFW3 + OpenGL legacy.
#
# Usage : build-butterscotch-musl.sh <variante> <dossier_source> <dossier_sortie>
#   variante : novideo (défaut) | video (ffmpeg pour les cinématiques)
#
# Sorties :
#   <sortie>/logs/apk-<variante>.log
#   <sortie>/logs/cmake-<variante>.log
#   <sortie>/logs/build-<variante>.log
#   <sortie>/butterscotch-musl-<variante>
set -eu

VARIANT="${1:-novideo}"
SRC="${2:-/src}"
OUT="${3:-/out}"

case "$VARIANT" in
  novideo|video) ;;
  *) echo "Variante inconnue : $VARIANT (attendu : novideo | video)" >&2; exit 2 ;;
esac

LOGS="$OUT/logs"
mkdir -p "$LOGS"

echo "[1/4] Dépendances Alpine ($VARIANT)"
apk add --no-cache \
  build-base cmake ninja pkgconf linux-headers \
  glfw-dev mesa-dev \
  > "$LOGS/apk-$VARIANT.log" 2>&1

VIDEO_BACKEND=none
if [ "$VARIANT" = "video" ]; then
  apk add --no-cache ffmpeg-dev >> "$LOGS/apk-$VARIANT.log" 2>&1
  VIDEO_BACKEND=ffmpeg
fi

echo "[2/4] Configuration CMake"
cd "$SRC"
cmake -S . -B "build-$VARIANT" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DPLATFORM=cli \
  -DBACKEND=glfw3 \
  -DENABLE_LEGACY_GL=ON \
  -DENABLE_MODERN_GL=OFF \
  -DENABLE_PHYSICS=OFF \
  -DENABLE_FMOD=OFF \
  -DVIDEO_BACKEND="$VIDEO_BACKEND" \
  > "$LOGS/cmake-$VARIANT.log" 2>&1

echo "[3/4] Compilation"
cmake --build "build-$VARIANT" > "$LOGS/build-$VARIANT.log" 2>&1

echo "[4/4] Strip et copie du binaire"
BIN="build-$VARIANT/butterscotch"
test -f "$BIN" || { echo "Binaire absent : $BIN" >&2; exit 1; }
strip "$BIN"
cp "$BIN" "$OUT/butterscotch-musl-$VARIANT"
ls -la "$OUT/butterscotch-musl-$VARIANT"
file "$OUT/butterscotch-musl-$VARIANT" || true
