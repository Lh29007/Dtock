#!/bin/sh
# Construit le rootfs Ruche (armv7) à partir du minirootfs Alpine officiel.
#
# Exécution : sudo sh ruche/scripts/build-rootfs.sh
# Prérequis : root, et binfmt_misc configuré pour armv7 (qemu-user-static),
#             afin que l'apk armv7 du rootfs tourne sur un hôte x86_64.
set -eu

HERE=$(cd "$(dirname "$0")/.." && pwd)
BASE="$HERE/base/alpine-minirootfs-3.24.4-armv7.tar.gz"
OUT="${OUT:-$HERE/dist}"
ARCHIVE="ruche-rootfs-3.24.4-armv7.tar.gz"
WORK=$(mktemp -d)
ROOT="$WORK/rootfs"

cleanup() {
    rm -rf "$WORK"
}
trap cleanup EXIT

[ "$(id -u)" -eq 0 ] || { echo "ce script doit tourner en root" >&2; exit 1; }
[ -f "$BASE" ] || { echo "base absente : $BASE" >&2; exit 1; }

echo "== 1. Extraction du minirootfs Alpine"
mkdir -p "$ROOT" "$OUT"
tar -xzf "$BASE" -C "$ROOT" --numeric-owner

echo "== 2. Application de l'overlay Ruche"
cp -a "$HERE/overlay/." "$ROOT/"
chmod 0755 "$ROOT/usr/local/bin/ruche"

echo "== 3. Réseau pour apk pendant la construction"
cp /etc/resolv.conf "$ROOT/etc/resolv.conf"

echo "== 4. Mise à jour de l'index et installation du manifeste"
chroot "$ROOT" /sbin/apk update
pkgs=$(grep -v -e '^#' -e '^$' "$ROOT/etc/ruche/manifest" | tr '\n' ' ')
echo "paquets : $pkgs"
# shellcheck disable=SC2086
chroot "$ROOT" /sbin/apk add $pkgs

echo "== 5. Vérifications dans le rootfs"
chroot "$ROOT" /sbin/apk --version
chroot "$ROOT" /bin/sh -c 'grep PRETTY_NAME /etc/os-release'
chroot "$ROOT" /usr/local/bin/ruche help >/dev/null
chroot "$ROOT" /usr/local/bin/ruche list
chroot "$ROOT" /usr/local/bin/ruche doctor

echo "== 6. Nettoyage (cache apk, DNS de build)"
rm -rf "$ROOT"/var/cache/apk/*
: > "$ROOT/etc/resolv.conf"

echo "== 7. Archive"
tar --numeric-owner -C "$ROOT" -czf "$OUT/$ARCHIVE" .
(cd "$OUT" && sha256sum "$ARCHIVE" > "$ARCHIVE.sha256")
ls -la "$OUT"
cat "$OUT/$ARCHIVE.sha256"
