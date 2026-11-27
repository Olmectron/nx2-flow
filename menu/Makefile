# SPDX-License-Identifier: GPL-2.0-or-later
#
# nx2-flow, the menu: a shelf of the games found as NUP files on the card.
#
#   make                 the program for the console (ounce-devkit): out/nx2-flow.nro
#   make preview         a PC program that renders the menu's screens from a pretend card, and
#                        the pictures themselves in out/preview/ (needs a PC g++ and Python + Pillow)
#   make nettest         a PC program that runs the file receiver with a folder for a card, to
#                        try it against a sender with no console, or against itself
#   make partstest       a PC program that checks how games kept in parts are listed and read
#                        (tools/test_parts.sh builds both, makes their pretend cards and runs them)
#   make font            source/font_baked.h again (tools/bake_font.py)
#   make clean

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TARGET   := nx2-flow
VERSION  := 0.2.5
OUT      := out
SOURCES  := app covers images library nup transfer main_switch
# The rules for a NUP file kept in parts are the game loader's own header: one place for both.
PARTS    := ../loader/runtime
HEADERS  := $(wildcard source/*.h) $(PARTS)/nupparts.h $(PARTS)/nup_title.h

include $(DEVKITPRO)/libounce/switch2_rules
ARCH     := $(SWITCH2_CPU) -mtp=soft -fPIE
DEFINES  := -D__SWITCH__ -D__SWITCH2__
LIBDIRS  := $(PORTLIBS) $(LIBNX) $(DEVKITPRO)/portlibs/switch
LIBS     := -lnx

INCLUDES := $(foreach dir,$(LIBDIRS),-I$(dir)/include) -Isource -I$(PARTS)
LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
CXXFLAGS := -g -O2 -Wall -ffunction-sections $(ARCH) $(DEFINES) $(INCLUDES) -std=gnu++20 -fno-rtti -fno-exceptions
LDFLAGS  := -specs=$(LIBNX)/switch.specs -g $(ARCH) -Wl,-Map,$(OUT)/$(TARGET).map
TOOLS    := $(DEVKITPRO)/tools/bin
OBJECTS  := $(foreach s,$(SOURCES),$(OUT)/$(s).o)

# The preview is built by the PC's own compiler, which is not the one the rules above set CXX to.
HOSTCXX  ?= g++
PYTHON   ?= python

.PHONY: all clean preview nettest partstest nacptest copytest font
all: $(OUT)/$(TARGET).nro

$(OUT):
	mkdir -p $@

$(OUT)/%.o: source/%.cpp $(HEADERS) | $(OUT)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OUT)/$(TARGET).elf: $(OBJECTS)
	$(CXX) $(LDFLAGS) $^ $(LIBPATHS) $(LIBS) -o $@

$(OUT)/$(TARGET).nacp: | $(OUT)
	$(TOOLS)/nacptool --create "nx2-flow" "Olmectron" "$(VERSION)" $@

$(OUT)/$(TARGET).nro: $(OUT)/$(TARGET).elf $(OUT)/$(TARGET).nacp icon.jpg
	$(TOOLS)/elf2nro $< $@ --nacp=$(OUT)/$(TARGET).nacp --icon=icon.jpg

PREVIEW_SOURCES := preview/main.cpp source/app.cpp source/covers.cpp source/images.cpp source/library.cpp source/nup.cpp

$(OUT)/preview/preview$(EXE): $(PREVIEW_SOURCES) $(HEADERS)
	mkdir -p $(OUT)/preview
	$(HOSTCXX) -std=gnu++20 -O2 -Wall -Isource -I$(PARTS) $(PREVIEW_SOURCES) -o $@

preview: $(OUT)/preview/preview$(EXE)
	rm -rf $(OUT)/preview/card $(OUT)/preview/*.ppm $(OUT)/preview/*.png
	$(PYTHON) tools/make_fake_card.py $(OUT)/preview/card
	$(OUT)/preview/preview$(EXE) $(OUT)/preview/card $(OUT)/preview
	$(PYTHON) tools/ppm_to_png.py $(OUT)/preview

# The file receiver itself, as it runs on the console, with a folder for a card.
nettest:
	mkdir -p $(OUT)/preview
	$(HOSTCXX) -std=gnu++20 -O2 -Wall -Isource -I$(PARTS) preview/transfer_test.cpp source/transfer.cpp source/library.cpp source/nup.cpp source/images.cpp -o $(OUT)/preview/transfer_test$(EXE)

# The library's list of a pretend card with games in parts (tools/make_parts_card.py), checked.
partstest:
	mkdir -p $(OUT)/preview
	$(HOSTCXX) -std=gnu++20 -O2 -Wall -Isource -I$(PARTS) preview/parts_test.cpp source/library.cpp source/nup.cpp source/images.cpp -o $(OUT)/preview/parts_test$(EXE)

# Host unit tests for NACP parsing (format 0 uncompressed and FW 21.0.0+ format 1 DEFLATE).
nacptest:
	mkdir -p $(OUT)/preview
	$(HOSTCXX) -std=gnu++20 -O2 -Wall -Isource -I$(PARTS) preview/nacp_test.cpp source/nup.cpp source/images.cpp -o $(OUT)/preview/nacp_test$(EXE)

# Host unit tests for atomic CopyFolder staging and write-error rollback.
copytest:
	mkdir -p $(OUT)/preview
	$(HOSTCXX) -std=gnu++20 -O2 -Wall -Isource -I$(PARTS) preview/copy_test.cpp source/library.cpp source/nup.cpp source/images.cpp -o $(OUT)/preview/copy_test$(EXE)

font:
	$(PYTHON) tools/bake_font.py

clean:
	rm -rf $(OUT)
