#---------------------------------------------------------------------------------
# AlphaCraft : prototype Nintendo 3DS
# Produit : AlphaCraft.3dsx
#
# Makefile autonome (même structure que hackchat-3ds, qui compile en CI).
# source/textures.h est généré par tools/gen_textures.py et commité.
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO n'est pas défini)
endif

DEVKITARM ?= $(DEVKITPRO)/devkitARM
CTRULIB   ?= $(DEVKITPRO)/libctru
PORTLIBS  ?= $(DEVKITPRO)/portlibs/3ds

export PATH := $(DEVKITPRO)/tools/bin:$(DEVKITARM)/bin:$(PATH)

CC      := $(DEVKITARM)/bin/arm-none-eabi-gcc
TARGET  := AlphaCraft
APP_TITLE       := AlphaCraft
APP_DESCRIPTION := Prototype Minecraft Alpha pour 3DS
APP_AUTHOR      := Lh29007

ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
INCLUDE := -Isource -I$(CTRULIB)/include -I$(PORTLIBS)/include
CFLAGS  := -g -Wall -O2 -std=gnu11 -mword-relocations -ffunction-sections \
           $(ARCH) $(INCLUDE) -D__3DS__ -D_3DS
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -L$(CTRULIB)/lib -L$(PORTLIBS)/lib
LIBS    := -lctru -lm

SRC  := $(wildcard source/*.c)
OBJS := $(patsubst source/%.c,build/%.o,$(SRC))

.PHONY: all clean

all: $(TARGET).3dsx

build:
	mkdir -p build

build/%.o: source/%.c source/textures.h source/font.h | build
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@

$(TARGET).smdh:
	smdhtool --create "$(APP_TITLE)" "$(APP_DESCRIPTION)" "$(APP_AUTHOR)" \
		$(CTRULIB)/default_icon.png $@

$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh
	3dsxtool $(TARGET).elf $@ --smdh=$(TARGET).smdh

clean:
	rm -rf build $(TARGET).elf $(TARGET).3dsx $(TARGET).smdh
