#---------------------------------------------------------------------------------
# hack.chat — client Nintendo 3DS (homebrew)
# Produit : hackchat.3dsx
# Lancer via ./build.sh (local) ou le workflow GitHub Actions.
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO n'est pas défini. Lance ./build.sh plutôt que make.)
endif

TARGET   := hackchat
BUILD    := build
SOURCES  := src
DATA     :=
INCLUDES := src
ROMFS    := romfs

APP_TITLE       := hack.chat
APP_DESCRIPTION := Client hack.chat pour 3DS
APP_AUTHOR      := hackchat-3ds

# Variables en évaluation différée (=) : ARCH, PORTLIBS et CTRULIB
# sont définis par les règles devkitPro incluses plus bas.
CFLAGS   = -g -Wall -O2 -mword-relocations -ffunction-sections $(ARCH) -D__3DS__
CXXFLAGS = $(CFLAGS)
ASFLAGS  = -g $(ARCH)
LDFLAGS  = -specs=3dsx.specs -g $(ARCH)

# mbedTLS (TLS) + libctru (système 3DS)
LIBS     = -lmbedtls -lmbedx509 -lmbedcrypto -lctru -lm
LIBDIRS  = $(PORTLIBS) $(CTRULIB)

include $(DEVKITARM)/3ds_rules
