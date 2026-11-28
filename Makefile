ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO is not set)
endif
include $(DEVKITARM)/3ds_rules

TARGET := ShieldOnlineArchive
BUILD := build
SOURCES := source
INCLUDES := include
ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS := -std=gnu11 -g -Wall -Wextra -Werror -O2 -ffunction-sections -fdata-sections $(ARCH)
CFLAGS += -Iinclude -Ivendor -Ivendor/cjson -Ivendor/dr_libs -Ivendor/faad2/include -I$(DEVKITPRO)/portlibs/3ds/include -I$(DEVKITPRO)/libctru/include -D__3DS__ -DMINIMP3_NO_SIMD
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(BUILD)/$(TARGET).map
LIBS := -L$(DEVKITPRO)/portlibs/3ds/lib -L$(DEVKITPRO)/libctru/lib -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -lcitro2d -lcitro3d -lctru -lm
MAKEROM := $(shell test -x tools/makerom.exe && echo tools/makerom.exe || echo makerom)
BANNERTOOL := $(shell test -x tools/bannertool.exe && echo tools/bannertool.exe || echo bannertool)
THREEDSXTOOL := $(DEVKITPRO)/tools/bin/3dsxtool.exe
APP_VERSION := 204
FAAD_SRC := $(wildcard vendor/faad2/libfaad/*.c)
FAAD_OBJ := $(patsubst vendor/faad2/libfaad/%.c,$(BUILD)/faad/%.o,$(FAAD_SRC))

.PHONY: all clean
all: $(BUILD)/$(TARGET).cia $(BUILD)/$(TARGET).3dsx

$(BUILD):
	@mkdir -p $@

$(BUILD)/ca_bundle.o: source/ca_bundle.S romfs/ca-bundle.pem | $(BUILD)
	$(CC) $(ARCH) -c -o $@ source/ca_bundle.S

$(BUILD)/faad/%.o: vendor/faad2/libfaad/%.c
	@mkdir -p $(BUILD)/faad
	$(CC) $(ARCH) -std=gnu11 -O2 -DFIXED_POINT -DPACKAGE_VERSION='"2.11.4"' -DHAVE_INTTYPES_H=1 -DHAVE_MEMCPY=1 -DHAVE_STRING_H=1 -DHAVE_STRINGS_H=1 -Ivendor/faad2/include -Ivendor/faad2/libfaad -c -o $@ $<

$(BUILD)/libfaad2.a: $(FAAD_OBJ)
	$(AR) rcs $@ $(FAAD_OBJ)

$(BUILD)/minimp4_impl.o: source/minimp4_impl.c vendor/minimp4.h | $(BUILD)
	$(CC) $(ARCH) -std=gnu11 -O2 -Ivendor -c -o $@ source/minimp4_impl.c

$(BUILD)/$(TARGET).elf: source/app_main.c source/audio.c source/ia_model.c source/ia_http.c source/cover.c source/i18n.c vendor/cjson/cJSON.c vendor/stb_image.h vendor/dr_libs/dr_wav.h vendor/dr_libs/dr_flac.h include/audio.h include/minimp3.h include/ia_model.h include/ia_http.h include/cover.h include/i18n.h $(BUILD)/ca_bundle.o $(BUILD)/minimp4_impl.o $(BUILD)/libfaad2.a | $(BUILD)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ source/app_main.c source/audio.c source/ia_model.c source/ia_http.c source/cover.c source/i18n.c vendor/cjson/cJSON.c $(BUILD)/ca_bundle.o $(BUILD)/minimp4_impl.o $(BUILD)/libfaad2.a $(LIBS)

$(BUILD)/icon.icn: assets/icon.png | $(BUILD)
	"$(BANNERTOOL)" makesmdh -s "SHIELD Online Archive" -l "Archives audio en ligne" -p "Shield-77" -i $< -o $@

$(BUILD)/banner.bnr: assets/banner.png assets/banner.wav | $(BUILD)
	"$(BANNERTOOL)" makebanner -i assets/banner.png -a assets/banner.wav -o $@

$(BUILD)/$(TARGET).cia: $(BUILD)/$(TARGET).elf $(BUILD)/icon.icn $(BUILD)/banner.bnr app.rsf
	"$(MAKEROM)" -f cia -o $@.tmp -target t -ver $(APP_VERSION) -desc app:4 -rsf app.rsf -exefslogo -elf $(BUILD)/$(TARGET).elf -icon $(BUILD)/icon.icn -banner $(BUILD)/banner.bnr
	@mv -f $@.tmp $@

$(BUILD)/$(TARGET).3dsx: $(BUILD)/$(TARGET).elf $(BUILD)/icon.icn
	"$(THREEDSXTOOL)" $(BUILD)/$(TARGET).elf $@ --smdh=$(BUILD)/icon.icn

clean:
	@rm -rf $(BUILD)
