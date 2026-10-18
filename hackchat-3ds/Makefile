#---------------------------------------------------------------------------------
# hack.chat — client Nintendo 3DS (homebrew)
# Produit : hackchat.3dsx
#
# Makefile autonome : appelle directement les outils devkitPro (arm-none-eabi-gcc,
# 3dsxtool, smdhtool), sans dépendre des règles internes de 3ds_rules.
# Lancer via ./build.sh (local) ou le workflow GitHub Actions.
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO n'est pas défini. Lance ./build.sh plutôt que make.)
endif

DEVKITARM ?= $(DEVKITPRO)/devkitARM
CTRULIB   ?= $(DEVKITPRO)/libctru
PORTLIBS  ?= $(DEVKITPRO)/portlibs/3ds

export PATH := $(DEVKITPRO)/tools/bin:$(DEVKITARM)/bin:$(PATH)

CC      := $(DEVKITARM)/bin/arm-none-eabi-gcc
TARGET  := hackchat
APP_TITLE       := hack.chat
APP_DESCRIPTION := Client hack.chat pour 3DS
APP_AUTHOR      := hackchat-3ds

ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
INCLUDE := -Isrc -I$(CTRULIB)/include -I$(PORTLIBS)/include
CFLAGS  := -g -Wall -O2 -std=gnu11 -mword-relocations -ffunction-sections \
           $(ARCH) $(INCLUDE) -D__3DS__
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -L$(CTRULIB)/lib -L$(PORTLIBS)/lib
LIBS    := -lmbedtls -lmbedx509 -lmbedcrypto -lctru -lm

SRC  := $(wildcard src/*.c)
OBJS := $(patsubst src/%.c,build/%.o,$(SRC))

.PHONY: all clean

all: $(TARGET).3dsx

build:
	mkdir -p build

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@

$(TARGET).smdh:
	smdhtool --create "$(APP_TITLE)" "$(APP_DESCRIPTION)" "$(APP_AUTHOR)" \
		$(CTRULIB)/default_icon.png $@

$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh romfs
	3dsxtool $(TARGET).elf $@ --romfs=romfs --smdh=$(TARGET).smdh

clean:
	rm -rf build $(TARGET).elf $(TARGET).3dsx $(TARGET).smdh
