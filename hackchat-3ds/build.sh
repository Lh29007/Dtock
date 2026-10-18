#!/usr/bin/env bash
#
# build.sh — compile le client hack.chat pour Nintendo 3DS.
#
#   ./build.sh            build complet (installe ce qui manque)
#   ./build.sh --clean    nettoie puis recompile
#   ./build.sh --help
#
# Où ça tourne :
#   - Termux (Android) : le script installe proot-distro + Debian, y copie le projet,
#     y lance le build, puis ramène le résultat dans ./out (et ~/storage/downloads si dispo).
#   - Debian / Ubuntu (ou toute distro avec apt) sur x86_64 ou aarch64 : build direct.
#
# Résultat : out/hackchat.3dsx  -> à copier sur la carte SD : /3ds/hackchat/hackchat.3dsx

set -euo pipefail

PROJ="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="$PROJ/out"
GUEST_DIR="/root/hackchat-3ds"
DKP="${DEVKITPRO:-/opt/devkitpro}"
GUEST_NAME="debian"

c_info() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
c_ok()   { printf '\033[1;32m OK\033[0m %s\n' "$*"; }
c_warn() { printf '\033[1;33m !!\033[0m %s\n' "$*"; }
die()    { printf '\033[1;31mERREUR:\033[0m %s\n' "$*" >&2; exit 1; }

CLEAN=0
for arg in "$@"; do
    case "$arg" in
        --clean) CLEAN=1 ;;
        -h|--help)
            sed -n '2,15p' "$0"
            exit 0 ;;
        *) die "option inconnue : $arg (voir --help)" ;;
    esac
done

is_termux() { [ -n "${TERMUX_VERSION:-}" ] || [[ "${PREFIX:-}" == *com.termux* ]]; }

# ---------------------------------------------------------------------------
# 1. Termux : on relance le même script dans une Debian (proot-distro)
# ---------------------------------------------------------------------------
if is_termux && [ -z "${HC_IN_GUEST:-}" ]; then
    c_info "Termux détecté : le build se fait dans une Debian (proot-distro)."

    if ! command -v proot-distro >/dev/null 2>&1; then
        c_info "Installation de proot-distro…"
        pkg install -y proot-distro
    fi

    if ! proot-distro login "$GUEST_NAME" -- true >/dev/null 2>&1; then
        c_info "Installation de Debian (une seule fois, environ 300 Mo)…"
        proot-distro install "$GUEST_NAME"
    fi

    EXTRA=""
    [ "$CLEAN" = 1 ] && EXTRA="--clean"

    c_info "Copie du projet dans Debian…"
    tar -C "$PROJ" --exclude=./out --exclude=./build -cf - . \
        | proot-distro login "$GUEST_NAME" -- bash -c \
            "rm -rf '$GUEST_DIR' && mkdir -p '$GUEST_DIR' && tar -C '$GUEST_DIR' -xf - && HC_IN_GUEST=1 bash '$GUEST_DIR/build.sh' $EXTRA"

    c_info "Récupération du résultat…"
    mkdir -p "$OUT_DIR"
    proot-distro login "$GUEST_NAME" -- bash -c "tar -C '$GUEST_DIR/out' -cf - ." \
        | tar -C "$OUT_DIR" -xf -

    c_ok "Fichier prêt : $OUT_DIR/hackchat.3dsx"

    if [ -d "$HOME/storage/downloads" ]; then
        cp -f "$OUT_DIR/hackchat.3dsx" "$HOME/storage/downloads/hackchat.3dsx"
        c_ok "Copie aussi dans : $HOME/storage/downloads/hackchat.3dsx"
        c_info "(Pour l'avoir dans Téléchargements : termux-setup-storage a été fait.)"
    else
        c_warn "Pour l'accès au stockage Android, lance 'termux-setup-storage' puis relance le build."
    fi

    echo
    echo "Sur la 3DS : copie hackchat.3dsx dans la carte SD -> /3ds/hackchat/hackchat.3dsx"
    echo "Puis lance-le depuis le Homebrew Launcher."
    exit 0
fi

# ---------------------------------------------------------------------------
# 2. Dépendances (Linux : Debian/Ubuntu, x86_64 ou aarch64)
# ---------------------------------------------------------------------------
[ "$(uname -s)" = Linux ] || die "Ce script ne tourne que sous Linux (ou Termux via proot)."
case "$(uname -m)" in
    x86_64|aarch64) ;;
    *) die "Architecture $(uname -m) non supportée par devkitPro (x86_64 ou aarch64 requis)." ;;
