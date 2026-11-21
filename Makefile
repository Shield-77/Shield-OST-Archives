ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO is not set)
endif
include $(DEVKITARM)/3ds_rules

TARGET := ShieldOSTArchives
BUILD := build
SOURCES := source
INCLUDES := include
ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS := -std=gnu11 -g -Wall -Wextra -Werror -O2 -ffunction-sections -fdata-sections $(ARCH)
CFLAGS += -Iinclude -I$(DEVKITPRO)/libctru/include -D__3DS__ -DMINIMP3_NO_SIMD
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(BUILD)/$(TARGET).map
LIBS := -L$(DEVKITPRO)/libctru/lib -lcitro2d -lcitro3d -lctru -lm
MAKEROM := $(shell test -x tools/makerom.exe && echo tools/makerom.exe || echo makerom)
BANNERTOOL := $(shell test -x tools/bannertool.exe && echo tools/bannertool.exe || echo bannertool)
THREEDSXTOOL := $(DEVKITPRO)/tools/bin/3dsxtool.exe
APP_VERSION := 100

.PHONY: all clean
all: $(BUILD)/$(TARGET).cia $(BUILD)/$(TARGET).3dsx

$(BUILD):
	@mkdir -p $@

$(BUILD)/$(TARGET).elf: source/main.c source/audio.c source/catalog.c include/audio.h include/catalog.h include/minimp3.h | $(BUILD)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ source/main.c source/audio.c source/catalog.c $(LIBS)

$(BUILD)/icon.icn: assets/icon.png | $(BUILD)
	"$(BANNERTOOL)" makesmdh -s "SHIELD OST Archives" -l "Lecteur MP3 et bibliotheques OST locales" -p "Shield-77" -i $< -o $@

$(BUILD)/banner.bnr: assets/banner.png assets/banner.wav | $(BUILD)
	"$(BANNERTOOL)" makebanner -i assets/banner.png -a assets/banner.wav -o $@

$(BUILD)/$(TARGET).cia: $(BUILD)/$(TARGET).elf $(BUILD)/icon.icn $(BUILD)/banner.bnr app.rsf
	"$(MAKEROM)" -f cia -o $@.tmp -target t -ver $(APP_VERSION) -desc app:4 -rsf app.rsf -exefslogo -elf $(BUILD)/$(TARGET).elf -icon $(BUILD)/icon.icn -banner $(BUILD)/banner.bnr
	@mv -f $@.tmp $@

$(BUILD)/$(TARGET).3dsx: $(BUILD)/$(TARGET).elf $(BUILD)/icon.icn
	"$(THREEDSXTOOL)" $(BUILD)/$(TARGET).elf $@ --smdh=$(BUILD)/icon.icn

clean:
	@rm -rf $(BUILD)
