# SPDX-License-Identifier: GPL-2.0-or-later
#
# nx2-flow covers, the companion: downloads covers on a Switch and hands them to nx2-flow.
#
#   make                 the program for a Switch (devkitA64 and libnx): out/nx2-flow-covers.nro
#   make clean

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TARGET   := nx2-flow-covers
VERSION  := 0.1.0
OUT      := out
SOURCES  := main cover_fetch cover_send
HEADERS  := $(wildcard source/*.h)

include $(DEVKITPRO)/libnx/switch_rules
ARCH     := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
DEFINES  := -D__SWITCH__ -DVERSION=\"$(VERSION)\"
LIBDIRS  := $(LIBNX)
LIBS     := -lnx

INCLUDES := $(foreach dir,$(LIBDIRS),-I$(dir)/include) -Isource
LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
CXXFLAGS := -g -O2 -Wall -ffunction-sections $(ARCH) $(DEFINES) $(INCLUDES) -std=gnu++20 -fno-rtti -fno-exceptions
LDFLAGS  := -specs=$(LIBNX)/switch.specs -g $(ARCH) -Wl,-Map,$(OUT)/$(TARGET).map
TOOLS    := $(DEVKITPRO)/tools/bin
OBJECTS  := $(foreach s,$(SOURCES),$(OUT)/$(s).o)

.PHONY: all clean
all: $(OUT)/$(TARGET).nro

$(OUT):
	mkdir -p $@

$(OUT)/%.o: source/%.cpp $(HEADERS) | $(OUT)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OUT)/$(TARGET).elf: $(OBJECTS)
	$(CXX) $(LDFLAGS) $^ $(LIBPATHS) $(LIBS) -o $@

$(OUT)/$(TARGET).nacp: Makefile | $(OUT)
	$(TOOLS)/nacptool --create "nx2-flow covers" "Olmectron" "$(VERSION)" $@

$(OUT)/$(TARGET).nro: $(OUT)/$(TARGET).elf $(OUT)/$(TARGET).nacp icon.jpg
	$(TOOLS)/elf2nro $< $@ --nacp=$(OUT)/$(TARGET).nacp --icon=icon.jpg

clean:
	rm -rf $(OUT)
