#---------------------------------------------------------------------------------
# AlphaCraft : prototype Nintendo 3DS
# Produit : AlphaCraft.3dsx
#
# source/textures.h est généré par tools/gen_textures.py ; relancer ce script après
# toute modification de textures/.
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITPRO)),)
$(error "DEVKITPRO doit être défini")
endif

TOPDIR ?= $(CURDIR)

#---------------------------------------------------------------------------------
# Cible
#---------------------------------------------------------------------------------
TARGET      := AlphaCraft
BUILD       := build
SOURCES     := source
DATA        :=
INCLUDES    := source

APP_TITLE       := AlphaCraft
APP_DESCRIPTION := Prototype Minecraft Alpha pour 3DS
APP_AUTHOR      := Lh29007

#---------------------------------------------------------------------------------
# Options de compilation
#---------------------------------------------------------------------------------
ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS  := -g -Wall -O2 -mword-relocations -ffunction-sections \
           $(ARCH)

CFLAGS  += $(INCLUDE) -DARM11 -D_3DS

CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions

ASFLAGS := -g $(ARCH)
LDFLAGS  = -specs=3dsx.specs -g $(ARCH)

LIBS    := -lctru -lm

LIBDIRS := $(CTRULIB)

#---------------------------------------------------------------------------------
# Build standard devkitPro (source/textures.h est généré et commité)
#---------------------------------------------------------------------------------
include $(DEVKITARM)/3ds_rules
