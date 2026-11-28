#!/bin/sh
# Construit le rootfs Ruche (armv7) à partir du minirootfs Alpine officiel.
#
# Exécution : sudo sh ruche/scripts/build-rootfs.sh
# Prérequis : root, et binfmt_misc configuré pour armv7 (qemu-user-static),
#             pour que l'apk armv7 du rootfs tourne sur un hôte x86_64.
#
# Pourquoi la normalisation des permissions (étape 3) :
# sur Termux, l'archive est extraite en non-root, donc tar ne peut pas
# restaurer les propriétaires d'origine. Si des dossiers comme /lib/apk/db
# arrivent en 0700, apk échoue ensuite avec « failed to write database:
# Permission denied ». On fixe donc des modes lisibles et traversables
# dans l'archive elle-même, au lieu de compter sur l'extraction.
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
tar -xzpf "$BASE" -C "$ROOT" --numeric-owner

echo "== 2. Application de l'overlay Ruche"
cp -a "$HERE/overlay/." "$ROOT/"
chmod 0755 "$ROOT/usr/local/bin/ruche"

echo "== 3. Normalisation des permissions (dans l'archive)"
# Dossiers : lisibles et traversables pour tout le monde, écrits par root.
# /root reste privé, comme dans la distro d'origine.
find "$ROOT" -type d ! -path "$ROOT/root" -exec chmod 0755 {} +
chmod 0700 "$ROOT/root"
# Fichiers : lisibles, avec le bit d'exécution d'origine conservé.
find "$ROOT" -type f -exec chmod u+rw,go+r {} +
# Dossiers qu'apk doit écrire : explicitement accessibles en écriture pour root.
chmod 0755 "$ROOT/lib/apk" "$ROOT/lib/apk/db" "$ROOT/var/cache/apk" "$ROOT/etc/apk"

echo "== 4. Réseau pour apk pendant la construction"
cp /etc/resolv.conf "$ROOT/etc/resolv.conf"

echo "== 5. Index apk et installation du manifeste"
chroot "$ROOT" /sbin/apk update
pkgs=$(grep -v -e '^#' -e '^$' "$ROOT/etc/ruche/manifest" | tr '\n' ' ')
echo "paquets : $pkgs"
# shellcheck disable=SC2086
chroot "$ROOT" /sbin/apk add $pkgs

echo "== 6. Vérifications dans le rootfs"
chroot "$ROOT" /sbin/apk --version
chroot "$ROOT" /bin/sh -c 'grep PRETTY_NAME /etc/os-release'
# Régression : apk doit pouvoir écrire sa base (c'est l'erreur vue sur Termux).
chroot "$ROOT" /bin/sh -c 'touch /lib/apk/db/.write-test && rm /lib/apk/db/.write-test'
chroot "$ROOT" /usr/local/bin/ruche help >/dev/null
chroot "$ROOT" /usr/local/bin/ruche list
chroot "$ROOT" /usr/local/bin/ruche doctor
chroot "$ROOT" /bin/sh -c 'command -v fzf >/dev/null && fzf --version'

echo "== 7. Nettoyage (cache apk, DNS de build)"
rm -rf "$ROOT"/var/cache/apk/*
: > "$ROOT/etc/resolv.conf"

echo "== 8. Archive (propriétaires forcés à root, modes normalisés)"
tar --numeric-owner --owner=0 --group=0 -C "$ROOT" -czf "$OUT/$ARCHIVE" .
(cd "$OUT" && sha256sum "$ARCHIVE" > "$ARCHIVE.sha256")
ls -la "$OUT"
cat "$OUT/$ARCHIVE.sha256"
