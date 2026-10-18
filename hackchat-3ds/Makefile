#---------------------------------------------------------------------------------
# hack.chat — client Nintendo 3DS (homebrew)
# Produit : hackchat.3dsx
# Ne pas lancer make directement : utilise ./build.sh (il prépare tout).
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO n'est pas défini. Lance ./build.sh plutôt que make.)
endif

TARGET      := hackchat
BUILD       := build
SOURCES     := src
DATA        :=
INCLUDES    := src
ROMFS       := romfs
TITLE       := hack.chat
DESCRIPTION := Client hack.chat pour 3DS
AUTHOR      := hackchat-3ds

CFLAGS  := -g -Wall -O2 -mword-relocations -ffunction-sections $(ARCH) -D__3DS__
CXXFLAGS := $(CFLAGS)
ASFLAGS := -g $(ARCH)
LDFLAGS := -specs=3dsx.specs -g $(ARCH)

# mbedTLS (TLS) + libctru (système 3DS)
LIBS    := -lmbedtls -lmbedx509 -lmbedcrypto -lctru -lm
LIBDIRS := $(PORTLIBS) $(CTRULIB)

include $(DEVKITARM)/3ds_rules