esac

SUDO=""
[ "$(id -u)" -ne 0 ] && SUDO="sudo"

if ! command -v apt-get >/dev/null 2>&1; then
    die "apt-get introuvable. Installe à la main : wget, openssl, make, zip, puis devkitPro (https://devkitpro.org/wiki/devkitPro_pacman)."
fi

c_info "Paquets système (wget, openssl, make, zip)…"
$SUDO apt-get update -qq </dev/null
$SUDO env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
    wget ca-certificates openssl make zip unzip curl </dev/null >/dev/null

# devkitPro (pacman) : installé une seule fois dans /opt/devkitpro
if [ ! -x "$DKP/devkitARM/bin/arm-none-eabi-gcc" ]; then
    c_info "Installation de devkitPro (pacman) — une seule fois, plusieurs centaines de Mo…"
    wget -q https://apt.devkitpro.org/install-devkitpro-pacman -O /tmp/install-devkitpro-pacman
    chmod +x /tmp/install-devkitpro-pacman
    printf 'y\n' | $SUDO /tmp/install-devkitpro-pacman
fi

export DEVKITPRO="$DKP"
export DEVKITARM="$DKP/devkitARM"

command -v dkp-pacman >/dev/null 2>&1 || die "dkp-pacman introuvable après installation de devkitPro."

c_info "Mise à jour de la base devkitPro et installation de 3ds-dev + mbedTLS…"
$SUDO dkp-pacman -Sy --noconfirm </dev/null >/dev/null
if ! $SUDO dkp-pacman -S --needed --noconfirm 3ds-dev 3ds-mbedtls </dev/null >/dev/null; then
    die "Installation impossible (3ds-dev / 3ds-mbedtls). Vérifie avec : dkp-pacman -Ss mbedtls"
fi

[ -f "$DKP/portlibs/3ds/include/mbedtls/ssl.h" ] \
    || die "mbedTLS introuvable dans $DKP/portlibs/3ds (paquet 3ds-mbedtls manquant)."
c_ok "devkitPro prêt ($DEVKITARM)"

# ---------------------------------------------------------------------------
# 3. Certificats racine embarqués (romfs) : la 3DS n'a pas de magasin CA à elle
# ---------------------------------------------------------------------------
c_info "Certificats racine embarqués dans l'application…"
[ -f /etc/ssl/certs/ca-certificates.crt ] \
    || die "/etc/ssl/certs/ca-certificates.crt introuvable (installe ca-certificates)."
mkdir -p "$PROJ/romfs"
cp -f /etc/ssl/certs/ca-certificates.crt "$PROJ/romfs/cacert.pem"

if echo | openssl s_client -connect hack.chat:443 -servername hack.chat \
        -CAfile "$PROJ/romfs/cacert.pem" -verify_return_error >/dev/null 2>&1; then
    c_ok "hack.chat répond en TLS avec un certificat valide"
else
    c_warn "Impossible de vérifier hack.chat depuis ce build (réseau ? pare-feu ?). Le build continue."
fi

# ---------------------------------------------------------------------------
# 4. Compilation
# ---------------------------------------------------------------------------
cd "$PROJ"
if [ "$CLEAN" = 1 ]; then
    c_info "Nettoyage…"
    make clean >/dev/null 2>&1 || true
fi

c_info "Compilation…"
make -j"$(nproc)"

[ -f "$PROJ/hackchat.3dsx" ] || die "La compilation n'a pas produit hackchat.3dsx."

mkdir -p "$OUT_DIR"
cp -f "$PROJ/hackchat.3dsx" "$OUT_DIR/"
[ -f "$PROJ/hackchat.elf" ] && cp -f "$PROJ/hackchat.elf" "$OUT_DIR/"

echo
c_ok "Build terminé !"
if [ "$(pwd -P)" = "$GUEST_DIR" ] || [ -n "${HC_IN_GUEST:-}" ]; then
    echo "  Fichier (dans Debian) : $OUT_DIR/hackchat.3dsx"
else
    echo "  Fichier : $OUT_DIR/hackchat.3dsx"
fi
echo "  Sur la 3DS : copie hackchat.3dsx dans la carte SD -> /3ds/hackchat/hackchat.3dsx"
echo "  Puis lance-le depuis le Homebrew Launcher."
