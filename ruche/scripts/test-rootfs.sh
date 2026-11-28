#!/bin/sh
# Teste un rootfs Ruche : extrait l'archive dans un dossier temporaire,
# vérifie le sha256, lance fzf dedans, puis supprime le rootfs extrait.
#
# Usage : sudo sh ruche/scripts/test-rootfs.sh CHEMIN/ruche-rootfs-3.24.4-armv7.tar.gz
# Prérequis : root et binfmt armv7 (qemu-user-static), comme pour le build.
set -eu

ARCHIVE="${1:?usage : test-rootfs.sh ARCHIVE.tar.gz}"
[ "$(id -u)" -eq 0 ] || { echo "ce script doit tourner en root" >&2; exit 1; }
[ -f "$ARCHIVE" ] || { echo "archive absente : $ARCHIVE" >&2; exit 1; }

WORK=$(mktemp -d)
ROOT="$WORK/rootfs"
cleanup() {
    rm -rf "$WORK"
    echo "== rootfs temporaire supprimé"
}
trap cleanup EXIT

echo "== 1. Vérification du sha256"
if [ -f "$ARCHIVE.sha256" ]; then
    (cd "$(dirname "$ARCHIVE")" && sha256sum -c "$(basename "$ARCHIVE").sha256")
else
    echo "pas de fichier .sha256 à côté, vérification ignorée"
fi

echo "== 2. Extraction"
mkdir -p "$ROOT"
tar -xzpf "$ARCHIVE" -C "$ROOT" --numeric-owner

echo "== 3. fzf présent et exécutable"
chroot "$ROOT" /bin/sh -c 'command -v fzf && fzf --version'

echo "== 4. Test fzf (mode filtre, sans terminal)"
expected="beta"
got=$(printf 'alpha\nbeta\ngamma\n' | chroot "$ROOT" /usr/bin/fzf --filter=bet | head -n 1)
if [ "$got" != "$expected" ]; then
    echo "ÉCHEC fzf : attendu '$expected', obtenu '$got'" >&2
    exit 1
fi
echo "OK fzf --filter : $got"

echo "== 5. Test ruche dans le rootfs"
chroot "$ROOT" /usr/local/bin/ruche list

echo "== Tous les tests sont passés"
