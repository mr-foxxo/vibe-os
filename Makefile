SHELL := /bin/sh
.DEFAULT_GOAL := all

# ARCHITECTURE:
# - make            : Build kernel (no glibc, uses stubs)
# - make glibc      : Build glibc library (lang/vendor/glibc -> build/lib/libglibc.*)
# - make apps       : Build language runtimes in build/bin
# - make clean      : Clean kernel build
# - make glibc-clean: Clean glibc
# - make apps-clean : Clean apps

UNAME_S := $(shell uname -s 2>/dev/null || echo Unknown)
TOOLCHAIN_PREFIX ?= i686-elf-
HAS_CROSS_TOOLCHAIN := $(shell command -v $(TOOLCHAIN_PREFIX)gcc >/dev/null 2>&1 && command -v $(TOOLCHAIN_PREFIX)ld >/dev/null 2>&1 && command -v $(TOOLCHAIN_PREFIX)objcopy >/dev/null 2>&1 && command -v $(TOOLCHAIN_PREFIX)nm >/dev/null 2>&1 && command -v $(TOOLCHAIN_PREFIX)ar >/dev/null 2>&1 && command -v $(TOOLCHAIN_PREFIX)ranlib >/dev/null 2>&1 && echo 1 || echo 0)
ALLOW_HOST_TOOLCHAIN ?= 0

# Capture original origins before overriding built-in defaults.
CC_ORIGIN := $(origin CC)
LD_ORIGIN := $(origin LD)
NM_ORIGIN := $(origin NM)
OBJCOPY_ORIGIN := $(origin OBJCOPY)
AR_ORIGIN := $(origin AR)
RANLIB_ORIGIN := $(origin RANLIB)
AS_ORIGIN := $(origin AS)
TOOLCHAIN_OVERRIDE := 0

# GNU make built-ins (cc, ld, ar...) should not block our auto-detection.
ifeq ($(CC_ORIGIN),default)
CC :=
endif
ifeq ($(LD_ORIGIN),default)
LD :=
endif
ifeq ($(NM_ORIGIN),default)
NM :=
endif
ifeq ($(OBJCOPY_ORIGIN),default)
OBJCOPY :=
endif
ifeq ($(AR_ORIGIN),default)
AR :=
endif
ifeq ($(RANLIB_ORIGIN),default)
RANLIB :=
endif
ifeq ($(AS_ORIGIN),default)
AS :=
endif
ifneq ($(CC_ORIGIN),default)
ifneq ($(CC_ORIGIN),undefined)
TOOLCHAIN_OVERRIDE := 1
endif
endif
ifneq ($(LD_ORIGIN),default)
ifneq ($(LD_ORIGIN),undefined)
TOOLCHAIN_OVERRIDE := 1
endif
endif
ifneq ($(NM_ORIGIN),default)
ifneq ($(NM_ORIGIN),undefined)
TOOLCHAIN_OVERRIDE := 1
endif
endif
ifneq ($(OBJCOPY_ORIGIN),default)
ifneq ($(OBJCOPY_ORIGIN),undefined)
TOOLCHAIN_OVERRIDE := 1
endif
endif
ifneq ($(AR_ORIGIN),default)
ifneq ($(AR_ORIGIN),undefined)
TOOLCHAIN_OVERRIDE := 1
endif
endif
ifneq ($(RANLIB_ORIGIN),default)
ifneq ($(RANLIB_ORIGIN),undefined)
TOOLCHAIN_OVERRIDE := 1
endif
endif

ifeq ($(strip $(AS)),)
AS := nasm
BOOT_NASM_DEFINES ?=
endif
ifeq ($(strip $(QEMU)),)
QEMU := qemu-system-i386
endif
QEMU_MEMORY_MB ?= 3072
# per-scenario timeout for QEMU validation matrices; slow/TCG-only hosts can raise it:
# Raised 20->60 (2026-10-19, measured): scenarios stop EARLY once all markers
# are seen, so the cap only bites broken boots. On this 2-core TCG host the
# fixed cost is firmware-side, not kernel: SeaBIOS usb-storage probe burns
# ~11 s before the first serial byte (USB markers land ~29-31 s), and
# ahci-q35 storage smoke reaches ~30 s under host contention. Both boots
# completed every marker; the 20 s cap was reporting firmware speed as a boot
# failure. usb-bios-boot also carries an explicit per-scenario budget in
# tools/validate_phase6.py documenting the BIOS constant.
PHASE6_TIMEOUT ?= 60
QEMU_SERIAL_LOG ?= build/qemu-serial.log
QEMU_AUDIO_CAPTURE_WAV ?= build/qemu-audio.wav
QEMU_IMAGE_OPTS ?= format=raw,file=$(IMAGE),snapshot=on
QEMU_NET_OPTS ?= -netdev user,id=net0 -device virtio-net-pci,netdev=net0
QEMU_RUN_MACHINE ?= pc
QEMU_RUN_CPU ?= core2duo
QEMU_RUN_SMP ?= 1,sockets=1,cores=1,threads=1,maxcpus=1
QEMU_RUN_VGA ?= std
QEMU_RUN_GPU_OPTS ?=
QEMU_RUN_VIDEO_OPTS ?= -vga $(QEMU_RUN_VGA) $(QEMU_RUN_GPU_OPTS)
QEMU_RUN_RTC_OPTS ?= -rtc base=localtime
QEMU_RUN_USB_OPTS ?= -usb
QEMU_SMP ?= 1
QEMU_RUN_COMMON_OPTS ?= -machine $(QEMU_RUN_MACHINE) -cpu $(QEMU_RUN_CPU) -smp $(QEMU_RUN_SMP) $(QEMU_RUN_VIDEO_OPTS) $(QEMU_RUN_RTC_OPTS) $(QEMU_RUN_USB_OPTS)
QEMU_AUDIO_DEVICE ?= AC97
QEMU_AUDIO_LIVE_CONTROLLER ?= intel-hda
QEMU_AUDIO_LIVE_CODEC ?= hda-output
QEMU_AUDIO_PROBE_BIN := $(shell if command -v $(QEMU) >/dev/null 2>&1; then printf '%s' '$(QEMU)'; elif command -v qemu-system-x86_64 >/dev/null 2>&1; then printf '%s' 'qemu-system-x86_64'; fi)
QEMU_AUDIO_HAS_COREAUDIO := $(shell if [ -n "$(QEMU_AUDIO_PROBE_BIN)" ] && $(QEMU_AUDIO_PROBE_BIN) -audiodev help 2>/dev/null | grep -qx 'coreaudio'; then printf '1'; fi)
QEMU_AUDIO_HAS_PIPEWIRE := $(shell if [ -n "$(QEMU_AUDIO_PROBE_BIN)" ] && $(QEMU_AUDIO_PROBE_BIN) -audiodev help 2>/dev/null | grep -qx 'pipewire'; then printf '1'; fi)
QEMU_AUDIO_HAS_DBUS := $(shell if [ -n "$(QEMU_AUDIO_PROBE_BIN)" ] && $(QEMU_AUDIO_PROBE_BIN) -audiodev help 2>/dev/null | grep -qx 'dbus'; then printf '1'; fi)
QEMU_AUDIO_HAS_PA := $(shell if [ -n "$(QEMU_AUDIO_PROBE_BIN)" ] && $(QEMU_AUDIO_PROBE_BIN) -audiodev help 2>/dev/null | grep -qx 'pa'; then printf '1'; fi)
QEMU_AUDIO_HAS_ALSA := $(shell if [ -n "$(QEMU_AUDIO_PROBE_BIN)" ] && $(QEMU_AUDIO_PROBE_BIN) -audiodev help 2>/dev/null | grep -qx 'alsa'; then printf '1'; fi)
ifeq ($(UNAME_S),Darwin)
ifeq ($(QEMU_AUDIO_HAS_COREAUDIO),1)
QEMU_AUDIO_DRIVER ?= coreaudio
endif
else ifeq ($(UNAME_S),Linux)
ifeq ($(QEMU_AUDIO_HAS_PIPEWIRE),1)
QEMU_AUDIO_DRIVER ?= pipewire
else ifeq ($(QEMU_AUDIO_HAS_PA),1)
QEMU_AUDIO_DRIVER ?= pa
else ifeq ($(QEMU_AUDIO_HAS_ALSA),1)
QEMU_AUDIO_DRIVER ?= alsa
else ifeq ($(QEMU_AUDIO_HAS_DBUS),1)
QEMU_AUDIO_DRIVER ?= dbus
endif
endif
ifeq ($(strip $(QEMU_AUDIO_DRIVER)),)
QEMU_AUDIO_MACHINE_OPTS ?= -machine $(QEMU_RUN_MACHINE)
QEMU_AUDIO_OPTS ?= -device $(QEMU_AUDIO_DEVICE)
QEMU_AUDIO_LIVE_OPTS ?= -device $(QEMU_AUDIO_DEVICE)
QEMU_AUDIO_HDA_LIVE_OPTS ?= -device intel-hda -device hda-duplex
QEMU_AUDIO_HDA_OUTPUT_LIVE_OPTS ?= -device intel-hda -device hda-output
else
QEMU_AUDIO_MACHINE_OPTS ?= -machine $(QEMU_RUN_MACHINE),pcspk-audiodev=snd0
QEMU_AUDIO_OPTS ?= -audiodev $(QEMU_AUDIO_DRIVER),id=snd0 -device $(QEMU_AUDIO_DEVICE),audiodev=snd0
QEMU_AUDIO_LIVE_OPTS ?= -audiodev $(QEMU_AUDIO_DRIVER),id=snd0 -device $(QEMU_AUDIO_DEVICE),audiodev=snd0
QEMU_AUDIO_HDA_LIVE_OPTS ?= -audiodev $(QEMU_AUDIO_DRIVER),id=snd0 -device intel-hda -device hda-duplex,audiodev=snd0
QEMU_AUDIO_HDA_OUTPUT_LIVE_OPTS ?= -audiodev $(QEMU_AUDIO_DRIVER),id=snd0 -device intel-hda -device hda-output,audiodev=snd0
endif
QEMU_RUN_COMMON_AUDIO_OPTS ?= $(QEMU_AUDIO_MACHINE_OPTS) -cpu $(QEMU_RUN_CPU) -smp $(QEMU_RUN_SMP) $(QEMU_RUN_VIDEO_OPTS) $(QEMU_RUN_RTC_OPTS) $(QEMU_RUN_USB_OPTS)
QEMU_AUDIO_CAPTURE_OPTS ?= -audiodev wav,id=snd0,path=$(QEMU_AUDIO_CAPTURE_WAV) -device $(QEMU_AUDIO_DEVICE),audiodev=snd0
ifeq ($(strip $(PYTHON)),)
PYTHON := python3
endif
CPU_ARCH_CFLAGS := -march=i586 -mtune=generic -mno-mmx -mno-sse -mno-sse2

ifeq ($(strip $(CC)),)
ifneq ($(TOOLCHAIN_PREFIX),i686-elf-)
CC := $(TOOLCHAIN_PREFIX)gcc
else
ifeq ($(HAS_CROSS_TOOLCHAIN),1)
CC := $(TOOLCHAIN_PREFIX)gcc
else
CC := gcc
endif
endif
endif

ifeq ($(strip $(LD)),)
ifneq ($(TOOLCHAIN_PREFIX),i686-elf-)
LD := $(TOOLCHAIN_PREFIX)ld
else
ifeq ($(HAS_CROSS_TOOLCHAIN),1)
LD := $(TOOLCHAIN_PREFIX)ld
else
LD := ld
endif
endif
endif

ifeq ($(strip $(NM)),)
ifneq ($(TOOLCHAIN_PREFIX),i686-elf-)
NM := $(TOOLCHAIN_PREFIX)nm
else
ifeq ($(HAS_CROSS_TOOLCHAIN),1)
NM := $(TOOLCHAIN_PREFIX)nm
else
NM := nm
endif
endif
endif

ifeq ($(strip $(OBJCOPY)),)
ifneq ($(TOOLCHAIN_PREFIX),i686-elf-)
OBJCOPY := $(TOOLCHAIN_PREFIX)objcopy
else
ifeq ($(HAS_CROSS_TOOLCHAIN),1)
OBJCOPY := $(TOOLCHAIN_PREFIX)objcopy
else
OBJCOPY := objcopy
endif
endif
endif

ifeq ($(strip $(AR)),)
ifneq ($(TOOLCHAIN_PREFIX),i686-elf-)
AR := $(TOOLCHAIN_PREFIX)ar
else
ifeq ($(HAS_CROSS_TOOLCHAIN),1)
AR := $(TOOLCHAIN_PREFIX)ar
else
AR := ar
endif
endif
endif

ifeq ($(strip $(RANLIB)),)
ifneq ($(TOOLCHAIN_PREFIX),i686-elf-)
RANLIB := $(TOOLCHAIN_PREFIX)ranlib
else
ifeq ($(HAS_CROSS_TOOLCHAIN),1)
RANLIB := $(TOOLCHAIN_PREFIX)ranlib
else
RANLIB := ranlib
endif
endif
endif

TOOLCHAIN_MODE := host
ifeq ($(HAS_CROSS_TOOLCHAIN),1)
TOOLCHAIN_MODE := cross
endif
ifneq ($(TOOLCHAIN_OVERRIDE),0)
TOOLCHAIN_MODE := custom
endif

MKFS_FAT_TOOL := $(shell command -v mkfs.fat 2>/dev/null || command -v newfs_msdos 2>/dev/null)
ifeq ($(strip $(MKFS_FAT_TOOL)),)
MKFS_FAT_TOOL := mkfs.fat
endif
MCOPY_TOOL := $(shell command -v mcopy 2>/dev/null)
ifeq ($(strip $(MCOPY_TOOL)),)
MCOPY_TOOL := mcopy
endif
MMD_TOOL := $(shell command -v mmd 2>/dev/null)
ifeq ($(strip $(MMD_TOOL)),)
MMD_TOOL := mmd
endif

ifeq ($(UNAME_S),Darwin)
ifneq ($(HAS_CROSS_TOOLCHAIN),1)
ifneq ($(ALLOW_HOST_TOOLCHAIN),1)
ifeq ($(TOOLCHAIN_OVERRIDE),0)
ifneq ($(TOOLCHAIN_PREFIX),i686-elf-)
TOOLCHAIN_MODE := custom
else
$(error Toolchain cruzada 32-bit nao encontrada no macOS. Configure TOOLCHAIN_PREFIX para sua toolchain i686-elf/x86_64-elf, ou exporte CC/LD/OBJCOPY/NM/AR/RANLIB, ou rode com ALLOW_HOST_TOOLCHAIN=1 por sua conta)
endif
endif
endif
endif
endif

BUILD_DIR := build
APP_BIN_DIR := $(BUILD_DIR)/bin
APP_LIB_DIR := $(BUILD_DIR)/lib
APP_CATALOG_MANIFEST := config/app_catalog.tsv
APP_CATALOG_GENERATED_DIR := $(BUILD_DIR)/generated
APP_CATALOG_GENERATED_MK := $(APP_CATALOG_GENERATED_DIR)/app_catalog.mk
APP_CATALOG_GENERATED_H := $(APP_CATALOG_GENERATED_DIR)/app_catalog.h
BOOT_DIR := boot
USERLAND_DIR := userland
LINKER_DIR := linker
BOOT_PARTITION_START_LBA := 2048
BOOT_PARTITION_SECTORS := 131072
BOOT_PARTITION_RESERVED_SECTORS := 2048
BOOT_STAGE2_START_SECTOR := 8
BOOT_KERNEL_START_SECTOR := 32
DATA_PARTITION_START_LBA := $(shell echo $$(( $(BOOT_PARTITION_START_LBA) + $(BOOT_PARTITION_SECTORS) )))
APPFS_DIRECTORY_LBA := 0
APPFS_DIRECTORY_SECTORS := 16
APPFS_APP_AREA_SECTORS := 131072
PERSIST_SECTOR_COUNT := 640
IMAGE_ASSET_START_LBA := $(shell echo $$(( $(APPFS_DIRECTORY_LBA) + $(APPFS_DIRECTORY_SECTORS) + $(APPFS_APP_AREA_SECTORS) + $(PERSIST_SECTOR_COUNT) )))
IMAGE_TOTAL_SECTORS := 524288
DATA_PARTITION_SECTORS := $(shell echo $$(( $(IMAGE_TOTAL_SECTORS) - $(DATA_PARTITION_START_LBA) )))
DOOM_WAD_SRC := userland/applications/games/DOOM/DOOM.WAD
DOOM_WAD_IMAGE_LBA := $(IMAGE_ASSET_START_LBA)
DOOM_WAD_RESERVED_SECTORS := 24576
CRAFT_TEXTURE_SRC := userland/applications/games/craft/upstream/textures/texture.png
CRAFT_FONT_SRC := userland/applications/games/craft/upstream/textures/font.png
CRAFT_SKY_SRC := userland/applications/games/craft/upstream/textures/sky.png
CRAFT_SIGN_SRC := userland/applications/games/craft/upstream/textures/sign.png
WALLPAPER_SRC := assets/wallpaper.png
VIBE_BOOT_WAV_SRC := assets/vibe_os_boot.wav
VIBE_DESKTOP_WAV_SRC := assets/vibe_os_desktop.wav
VIBE_BOOTLOADER_AUDIO_RAW := $(BUILD_DIR)/vibe_os_boot_stage2.raw
VIBE_BOOTLOADER_AUDIO_RATE ?= 4000
WALLPAPER_RUNTIME_PNG := $(BUILD_DIR)/wallpaper-runtime.png
WALLPAPER_RUNTIME_W := 1360
WALLPAPER_RUNTIME_H := 720
BOOTLOADER_BG_SRC := assets/bootloader_background.png
ICON_THEME_INDEX_SRC := assets/icons/index.theme
ICON_APP_DEFAULT_48_SRC := assets/icons/apps/48/application-default-icon.png
ICON_APP_DEFAULT_16_SRC := assets/icons/apps/16/application-default-icon.png
ICON_TERMINAL_16_SRC := assets/icons/apps/16/utilities-terminal.png
ICON_CLOCK_16_SRC := assets/icons/apps/16/clock.png
ICON_CALCULATOR_16_SRC := assets/icons/apps/16/accessories-calculator.png
ICON_EDITOR_24_SRC := assets/icons/apps/24/accessories-text-editor.png
ICON_TASKS_16_SRC := assets/icons/apps/16/utilities-system-monitor.png
ICON_IMAGEVIEWER_16_SRC := assets/icons/apps/16/camera-photo.png
ICON_WALLPAPER_16_SRC := assets/icons/apps/16/preferences-desktop-wallpaper.png
ICON_PREFERENCES_16_SRC := assets/icons/apps/16/preferences-desktop.png
ICON_APP_MENU_16_SRC := assets/icons/apps/16/package_applications.png
ICON_NETWORK_16_SRC := assets/icons/apps/16/preferences-system-network.png
ICON_TEXT_16_SRC := assets/icons/apps/16/text.png
ICON_AUDIO_PLAYER_16_SRC := assets/icons/apps/16/multimedia-audio-player.png
ICON_AUDIO_PLAYER_24_SRC := assets/icons/apps/24/audio-player.png
ICON_SKETCHPAD_22_SRC := assets/icons/apps/22/preferences-desktop-theme.png
ICON_GAME_SNAKE_16_SRC := assets/icons/apps/16/ksnake.png
ICON_GAME_BOARD_16_SRC := assets/icons/apps/16/package_games_board.png
ICON_GAME_MINES_16_SRC := assets/icons/apps/16/kmines.png
ICON_GAME_ARCADE_16_SRC := assets/icons/apps/16/package_games_arcade.png
ICON_GAME_INVADERS_16_SRC := assets/icons/apps/16/kspaceduel.png
ICON_CRAFT_24_SRC := assets/icons/apps/24/craft.png
ICON_CRAFT_48_SRC := assets/icons/apps/48/craft.png
ICON_FOLDER_16_SRC := assets/icons/places/16/folder.png
ICON_TRASH_16_SRC := assets/icons/places/16/user-trash.png
ICON_FOLDER_OPEN_32_SRC := assets/icons/places/32/folder_open.png
ICON_FOLDER_DOCS_32_SRC := assets/icons/places/32/folder-documents.png
ICON_FOLDER_MUSIC_32_SRC := assets/icons/places/32/folder-music.png
ICON_FOLDER_PICTURES_32_SRC := assets/icons/places/32/folder-pictures.png
ICON_FOLDER_VIDEO_32_SRC := assets/icons/places/32/folder-video.png
ICON_ACTION_GO_UP_16_SRC := assets/icons/actions/16/go-up.png
ICON_ACTION_OPEN_16_SRC := assets/icons/actions/16/document-open.png
ICON_ACTION_NEW_16_SRC := assets/icons/actions/16/document-new.png
ICON_ACTION_COPY_16_SRC := assets/icons/actions/16/edit-copy.png
ICON_ACTION_PASTE_16_SRC := assets/icons/actions/16/edit-paste.png
ICON_ACTION_RENAME_16_SRC := assets/icons/actions/16/item_rename.png
ICON_ACTION_SAVE_16_SRC := assets/icons/actions/16/filesave.png
ICON_ACTION_SAVE_AS_16_SRC := assets/icons/actions/16/filesaveas.png
ICON_ACTION_EXPORT_16_SRC := assets/icons/actions/16/document-export.png
ICON_ACTION_CANCEL_16_SRC := assets/icons/actions/16/cancel.png
ICON_ACTION_APPLY_16_SRC := assets/icons/actions/16/gtk-apply.png
ICON_ACTION_WINDOW_CLOSE_16_SRC := assets/icons/actions/16/window-close.png
ICON_ACTION_WINDOW_NEW_16_SRC := assets/icons/actions/16/window-new.png
ICON_ACTION_GO_DOWN_16_SRC := assets/icons/actions/16/go-down.png
ICON_ACTION_TRASH_16_SRC := assets/icons/actions/16/edittrash.png
ICON_FOLDER_48_SRC := assets/icons/places/48/folder.png
ICON_TRASH_48_SRC := assets/icons/places/48/user-trash.png
ICON_AUDIO_ZERO_16_SRC := assets/icons/panel/16/audio-volume-low-zero.png
ICON_AUDIO_LOW_16_SRC := assets/icons/panel/16/audio-volume-low.png
ICON_AUDIO_MEDIUM_16_SRC := assets/icons/panel/16/audio-volume-medium.png
ICON_AUDIO_HIGH_16_SRC := assets/icons/panel/16/audio-volume-high.png
ICON_WIFI_NONE_16_SRC := assets/icons/notifications/16/notification-network-wireless-none.png
ICON_WIFI_MEDIUM_16_SRC := assets/icons/notifications/16/notification-network-wireless-medium.png
ICON_WIFI_FULL_16_SRC := assets/icons/notifications/16/notification-network-wireless-full.png
ICON_ETHERNET_CONNECTED_24_SRC := assets/icons/notifications/24/notification-network-ethernet-connected.png
ICON_ETHERNET_DISCONNECTED_24_SRC := assets/icons/notifications/24/notification-network-ethernet-disconnected.png
ICON_THEME_INDEX_BYTES := $(shell wc -c < $(ICON_THEME_INDEX_SRC) | tr -d '[:space:]')
ICON_THEME_INDEX_SECTORS := $(shell echo $$(( ($(ICON_THEME_INDEX_BYTES) + 511) / 512 )))
BOOTLOADER_BG_WIDTH := 192
BOOTLOADER_BG_HEIGHT := 144
BOOTLOADER_BG_RESAMPLE := box
CRAFT_TEXTURE_IMAGE_LBA := $(shell echo $$(( $(DOOM_WAD_IMAGE_LBA) + $(DOOM_WAD_RESERVED_SECTORS) )))
CRAFT_FONT_IMAGE_LBA := $(shell echo $$(( $(CRAFT_TEXTURE_IMAGE_LBA) + 128 )))
CRAFT_SKY_IMAGE_LBA := $(shell echo $$(( $(CRAFT_FONT_IMAGE_LBA) + 128 )))
CRAFT_SIGN_IMAGE_LBA := $(shell echo $$(( $(CRAFT_SKY_IMAGE_LBA) + 256 )))
WALLPAPER_RESERVED_SECTORS := 1024
VIBE_BOOT_WAV_RESERVED_SECTORS := 1024
VIBE_DESKTOP_WAV_RESERVED_SECTORS := 1024
WALLPAPER_IMAGE_LBA := $(shell echo $$(( $(CRAFT_SIGN_IMAGE_LBA) + 128 )))
VIBE_BOOT_WAV_IMAGE_LBA := $(shell echo $$(( $(WALLPAPER_IMAGE_LBA) + $(WALLPAPER_RESERVED_SECTORS) )))
VIBE_DESKTOP_WAV_IMAGE_LBA := $(shell echo $$(( $(VIBE_BOOT_WAV_IMAGE_LBA) + $(VIBE_BOOT_WAV_RESERVED_SECTORS) )))
BOOTLOADER_BG_IMAGE_LBA := $(shell echo $$(( $(VIBE_DESKTOP_WAV_IMAGE_LBA) + $(VIBE_DESKTOP_WAV_RESERVED_SECTORS) )))
ICON_APP_DEFAULT_48_BYTES := $(shell wc -c < $(ICON_APP_DEFAULT_48_SRC) | tr -d '[:space:]')
ICON_APP_DEFAULT_48_SECTORS := $(shell echo $$(( ($(ICON_APP_DEFAULT_48_BYTES) + 511) / 512 )))
ICON_APP_DEFAULT_16_BYTES := $(shell wc -c < $(ICON_APP_DEFAULT_16_SRC) | tr -d '[:space:]')
ICON_APP_DEFAULT_16_SECTORS := $(shell echo $$(( ($(ICON_APP_DEFAULT_16_BYTES) + 511) / 512 )))
ICON_TERMINAL_16_BYTES := $(shell wc -c < $(ICON_TERMINAL_16_SRC) | tr -d '[:space:]')
ICON_TERMINAL_16_SECTORS := $(shell echo $$(( ($(ICON_TERMINAL_16_BYTES) + 511) / 512 )))
ICON_CLOCK_16_BYTES := $(shell wc -c < $(ICON_CLOCK_16_SRC) | tr -d '[:space:]')
ICON_CLOCK_16_SECTORS := $(shell echo $$(( ($(ICON_CLOCK_16_BYTES) + 511) / 512 )))
ICON_CALCULATOR_16_BYTES := $(shell wc -c < $(ICON_CALCULATOR_16_SRC) | tr -d '[:space:]')
ICON_CALCULATOR_16_SECTORS := $(shell echo $$(( ($(ICON_CALCULATOR_16_BYTES) + 511) / 512 )))
ICON_EDITOR_24_BYTES := $(shell wc -c < $(ICON_EDITOR_24_SRC) | tr -d '[:space:]')
ICON_EDITOR_24_SECTORS := $(shell echo $$(( ($(ICON_EDITOR_24_BYTES) + 511) / 512 )))
ICON_TASKS_16_BYTES := $(shell wc -c < $(ICON_TASKS_16_SRC) | tr -d '[:space:]')
ICON_TASKS_16_SECTORS := $(shell echo $$(( ($(ICON_TASKS_16_BYTES) + 511) / 512 )))
ICON_IMAGEVIEWER_16_BYTES := $(shell wc -c < $(ICON_IMAGEVIEWER_16_SRC) | tr -d '[:space:]')
ICON_IMAGEVIEWER_16_SECTORS := $(shell echo $$(( ($(ICON_IMAGEVIEWER_16_BYTES) + 511) / 512 )))
ICON_WALLPAPER_16_BYTES := $(shell wc -c < $(ICON_WALLPAPER_16_SRC) | tr -d '[:space:]')
ICON_WALLPAPER_16_SECTORS := $(shell echo $$(( ($(ICON_WALLPAPER_16_BYTES) + 511) / 512 )))
ICON_PREFERENCES_16_BYTES := $(shell wc -c < $(ICON_PREFERENCES_16_SRC) | tr -d '[:space:]')
ICON_PREFERENCES_16_SECTORS := $(shell echo $$(( ($(ICON_PREFERENCES_16_BYTES) + 511) / 512 )))
ICON_APP_MENU_16_BYTES := $(shell wc -c < $(ICON_APP_MENU_16_SRC) | tr -d '[:space:]')
ICON_APP_MENU_16_SECTORS := $(shell echo $$(( ($(ICON_APP_MENU_16_BYTES) + 511) / 512 )))
ICON_NETWORK_16_BYTES := $(shell wc -c < $(ICON_NETWORK_16_SRC) | tr -d '[:space:]')
ICON_NETWORK_16_SECTORS := $(shell echo $$(( ($(ICON_NETWORK_16_BYTES) + 511) / 512 )))
ICON_TEXT_16_BYTES := $(shell wc -c < $(ICON_TEXT_16_SRC) | tr -d '[:space:]')
ICON_TEXT_16_SECTORS := $(shell echo $$(( ($(ICON_TEXT_16_BYTES) + 511) / 512 )))
ICON_AUDIO_PLAYER_16_BYTES := $(shell wc -c < $(ICON_AUDIO_PLAYER_16_SRC) | tr -d '[:space:]')
ICON_AUDIO_PLAYER_16_SECTORS := $(shell echo $$(( ($(ICON_AUDIO_PLAYER_16_BYTES) + 511) / 512 )))
ICON_AUDIO_PLAYER_24_BYTES := $(shell wc -c < $(ICON_AUDIO_PLAYER_24_SRC) | tr -d '[:space:]')
ICON_AUDIO_PLAYER_24_SECTORS := $(shell echo $$(( ($(ICON_AUDIO_PLAYER_24_BYTES) + 511) / 512 )))
ICON_SKETCHPAD_22_BYTES := $(shell wc -c < $(ICON_SKETCHPAD_22_SRC) | tr -d '[:space:]')
ICON_SKETCHPAD_22_SECTORS := $(shell echo $$(( ($(ICON_SKETCHPAD_22_BYTES) + 511) / 512 )))
ICON_GAME_SNAKE_16_BYTES := $(shell wc -c < $(ICON_GAME_SNAKE_16_SRC) | tr -d '[:space:]')
ICON_GAME_SNAKE_16_SECTORS := $(shell echo $$(( ($(ICON_GAME_SNAKE_16_BYTES) + 511) / 512 )))
ICON_GAME_BOARD_16_BYTES := $(shell wc -c < $(ICON_GAME_BOARD_16_SRC) | tr -d '[:space:]')
ICON_GAME_BOARD_16_SECTORS := $(shell echo $$(( ($(ICON_GAME_BOARD_16_BYTES) + 511) / 512 )))
ICON_GAME_MINES_16_BYTES := $(shell wc -c < $(ICON_GAME_MINES_16_SRC) | tr -d '[:space:]')
ICON_GAME_MINES_16_SECTORS := $(shell echo $$(( ($(ICON_GAME_MINES_16_BYTES) + 511) / 512 )))
ICON_GAME_ARCADE_16_BYTES := $(shell wc -c < $(ICON_GAME_ARCADE_16_SRC) | tr -d '[:space:]')
ICON_GAME_ARCADE_16_SECTORS := $(shell echo $$(( ($(ICON_GAME_ARCADE_16_BYTES) + 511) / 512 )))
ICON_GAME_INVADERS_16_BYTES := $(shell wc -c < $(ICON_GAME_INVADERS_16_SRC) | tr -d '[:space:]')
ICON_GAME_INVADERS_16_SECTORS := $(shell echo $$(( ($(ICON_GAME_INVADERS_16_BYTES) + 511) / 512 )))
ICON_CRAFT_24_BYTES := $(shell wc -c < $(ICON_CRAFT_24_SRC) | tr -d '[:space:]')
ICON_CRAFT_24_SECTORS := $(shell echo $$(( ($(ICON_CRAFT_24_BYTES) + 511) / 512 )))
ICON_CRAFT_48_BYTES := $(shell wc -c < $(ICON_CRAFT_48_SRC) | tr -d '[:space:]')
ICON_CRAFT_48_SECTORS := $(shell echo $$(( ($(ICON_CRAFT_48_BYTES) + 511) / 512 )))
ICON_FOLDER_16_BYTES := $(shell wc -c < $(ICON_FOLDER_16_SRC) | tr -d '[:space:]')
ICON_FOLDER_16_SECTORS := $(shell echo $$(( ($(ICON_FOLDER_16_BYTES) + 511) / 512 )))
ICON_TRASH_16_BYTES := $(shell wc -c < $(ICON_TRASH_16_SRC) | tr -d '[:space:]')
ICON_TRASH_16_SECTORS := $(shell echo $$(( ($(ICON_TRASH_16_BYTES) + 511) / 512 )))
ICON_FOLDER_OPEN_32_BYTES := $(shell wc -c < $(ICON_FOLDER_OPEN_32_SRC) | tr -d '[:space:]')
ICON_FOLDER_OPEN_32_SECTORS := $(shell echo $$(( ($(ICON_FOLDER_OPEN_32_BYTES) + 511) / 512 )))
ICON_FOLDER_DOCS_32_BYTES := $(shell wc -c < $(ICON_FOLDER_DOCS_32_SRC) | tr -d '[:space:]')
ICON_FOLDER_DOCS_32_SECTORS := $(shell echo $$(( ($(ICON_FOLDER_DOCS_32_BYTES) + 511) / 512 )))
ICON_FOLDER_MUSIC_32_BYTES := $(shell wc -c < $(ICON_FOLDER_MUSIC_32_SRC) | tr -d '[:space:]')
ICON_FOLDER_MUSIC_32_SECTORS := $(shell echo $$(( ($(ICON_FOLDER_MUSIC_32_BYTES) + 511) / 512 )))
ICON_FOLDER_PICTURES_32_BYTES := $(shell wc -c < $(ICON_FOLDER_PICTURES_32_SRC) | tr -d '[:space:]')
ICON_FOLDER_PICTURES_32_SECTORS := $(shell echo $$(( ($(ICON_FOLDER_PICTURES_32_BYTES) + 511) / 512 )))
ICON_FOLDER_VIDEO_32_BYTES := $(shell wc -c < $(ICON_FOLDER_VIDEO_32_SRC) | tr -d '[:space:]')
ICON_FOLDER_VIDEO_32_SECTORS := $(shell echo $$(( ($(ICON_FOLDER_VIDEO_32_BYTES) + 511) / 512 )))
ICON_ACTION_GO_UP_16_BYTES := $(shell wc -c < $(ICON_ACTION_GO_UP_16_SRC) | tr -d '[:space:]')
ICON_ACTION_GO_UP_16_SECTORS := $(shell echo $$(( ($(ICON_ACTION_GO_UP_16_BYTES) + 511) / 512 )))
ICON_ACTION_OPEN_16_BYTES := $(shell wc -c < $(ICON_ACTION_OPEN_16_SRC) | tr -d '[:space:]')
ICON_ACTION_OPEN_16_SECTORS := $(shell echo $$(( ($(ICON_ACTION_OPEN_16_BYTES) + 511) / 512 )))
ICON_ACTION_NEW_16_BYTES := $(shell wc -c < $(ICON_ACTION_NEW_16_SRC) | tr -d '[:space:]')
ICON_ACTION_NEW_16_SECTORS := $(shell echo $$(( ($(ICON_ACTION_NEW_16_BYTES) + 511) / 512 )))
ICON_ACTION_COPY_16_BYTES := $(shell wc -c < $(ICON_ACTION_COPY_16_SRC) | tr -d '[:space:]')
ICON_ACTION_COPY_16_SECTORS := $(shell echo $$(( ($(ICON_ACTION_COPY_16_BYTES) + 511) / 512 )))
ICON_ACTION_PASTE_16_BYTES := $(shell wc -c < $(ICON_ACTION_PASTE_16_SRC) | tr -d '[:space:]')
ICON_ACTION_PASTE_16_SECTORS := $(shell echo $$(( ($(ICON_ACTION_PASTE_16_BYTES) + 511) / 512 )))
ICON_ACTION_RENAME_16_BYTES := $(shell wc -c < $(ICON_ACTION_RENAME_16_SRC) | tr -d '[:space:]')
ICON_ACTION_RENAME_16_SECTORS := $(shell echo $$(( ($(ICON_ACTION_RENAME_16_BYTES) + 511) / 512 )))
ICON_ACTION_SAVE_16_BYTES := $(shell wc -c < $(ICON_ACTION_SAVE_16_SRC) | tr -d '[:space:]')
ICON_ACTION_SAVE_16_SECTORS := $(shell echo $$(( ($(ICON_ACTION_SAVE_16_BYTES) + 511) / 512 )))
ICON_ACTION_SAVE_AS_16_BYTES := $(shell wc -c < $(ICON_ACTION_SAVE_AS_16_SRC) | tr -d '[:space:]')
ICON_ACTION_SAVE_AS_16_SECTORS := $(shell echo $$(( ($(ICON_ACTION_SAVE_AS_16_BYTES) + 511) / 512 )))
ICON_ACTION_EXPORT_16_BYTES := $(shell wc -c < $(ICON_ACTION_EXPORT_16_SRC) | tr -d '[:space:]')
ICON_ACTION_EXPORT_16_SECTORS := $(shell echo $$(( ($(ICON_ACTION_EXPORT_16_BYTES) + 511) / 512 )))
ICON_ACTION_CANCEL_16_BYTES := $(shell wc -c < $(ICON_ACTION_CANCEL_16_SRC) | tr -d '[:space:]')
ICON_ACTION_CANCEL_16_SECTORS := $(shell echo $$(( ($(ICON_ACTION_CANCEL_16_BYTES) + 511) / 512 )))
ICON_ACTION_APPLY_16_BYTES := $(shell wc -c < $(ICON_ACTION_APPLY_16_SRC) | tr -d '[:space:]')
ICON_ACTION_APPLY_16_SECTORS := $(shell echo $$(( ($(ICON_ACTION_APPLY_16_BYTES) + 511) / 512 )))
ICON_ACTION_WINDOW_CLOSE_16_BYTES := $(shell wc -c < $(ICON_ACTION_WINDOW_CLOSE_16_SRC) | tr -d '[:space:]')
ICON_ACTION_WINDOW_CLOSE_16_SECTORS := $(shell echo $$(( ($(ICON_ACTION_WINDOW_CLOSE_16_BYTES) + 511) / 512 )))
ICON_ACTION_WINDOW_NEW_16_BYTES := $(shell wc -c < $(ICON_ACTION_WINDOW_NEW_16_SRC) | tr -d '[:space:]')
ICON_ACTION_WINDOW_NEW_16_SECTORS := $(shell echo $$(( ($(ICON_ACTION_WINDOW_NEW_16_BYTES) + 511) / 512 )))
ICON_ACTION_GO_DOWN_16_BYTES := $(shell wc -c < $(ICON_ACTION_GO_DOWN_16_SRC) | tr -d '[:space:]')
ICON_ACTION_GO_DOWN_16_SECTORS := $(shell echo $$(( ($(ICON_ACTION_GO_DOWN_16_BYTES) + 511) / 512 )))
ICON_ACTION_TRASH_16_BYTES := $(shell wc -c < $(ICON_ACTION_TRASH_16_SRC) | tr -d '[:space:]')
ICON_ACTION_TRASH_16_SECTORS := $(shell echo $$(( ($(ICON_ACTION_TRASH_16_BYTES) + 511) / 512 )))
ICON_FOLDER_48_BYTES := $(shell wc -c < $(ICON_FOLDER_48_SRC) | tr -d '[:space:]')
ICON_FOLDER_48_SECTORS := $(shell echo $$(( ($(ICON_FOLDER_48_BYTES) + 511) / 512 )))
ICON_TRASH_48_BYTES := $(shell wc -c < $(ICON_TRASH_48_SRC) | tr -d '[:space:]')
ICON_TRASH_48_SECTORS := $(shell echo $$(( ($(ICON_TRASH_48_BYTES) + 511) / 512 )))
ICON_AUDIO_ZERO_16_BYTES := $(shell wc -c < $(ICON_AUDIO_ZERO_16_SRC) | tr -d '[:space:]')
ICON_AUDIO_ZERO_16_SECTORS := $(shell echo $$(( ($(ICON_AUDIO_ZERO_16_BYTES) + 511) / 512 )))
ICON_AUDIO_LOW_16_BYTES := $(shell wc -c < $(ICON_AUDIO_LOW_16_SRC) | tr -d '[:space:]')
ICON_AUDIO_LOW_16_SECTORS := $(shell echo $$(( ($(ICON_AUDIO_LOW_16_BYTES) + 511) / 512 )))
ICON_AUDIO_MEDIUM_16_BYTES := $(shell wc -c < $(ICON_AUDIO_MEDIUM_16_SRC) | tr -d '[:space:]')
ICON_AUDIO_MEDIUM_16_SECTORS := $(shell echo $$(( ($(ICON_AUDIO_MEDIUM_16_BYTES) + 511) / 512 )))
ICON_AUDIO_HIGH_16_BYTES := $(shell wc -c < $(ICON_AUDIO_HIGH_16_SRC) | tr -d '[:space:]')
ICON_AUDIO_HIGH_16_SECTORS := $(shell echo $$(( ($(ICON_AUDIO_HIGH_16_BYTES) + 511) / 512 )))
ICON_WIFI_NONE_16_BYTES := $(shell wc -c < $(ICON_WIFI_NONE_16_SRC) | tr -d '[:space:]')
ICON_WIFI_NONE_16_SECTORS := $(shell echo $$(( ($(ICON_WIFI_NONE_16_BYTES) + 511) / 512 )))
ICON_WIFI_MEDIUM_16_BYTES := $(shell wc -c < $(ICON_WIFI_MEDIUM_16_SRC) | tr -d '[:space:]')
ICON_WIFI_MEDIUM_16_SECTORS := $(shell echo $$(( ($(ICON_WIFI_MEDIUM_16_BYTES) + 511) / 512 )))
ICON_WIFI_FULL_16_BYTES := $(shell wc -c < $(ICON_WIFI_FULL_16_SRC) | tr -d '[:space:]')
ICON_WIFI_FULL_16_SECTORS := $(shell echo $$(( ($(ICON_WIFI_FULL_16_BYTES) + 511) / 512 )))
ICON_ETHERNET_CONNECTED_24_BYTES := $(shell wc -c < $(ICON_ETHERNET_CONNECTED_24_SRC) | tr -d '[:space:]')
ICON_ETHERNET_CONNECTED_24_SECTORS := $(shell echo $$(( ($(ICON_ETHERNET_CONNECTED_24_BYTES) + 511) / 512 )))
ICON_ETHERNET_DISCONNECTED_24_BYTES := $(shell wc -c < $(ICON_ETHERNET_DISCONNECTED_24_SRC) | tr -d '[:space:]')
ICON_ETHERNET_DISCONNECTED_24_SECTORS := $(shell echo $$(( ($(ICON_ETHERNET_DISCONNECTED_24_BYTES) + 511) / 512 )))
ICON_APP_DEFAULT_48_IMAGE_LBA := $(shell echo $$(( $(BOOTLOADER_BG_IMAGE_LBA) + 6312 )))
ICON_FOLDER_48_IMAGE_LBA := $(shell echo $$(( $(ICON_APP_DEFAULT_48_IMAGE_LBA) + $(ICON_APP_DEFAULT_48_SECTORS) )))
ICON_TRASH_48_IMAGE_LBA := $(shell echo $$(( $(ICON_FOLDER_48_IMAGE_LBA) + $(ICON_FOLDER_48_SECTORS) )))
ICON_AUDIO_ZERO_16_IMAGE_LBA := $(shell echo $$(( $(ICON_TRASH_48_IMAGE_LBA) + $(ICON_TRASH_48_SECTORS) )))
ICON_AUDIO_LOW_16_IMAGE_LBA := $(shell echo $$(( $(ICON_AUDIO_ZERO_16_IMAGE_LBA) + $(ICON_AUDIO_ZERO_16_SECTORS) )))
ICON_AUDIO_MEDIUM_16_IMAGE_LBA := $(shell echo $$(( $(ICON_AUDIO_LOW_16_IMAGE_LBA) + $(ICON_AUDIO_LOW_16_SECTORS) )))
ICON_AUDIO_HIGH_16_IMAGE_LBA := $(shell echo $$(( $(ICON_AUDIO_MEDIUM_16_IMAGE_LBA) + $(ICON_AUDIO_MEDIUM_16_SECTORS) )))
ICON_WIFI_NONE_16_IMAGE_LBA := $(shell echo $$(( $(ICON_AUDIO_HIGH_16_IMAGE_LBA) + $(ICON_AUDIO_HIGH_16_SECTORS) )))
ICON_WIFI_MEDIUM_16_IMAGE_LBA := $(shell echo $$(( $(ICON_WIFI_NONE_16_IMAGE_LBA) + $(ICON_WIFI_NONE_16_SECTORS) )))
ICON_WIFI_FULL_16_IMAGE_LBA := $(shell echo $$(( $(ICON_WIFI_MEDIUM_16_IMAGE_LBA) + $(ICON_WIFI_MEDIUM_16_SECTORS) )))
ICON_ETHERNET_CONNECTED_24_IMAGE_LBA := $(shell echo $$(( $(ICON_WIFI_FULL_16_IMAGE_LBA) + $(ICON_WIFI_FULL_16_SECTORS) )))
ICON_ETHERNET_DISCONNECTED_24_IMAGE_LBA := $(shell echo $$(( $(ICON_ETHERNET_CONNECTED_24_IMAGE_LBA) + $(ICON_ETHERNET_CONNECTED_24_SECTORS) )))
ICON_APP_DEFAULT_16_IMAGE_LBA := $(shell echo $$(( $(ICON_ETHERNET_DISCONNECTED_24_IMAGE_LBA) + $(ICON_ETHERNET_DISCONNECTED_24_SECTORS) )))
ICON_TERMINAL_16_IMAGE_LBA := $(shell echo $$(( $(ICON_APP_DEFAULT_16_IMAGE_LBA) + $(ICON_APP_DEFAULT_16_SECTORS) )))
ICON_CLOCK_16_IMAGE_LBA := $(shell echo $$(( $(ICON_TERMINAL_16_IMAGE_LBA) + $(ICON_TERMINAL_16_SECTORS) )))
ICON_CALCULATOR_16_IMAGE_LBA := $(shell echo $$(( $(ICON_CLOCK_16_IMAGE_LBA) + $(ICON_CLOCK_16_SECTORS) )))
ICON_EDITOR_24_IMAGE_LBA := $(shell echo $$(( $(ICON_CALCULATOR_16_IMAGE_LBA) + $(ICON_CALCULATOR_16_SECTORS) )))
ICON_TASKS_16_IMAGE_LBA := $(shell echo $$(( $(ICON_EDITOR_24_IMAGE_LBA) + $(ICON_EDITOR_24_SECTORS) )))
ICON_IMAGEVIEWER_16_IMAGE_LBA := $(shell echo $$(( $(ICON_TASKS_16_IMAGE_LBA) + $(ICON_TASKS_16_SECTORS) )))
ICON_WALLPAPER_16_IMAGE_LBA := $(shell echo $$(( $(ICON_IMAGEVIEWER_16_IMAGE_LBA) + $(ICON_IMAGEVIEWER_16_SECTORS) )))
ICON_PREFERENCES_16_IMAGE_LBA := $(shell echo $$(( $(ICON_WALLPAPER_16_IMAGE_LBA) + $(ICON_WALLPAPER_16_SECTORS) )))
ICON_NETWORK_16_IMAGE_LBA := $(shell echo $$(( $(ICON_PREFERENCES_16_IMAGE_LBA) + $(ICON_PREFERENCES_16_SECTORS) )))
ICON_FOLDER_16_IMAGE_LBA := $(shell echo $$(( $(ICON_NETWORK_16_IMAGE_LBA) + $(ICON_NETWORK_16_SECTORS) )))
ICON_TRASH_16_IMAGE_LBA := $(shell echo $$(( $(ICON_FOLDER_16_IMAGE_LBA) + $(ICON_FOLDER_16_SECTORS) )))
ICON_FOLDER_OPEN_32_IMAGE_LBA := $(shell echo $$(( $(ICON_TRASH_16_IMAGE_LBA) + $(ICON_TRASH_16_SECTORS) )))
ICON_FOLDER_DOCS_32_IMAGE_LBA := $(shell echo $$(( $(ICON_FOLDER_OPEN_32_IMAGE_LBA) + $(ICON_FOLDER_OPEN_32_SECTORS) )))
ICON_FOLDER_MUSIC_32_IMAGE_LBA := $(shell echo $$(( $(ICON_FOLDER_DOCS_32_IMAGE_LBA) + $(ICON_FOLDER_DOCS_32_SECTORS) )))
ICON_FOLDER_PICTURES_32_IMAGE_LBA := $(shell echo $$(( $(ICON_FOLDER_MUSIC_32_IMAGE_LBA) + $(ICON_FOLDER_MUSIC_32_SECTORS) )))
ICON_FOLDER_VIDEO_32_IMAGE_LBA := $(shell echo $$(( $(ICON_FOLDER_PICTURES_32_IMAGE_LBA) + $(ICON_FOLDER_PICTURES_32_SECTORS) )))
ICON_ACTION_GO_UP_16_IMAGE_LBA := $(shell echo $$(( $(ICON_FOLDER_VIDEO_32_IMAGE_LBA) + $(ICON_FOLDER_VIDEO_32_SECTORS) )))
ICON_ACTION_NEW_16_IMAGE_LBA := $(shell echo $$(( $(ICON_ACTION_GO_UP_16_IMAGE_LBA) + $(ICON_ACTION_GO_UP_16_SECTORS) )))
ICON_ACTION_COPY_16_IMAGE_LBA := $(shell echo $$(( $(ICON_ACTION_NEW_16_IMAGE_LBA) + $(ICON_ACTION_NEW_16_SECTORS) )))
ICON_ACTION_PASTE_16_IMAGE_LBA := $(shell echo $$(( $(ICON_ACTION_COPY_16_IMAGE_LBA) + $(ICON_ACTION_COPY_16_SECTORS) )))
ICON_ACTION_TRASH_16_IMAGE_LBA := $(shell echo $$(( $(ICON_ACTION_PASTE_16_IMAGE_LBA) + $(ICON_ACTION_PASTE_16_SECTORS) )))
ICON_AUDIO_PLAYER_24_IMAGE_LBA := $(shell echo $$(( $(ICON_ACTION_TRASH_16_IMAGE_LBA) + $(ICON_ACTION_TRASH_16_SECTORS) )))
ICON_THEME_INDEX_IMAGE_LBA := $(shell echo $$(( $(ICON_AUDIO_PLAYER_24_IMAGE_LBA) + $(ICON_AUDIO_PLAYER_24_SECTORS) )))
ICON_APP_MENU_16_IMAGE_LBA := $(shell echo $$(( $(ICON_THEME_INDEX_IMAGE_LBA) + $(ICON_THEME_INDEX_SECTORS) )))
ICON_AUDIO_PLAYER_16_IMAGE_LBA := $(shell echo $$(( $(ICON_APP_MENU_16_IMAGE_LBA) + $(ICON_APP_MENU_16_SECTORS) )))
ICON_TEXT_16_IMAGE_LBA := $(shell echo $$(( $(ICON_AUDIO_PLAYER_16_IMAGE_LBA) + $(ICON_AUDIO_PLAYER_16_SECTORS) )))
ICON_SKETCHPAD_22_IMAGE_LBA := $(shell echo $$(( $(ICON_TEXT_16_IMAGE_LBA) + $(ICON_TEXT_16_SECTORS) )))
ICON_GAME_SNAKE_16_IMAGE_LBA := $(shell echo $$(( $(ICON_SKETCHPAD_22_IMAGE_LBA) + $(ICON_SKETCHPAD_22_SECTORS) )))
ICON_GAME_BOARD_16_IMAGE_LBA := $(shell echo $$(( $(ICON_GAME_SNAKE_16_IMAGE_LBA) + $(ICON_GAME_SNAKE_16_SECTORS) )))
ICON_GAME_MINES_16_IMAGE_LBA := $(shell echo $$(( $(ICON_GAME_BOARD_16_IMAGE_LBA) + $(ICON_GAME_BOARD_16_SECTORS) )))
ICON_GAME_ARCADE_16_IMAGE_LBA := $(shell echo $$(( $(ICON_GAME_MINES_16_IMAGE_LBA) + $(ICON_GAME_MINES_16_SECTORS) )))
ICON_GAME_INVADERS_16_IMAGE_LBA := $(shell echo $$(( $(ICON_GAME_ARCADE_16_IMAGE_LBA) + $(ICON_GAME_ARCADE_16_SECTORS) )))
ICON_CRAFT_24_IMAGE_LBA := $(shell echo $$(( $(ICON_GAME_INVADERS_16_IMAGE_LBA) + $(ICON_GAME_INVADERS_16_SECTORS) )))
ICON_CRAFT_48_IMAGE_LBA := $(shell echo $$(( $(ICON_CRAFT_24_IMAGE_LBA) + $(ICON_CRAFT_24_SECTORS) )))
ICON_ACTION_OPEN_16_IMAGE_LBA := $(shell echo $$(( $(ICON_CRAFT_48_IMAGE_LBA) + $(ICON_CRAFT_48_SECTORS) )))
ICON_ACTION_RENAME_16_IMAGE_LBA := $(shell echo $$(( $(ICON_ACTION_OPEN_16_IMAGE_LBA) + $(ICON_ACTION_OPEN_16_SECTORS) )))
ICON_ACTION_SAVE_16_IMAGE_LBA := $(shell echo $$(( $(ICON_ACTION_RENAME_16_IMAGE_LBA) + $(ICON_ACTION_RENAME_16_SECTORS) )))
ICON_ACTION_SAVE_AS_16_IMAGE_LBA := $(shell echo $$(( $(ICON_ACTION_SAVE_16_IMAGE_LBA) + $(ICON_ACTION_SAVE_16_SECTORS) )))
ICON_ACTION_EXPORT_16_IMAGE_LBA := $(shell echo $$(( $(ICON_ACTION_SAVE_AS_16_IMAGE_LBA) + $(ICON_ACTION_SAVE_AS_16_SECTORS) )))
ICON_ACTION_CANCEL_16_IMAGE_LBA := $(shell echo $$(( $(ICON_ACTION_EXPORT_16_IMAGE_LBA) + $(ICON_ACTION_EXPORT_16_SECTORS) )))
ICON_ACTION_APPLY_16_IMAGE_LBA := $(shell echo $$(( $(ICON_ACTION_CANCEL_16_IMAGE_LBA) + $(ICON_ACTION_CANCEL_16_SECTORS) )))
ICON_ACTION_WINDOW_CLOSE_16_IMAGE_LBA := $(shell echo $$(( $(ICON_ACTION_APPLY_16_IMAGE_LBA) + $(ICON_ACTION_APPLY_16_SECTORS) )))
ICON_ACTION_WINDOW_NEW_16_IMAGE_LBA := $(shell echo $$(( $(ICON_ACTION_WINDOW_CLOSE_16_IMAGE_LBA) + $(ICON_ACTION_WINDOW_CLOSE_16_SECTORS) )))
ICON_ACTION_GO_DOWN_16_IMAGE_LBA := $(shell echo $$(( $(ICON_ACTION_WINDOW_NEW_16_IMAGE_LBA) + $(ICON_ACTION_WINDOW_NEW_16_SECTORS) )))
IMAGE_ASSET_MANIFEST := $(BUILD_DIR)/image-assets.manifest
DATA_IMAGE := $(BUILD_DIR)/data-partition.img
DATA_IMAGE_MANIFEST := $(BUILD_DIR)/data-partition.manifest
BOOT_VOLUME_MANIFEST := $(BUILD_DIR)/boot-volume-layout.txt
BOOT_POLICY_MANIFEST := $(BUILD_DIR)/boot-policy.txt
PHASE6_REPORT := $(BUILD_DIR)/phase6-validation.md
PHASED_REPORT := $(BUILD_DIR)/phase-d-validation.md
PHASEG_REPORT := $(BUILD_DIR)/phase-g-validation.md
MODULAR_APPS_REPORT := $(BUILD_DIR)/modular-apps-validation.md
GPU_BACKENDS_REPORT := $(BUILD_DIR)/gpu-backends-report.md
AUDIO_STACK_REPORT := $(BUILD_DIR)/audio-stack-validation.md
SMP_REPORT := $(BUILD_DIR)/smp-validation.md
GPU_BACKENDS_I915_EXPERIMENTAL_REPORT := $(BUILD_DIR)/gpu-backends-i915-experimental-report.md
GPU_BACKENDS_RECOVERY_REPORT := $(BUILD_DIR)/gpu-backends-recovery-report.md
CRAFT_UPSTREAM_EXPERIMENTAL ?= 1

BOOT_SMOKE_IMAGE := $(BUILD_DIR)/boot-smoke.img
BOOT_SMOKE_DATA_IMAGE := $(BUILD_DIR)/data-partition.boot-smoke.img
BOOT_SMOKE_DATA_IMAGE_MANIFEST := $(BUILD_DIR)/data-partition.boot-smoke.manifest
BOOT_SMOKE_IMAGE_ASSET_MANIFEST := $(BUILD_DIR)/image-assets.boot-smoke.manifest
BOOT_SMOKE_VOLUME_MANIFEST := $(BUILD_DIR)/boot-volume-layout.boot-smoke.txt
BOOT_SMOKE_POLICY_MANIFEST := $(BUILD_DIR)/boot-policy.boot-smoke.txt
BOOT_SMOKE_PHASE6_REPORT := $(BUILD_DIR)/phase6-validation-boot-smoke.md
BOOTLOADER_BG_SMOKE_BIN := $(BUILD_DIR)/bootloader-bg.smoke.bin
WALLPAPER_RUNTIME_SMOKE_PNG := $(BUILD_DIR)/wallpaper-runtime.smoke.png

# Include BSD USB build rules
-include kernel/drivers/usb/Build.usb.mk


# Build.ksh.mk must be included early for KSH_LIB variable
-include compat/bin/ksh/Build.ksh.mk

# Kernel sources - kernel only, no stage2
KERNEL_SRCS := $(shell find kernel -name '*.c' ! -name '* *' ! -path 'kernel/drivers/usb/bsd_usb_impl.c' ! -path 'kernel/drivers/usb/bsd_usb_pci.c')
KEYMAP_SRCS := $(shell find kernel/drivers/input/keymaps -name '*.c' ! -name '* *')
KERNEL_OBJS := $(addprefix $(BUILD_DIR)/,$(patsubst %.c,%.o,$(KERNEL_SRCS)))
KERNEL_OBJS += $(BSD_USB_OBJS)
KEYMAP_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(KEYMAP_SRCS))

KERNEL_ASM_SRCS := $(shell find kernel_asm -name '*.asm' ! -name '* *')
KERNEL_ASM_OBJS := $(patsubst kernel_asm/%.asm,$(BUILD_DIR)/kernel_asm/%.o,$(KERNEL_ASM_SRCS))

# Userland linked into the kernel image.
USERLAND_SRCS := \
	$(USERLAND_DIR)/userland.c \
	$(USERLAND_DIR)/modules/shell.c \
	$(USERLAND_DIR)/modules/busybox.c \
	$(USERLAND_DIR)/modules/console.c \
	$(USERLAND_DIR)/modules/fs.c \
	$(USERLAND_DIR)/modules/bmp.c \
	$(USERLAND_DIR)/modules/image.c \
	$(USERLAND_DIR)/modules/icon_theme.c \
	$(USERLAND_DIR)/modules/lang_loader.c \
	$(USERLAND_DIR)/modules/utils.c \
	$(USERLAND_DIR)/modules/syscalls.c \
	$(USERLAND_DIR)/modules/ssp.c \
	$(USERLAND_DIR)/modules/ui.c \
	$(USERLAND_DIR)/modules/dirty_rects.c \
	$(USERLAND_DIR)/modules/ui_clip.c \
	$(USERLAND_DIR)/modules/ui_cursor.c \
	$(USERLAND_DIR)/sectorc/sectorc_main.c \
	$(USERLAND_DIR)/sectorc/sectorc_driver.c \
	$(USERLAND_DIR)/sectorc/sectorc_port.c \
	$(USERLAND_DIR)/sectorc/sectorc_runtime.c \
	$(USERLAND_DIR)/sectorc/sectorc_exec.c \
	$(USERLAND_DIR)/lua/lua_main.c \
	$(USERLAND_DIR)/lua/lua_repl.c \
	$(USERLAND_DIR)/lua/lua_runner.c \
	$(USERLAND_DIR)/lua/lua_runtime.c \
	$(USERLAND_DIR)/lua/lua_bindings_console.c \
	$(USERLAND_DIR)/lua/lua_bindings_sys.c \
	$(USERLAND_DIR)/lua/lua_port.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/lapi.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/lauxlib.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/lbaselib.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/lcode.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/lctype.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/ldebug.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/ldump.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/ldo.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/lfunc.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/lgc.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/llex.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/lmem.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/lobject.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/lopcodes.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/lparser.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/lstate.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/lstring.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/ltable.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/ltm.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/lundump.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/lvm.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/lzio.c \
	$(USERLAND_DIR)/applications/desktop.c \
	$(USERLAND_DIR)/applications/terminal.c \
	$(USERLAND_DIR)/applications/clock.c \
	$(USERLAND_DIR)/applications/filemanager.c \
	$(USERLAND_DIR)/applications/editor.c \
	$(USERLAND_DIR)/applications/taskmgr.c \
	$(USERLAND_DIR)/applications/calculator.c \
	$(USERLAND_DIR)/applications/imageviewer.c \
	$(USERLAND_DIR)/applications/audioplayer.c \
	$(USERLAND_DIR)/applications/sketchpad.c \
	$(USERLAND_DIR)/applications/games/snake.c \
	$(USERLAND_DIR)/applications/games/tetris.c \
	$(USERLAND_DIR)/applications/games/game_2048.c \
	$(USERLAND_DIR)/applications/games/minesweeper.c \
	$(USERLAND_DIR)/applications/games/pacman.c \
	$(USERLAND_DIR)/applications/games/space_invaders.c \
	$(USERLAND_DIR)/applications/games/pong.c \
	$(USERLAND_DIR)/applications/games/donkey_kong.c \
	$(USERLAND_DIR)/applications/games/brick_race.c \
	$(USERLAND_DIR)/applications/games/flap_birb.c \
	$(USERLAND_DIR)/applications/games/doom.c \
	$(USERLAND_DIR)/applications/games/craft/craft_app.c \
	$(USERLAND_DIR)/applications/games/craft/craft_gl_compat.c \
	$(USERLAND_DIR)/applications/games/craft/craft_glfw_compat.c \
	$(USERLAND_DIR)/applications/games/craft/craft_curl_compat.c \
	$(USERLAND_DIR)/applications/games/craft/craft_thread_compat.c \
	$(USERLAND_DIR)/applications/games/craft/craft_auth_compat.c \
	$(USERLAND_DIR)/applications/games/craft/craft_client_compat.c \
	$(USERLAND_DIR)/applications/games/craft/craft_db_compat.c \
	$(USERLAND_DIR)/applications/games/craft/upstream/deps/lodepng/lodepng.c \
	$(USERLAND_DIR)/applications/games/craft/world.c \
	$(USERLAND_DIR)/applications/games/craft/noise.c \
	$(USERLAND_DIR)/applications/games/doom_port/doom_port_main.c \
	$(USERLAND_DIR)/applications/games/doom_port/doom_libc_shim.c \
	$(USERLAND_DIR)/applications/games/doom_port/i_system_vibe.c \
	$(USERLAND_DIR)/applications/games/doom_port/i_video_vibe.c \
	$(USERLAND_DIR)/applications/games/doom_port/i_sound_vibe.c \
	$(USERLAND_DIR)/applications/games/doom_port/i_net_vibe.c

ifeq ($(CRAFT_UPSTREAM_EXPERIMENTAL),1)
USERLAND_SRCS += \
	$(USERLAND_DIR)/applications/games/craft/craft_math_compat.c \
	$(USERLAND_DIR)/applications/games/craft/craft_util_compat.c \
	$(USERLAND_DIR)/applications/games/craft/craft_upstream_map.c \
	$(USERLAND_DIR)/applications/games/craft/craft_upstream_matrix.c \
	$(USERLAND_DIR)/applications/games/craft/craft_upstream_ring.c \
	$(USERLAND_DIR)/applications/games/craft/craft_upstream_sign.c \
	$(USERLAND_DIR)/applications/games/craft/craft_upstream_item.c \
	$(USERLAND_DIR)/applications/games/craft/craft_upstream_cube.c \
	$(USERLAND_DIR)/applications/games/craft/craft_upstream_runner.c
endif
USERLAND_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(USERLAND_SRCS))
KERNEL_USERLAND_SRCS := \
	$(USERLAND_DIR)/bootstrap_init.c \
	$(USERLAND_DIR)/bootstrap_hosts.c \
	$(USERLAND_DIR)/bootstrap_service.c \
	$(USERLAND_DIR)/bootstrap_runtime.c \
	$(USERLAND_DIR)/modules/shell.c \
	$(USERLAND_DIR)/modules/busybox.c \
	$(USERLAND_DIR)/modules/console.c \
	$(USERLAND_DIR)/modules/fs.c \
	$(USERLAND_DIR)/modules/lang_loader.c \
	$(USERLAND_DIR)/modules/utils.c \
	$(USERLAND_DIR)/modules/syscalls.c
KERNEL_USERLAND_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(KERNEL_USERLAND_SRCS))

DOOM_SRC_DIR := $(USERLAND_DIR)/applications/games/DOOM/linuxdoom-1.10
DOOM_CORE_SRCS := \
	$(DOOM_SRC_DIR)/doomdef.c \
	$(DOOM_SRC_DIR)/doomstat.c \
	$(DOOM_SRC_DIR)/dstrings.c \
	$(DOOM_SRC_DIR)/tables.c \
	$(DOOM_SRC_DIR)/f_finale.c \
	$(DOOM_SRC_DIR)/f_wipe.c \
	$(DOOM_SRC_DIR)/d_main.c \
	$(DOOM_SRC_DIR)/d_net.c \
	$(DOOM_SRC_DIR)/d_items.c \
	$(DOOM_SRC_DIR)/g_game.c \
	$(DOOM_SRC_DIR)/m_menu.c \
	$(DOOM_SRC_DIR)/m_misc.c \
	$(DOOM_SRC_DIR)/m_argv.c \
	$(DOOM_SRC_DIR)/m_bbox.c \
	$(DOOM_SRC_DIR)/m_fixed.c \
	$(DOOM_SRC_DIR)/m_swap.c \
	$(DOOM_SRC_DIR)/m_cheat.c \
	$(DOOM_SRC_DIR)/m_random.c \
	$(DOOM_SRC_DIR)/am_map.c \
	$(DOOM_SRC_DIR)/p_ceilng.c \
	$(DOOM_SRC_DIR)/p_doors.c \
	$(DOOM_SRC_DIR)/p_enemy.c \
	$(DOOM_SRC_DIR)/p_floor.c \
	$(DOOM_SRC_DIR)/p_inter.c \
	$(DOOM_SRC_DIR)/p_lights.c \
	$(DOOM_SRC_DIR)/p_map.c \
	$(DOOM_SRC_DIR)/p_maputl.c \
	$(DOOM_SRC_DIR)/p_plats.c \
	$(DOOM_SRC_DIR)/p_pspr.c \
	$(DOOM_SRC_DIR)/p_setup.c \
	$(DOOM_SRC_DIR)/p_sight.c \
	$(DOOM_SRC_DIR)/p_spec.c \
	$(DOOM_SRC_DIR)/p_switch.c \
	$(DOOM_SRC_DIR)/p_mobj.c \
	$(DOOM_SRC_DIR)/p_telept.c \
	$(DOOM_SRC_DIR)/p_tick.c \
	$(DOOM_SRC_DIR)/p_saveg.c \
	$(DOOM_SRC_DIR)/p_user.c \
	$(DOOM_SRC_DIR)/r_bsp.c \
	$(DOOM_SRC_DIR)/r_data.c \
	$(DOOM_SRC_DIR)/r_draw.c \
	$(DOOM_SRC_DIR)/r_main.c \
	$(DOOM_SRC_DIR)/r_plane.c \
	$(DOOM_SRC_DIR)/r_segs.c \
	$(DOOM_SRC_DIR)/r_sky.c \
	$(DOOM_SRC_DIR)/r_things.c \
	$(DOOM_SRC_DIR)/w_wad.c \
	$(DOOM_SRC_DIR)/wi_stuff.c \
	$(DOOM_SRC_DIR)/v_video.c \
	$(DOOM_SRC_DIR)/st_lib.c \
	$(DOOM_SRC_DIR)/st_stuff.c \
	$(DOOM_SRC_DIR)/hu_stuff.c \
	$(DOOM_SRC_DIR)/hu_lib.c \
	$(DOOM_SRC_DIR)/s_sound.c \
	$(DOOM_SRC_DIR)/z_zone.c \
	$(DOOM_SRC_DIR)/info.c \
	$(DOOM_SRC_DIR)/sounds.c
DOOM_CORE_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(DOOM_CORE_SRCS))
DOOM_SYMBOL_REMAP = \
	-Dstdout=doom_stdout \
	-Dstderr=doom_stderr \
	-Dsndserver_filename=doom_sndserver_filename \
	-Datoi=doom_atoi \
	-Dstrcat=doom_strcat \
	-Dstrcasecmp=doom_strcasecmp \
	-Dstrncasecmp=doom_strncasecmp \
	-Dvsnprintf=doom_vsnprintf \
	-Dsnprintf=doom_snprintf \
	-Dvsprintf=doom_vsprintf \
	-Dsprintf=doom_sprintf \
	-Dvprintf=doom_vprintf \
	-Dprintf=doom_printf \
	-Dvfprintf=doom_vfprintf \
	-Dfprintf=doom_fprintf \
	-Dputchar=doom_putchar \
	-Dgetchar=doom_getchar \
	-Dputs=doom_puts \
	-Dfputc=doom_fputc \
	-Dfwrite=doom_fwrite \
	-Dfflush=doom_fflush \
	-Dfseek=doom_fseek \
	-Dftell=doom_ftell \
	-Dsetbuf=doom_setbuf \
	-Dsscanf=doom_sscanf \
	-Dfscanf=doom_fscanf \
	-Daccess=doom_access \
	-Dmkdir=doom_mkdir \
	-Dopen=doom_open \
	-Dclose=doom_close \
	-Dread=doom_read \
	-Dwrite=doom_write \
	-Dlseek=doom_lseek \
	-Dfstat=doom_fstat \
	-Dexit=doom_exit
DOOM_CFLAGS = -std=gnu17 -m32 $(CPU_ARCH_CFLAGS) -Os -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-builtin -fcf-protection=none -nostdlib -Wall -Wextra -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -I. -Iuserland/lua/include -idirafter headers -Iuserland -Ilang/include -Ilang/vendor/quickjs-ng -Ilang/vendor/mruby/include -Ilang/vendor/micropython -DNORMALUNIX -DLINUX -DSEEK_SET=0 -DSEEK_CUR=1 -DSEEK_END=2 -include stdio.h -include stdlib.h -include string.h -include userland/applications/games/doom_port/doom_libc_shim.h -Wno-sequence-point -Wno-unused-const-variable -Wno-unused-but-set-variable $(DOOM_SYMBOL_REMAP)
DOOM_PORT_SRC_DIR := $(USERLAND_DIR)/applications/games/doom_port
DOOM_PORT_CFLAGS = $(CFLAGS) -Iuserland/lua/include -include userland/applications/games/doom_port/doom_libc_shim.h $(DOOM_SYMBOL_REMAP)

USERLAND_OBJS += $(DOOM_CORE_OBJS)

$(BUILD_DIR)/$(DOOM_SRC_DIR)/%.o: $(DOOM_SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(DOOM_CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(DOOM_PORT_SRC_DIR)/%.o: $(DOOM_PORT_SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(DOOM_PORT_CFLAGS) -c $< -o $@

# QuickJS wrapper - separate app build (see below)
# QUICKJS_SRCS := lang/apps/js/quickjs_wrapper.c
# QUICKJS_OBJS := $(patsubst lang/apps/js/%.c,$(BUILD_DIR)/lang_apps_js_%.o,$(QUICKJS_SRCS))

# Add QuickJS to userland objects
# USERLAND_OBJS += $(QUICKJS_OBJS)

# $(BUILD_DIR)/lang_apps_js_%.o: lang/apps/js/%.c
# 	$(CC) $(CFLAGS) -c $< -o $@

# mruby wrapper - DISABLED: requires build artifacts (mruby/presym/id.h)
# MRUBY_SRCS := lang/apps/ruby/mruby_wrapper.c
# MRUBY_OBJS := $(patsubst lang/apps/ruby/%.c,$(BUILD_DIR)/lang_apps_ruby_%.o,$(MRUBY_SRCS))
# Add mruby to userland objects
# USERLAND_OBJS += $(MRUBY_OBJS)

# $(BUILD_DIR)/lang_apps_ruby_%.o: lang/apps/ruby/%.c
# 	$(CC) $(CFLAGS) -c $< -o $@

# MicroPython wrapper - DISABLED: requires build artifacts (py/*.h modules)
# MICROPYTHON_SRCS := lang/apps/python/micropython_wrapper.c
# MICROPYTHON_OBJS := $(patsubst lang/apps/python/%.c,$(BUILD_DIR)/lang_apps_python_%.o,$(MICROPYTHON_SRCS))

# Add MicroPython to userland objects
# USERLAND_OBJS += $(MICROPYTHON_OBJS)

$(BUILD_DIR)/lang_apps_python_%.o: lang/apps/python/%.c
	$(CC) $(CFLAGS) -c $< -o $@

BOOT_BIN := $(BUILD_DIR)/boot.bin
STAGE2_BIN := $(BUILD_DIR)/stage2.bin
MBR_BIN := $(BUILD_DIR)/mbr.bin
AP_TRAMPOLINE_BIN := $(BUILD_DIR)/ap_trampoline.bin
AP_TRAMPOLINE_OBJ := $(BUILD_DIR)/ap_trampoline_blob.o
KERNEL_ELF := $(BUILD_DIR)/kernel.elf
KERNEL_BIN := $(BUILD_DIR)/kernel.bin
USERLAND_MAIN_ELF := $(BUILD_DIR)/userland-main.elf
USERLAND_MAIN_BIN := $(BUILD_DIR)/userland-main.bin
BOOTLOADER_BG_BIN := $(BUILD_DIR)/bootloader-bg.bin
IMAGE := $(BUILD_DIR)/boot.img

# P4 classe #5 (adendo 31 probe): kernel objects now carry GCC stack
# protection. With -fno-pic -fno-pie, -mstack-protector-guard=global emits a
# DIRECT load of __stack_chk_guard (no %gs TLS, no GOT) — flat-ring0 safe.
# Symbol impl: kernel/stackguard.c; a guard hit panics with the offender's
# return address (nm-resolvable). DOOM/vendored CFLAGS keep -fno-stack-protector.
CFLAGS := -std=gnu17 -m32 $(CPU_ARCH_CFLAGS) -Os -ffreestanding -fno-pic -fno-pie -fstack-protector -mstack-protector-guard=global -fno-builtin -fcf-protection=none -nostdlib -Wall -Wextra -Werror -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -I. -idirafter headers -Iuserland -Ilang/include -Ilang/vendor/quickjs-ng -Ilang/vendor/mruby/include -Ilang/vendor/micropython
CFLAGS += -I$(APP_CATALOG_GENERATED_DIR)
CFLAGS += -MMD -MP
LUA_CFLAGS := -Iuserland/lua/include -Iuserland/lua/vendor/lua-5.4.6/src
# i915 modeset is now enabled by default (was experimental)
INTEL_I915_EXPERIMENTAL_COMMIT ?= 1
CFLAGS += -DINTEL_I915_EXPERIMENTAL_COMMIT=$(INTEL_I915_EXPERIMENTAL_COMMIT)
VIDEO_DRM_TEST_FORCE_HANDOFF_FAIL ?= 0
CFLAGS += -DVIDEO_DRM_TEST_FORCE_HANDOFF_FAIL=$(VIDEO_DRM_TEST_FORCE_HANDOFF_FAIL)
KERNEL_CFLAGS := $(CFLAGS) -D__VIBE_KERNEL__
LDFLAGS_KERNEL := -m elf_i386 -T $(LINKER_DIR)/kernel.ld -nostdlib -N --allow-multiple-definition
LDFLAGS_USERLAND := -m elf_i386 -T $(LINKER_DIR)/userland.ld -nostdlib -N
LDFLAGS_APP := -m elf_i386 -T $(LINKER_DIR)/app.ld -nostdlib -N

LIBGCC_A := $(shell $(CC) -m32 $(CPU_ARCH_CFLAGS) -print-libgcc-file-name 2>/dev/null)

HELLO_APP_BUILD_DIR := $(BUILD_DIR)/lang/hello
HELLO_APP_OBJS := \
	$(HELLO_APP_BUILD_DIR)/app_entry.o \
	$(HELLO_APP_BUILD_DIR)/app_runtime.o \
	$(HELLO_APP_BUILD_DIR)/hello_main.o
HELLO_APP_ELF := $(BUILD_DIR)/lang/hello.elf
HELLO_APP_BIN := $(BUILD_DIR)/lang/hello.app

SOUNDCTL_APP_BUILD_DIR := $(BUILD_DIR)/lang/soundctl
SOUNDCTL_APP_OBJS := \
	$(SOUNDCTL_APP_BUILD_DIR)/app_entry.o \
	$(SOUNDCTL_APP_BUILD_DIR)/app_runtime.o \
	$(SOUNDCTL_APP_BUILD_DIR)/soundctl_main.o
SOUNDCTL_APP_ELF := $(BUILD_DIR)/lang/soundctl.elf
SOUNDCTL_APP_BIN := $(BUILD_DIR)/lang/soundctl.app

AUDIOSVC_APP_BUILD_DIR := $(BUILD_DIR)/lang/audiosvc
AUDIOSVC_APP_OBJS := \
	$(AUDIOSVC_APP_BUILD_DIR)/app_entry.o \
	$(AUDIOSVC_APP_BUILD_DIR)/app_runtime.o \
	$(AUDIOSVC_APP_BUILD_DIR)/audiosvc_main.o \
	$(BUILD_DIR)/userland/modules/console.o \
	$(BUILD_DIR)/userland/modules/fs.o \
	$(BUILD_DIR)/userland/modules/utils.o \
	$(BUILD_DIR)/userland/modules/syscalls.o
AUDIOSVC_APP_ELF := $(BUILD_DIR)/lang/audiosvc.elf
AUDIOSVC_APP_BIN := $(BUILD_DIR)/lang/audiosvc.app

NETMGRD_APP_BUILD_DIR := $(BUILD_DIR)/lang/netmgrd
NETMGRD_APP_OBJS := \
	$(NETMGRD_APP_BUILD_DIR)/app_entry.o \
	$(NETMGRD_APP_BUILD_DIR)/app_runtime.o \
	$(NETMGRD_APP_BUILD_DIR)/netmgrd_main.o
NETMGRD_APP_ELF := $(BUILD_DIR)/lang/netmgrd.elf
NETMGRD_APP_BIN := $(BUILD_DIR)/lang/netmgrd.app

NETCTL_APP_BUILD_DIR := $(BUILD_DIR)/lang/netctl
NETCTL_APP_OBJS := \
	$(NETCTL_APP_BUILD_DIR)/app_entry.o \
	$(NETCTL_APP_BUILD_DIR)/app_runtime.o \
	$(NETCTL_APP_BUILD_DIR)/netctl_main.o
NETCTL_APP_ELF := $(BUILD_DIR)/lang/netctl.elf
NETCTL_APP_BIN := $(BUILD_DIR)/lang/netctl.app

FOXXOD_APP_BUILD_DIR := $(BUILD_DIR)/lang/foxxod
FOXXOD_APP_OBJS := \
	$(FOXXOD_APP_BUILD_DIR)/app_entry.o \
	$(FOXXOD_APP_BUILD_DIR)/app_runtime.o \
	$(FOXXOD_APP_BUILD_DIR)/foxxod_main.o \
	$(BUILD_DIR)/userland/modules/syscalls.o
FOXXOD_APP_ELF := $(BUILD_DIR)/lang/foxxod.elf
FOXXOD_APP_BIN := $(BUILD_DIR)/lang/foxxod.app

JS_APP_BUILD_DIR := $(BUILD_DIR)/lang/js
JS_APP_OBJS := \
	$(JS_APP_BUILD_DIR)/app_entry.o \
	$(JS_APP_BUILD_DIR)/app_runtime.o \
	$(JS_APP_BUILD_DIR)/js_main.o
JS_APP_ELF := $(BUILD_DIR)/lang/js.elf
JS_APP_BIN := $(BUILD_DIR)/lang/js.app

RUBY_APP_BUILD_DIR := $(BUILD_DIR)/lang/ruby
RUBY_APP_OBJS := \
	$(RUBY_APP_BUILD_DIR)/app_entry.o \
	$(RUBY_APP_BUILD_DIR)/app_runtime.o \
	$(RUBY_APP_BUILD_DIR)/ruby_main.o
RUBY_APP_ELF := $(BUILD_DIR)/lang/ruby.elf
RUBY_APP_BIN := $(BUILD_DIR)/lang/ruby.app

PYTHON_APP_BUILD_DIR := $(BUILD_DIR)/lang/python
PYTHON_APP_OBJS := \
	$(PYTHON_APP_BUILD_DIR)/app_entry.o \
	$(PYTHON_APP_BUILD_DIR)/app_runtime.o \
	$(PYTHON_APP_BUILD_DIR)/python_main.o
PYTHON_APP_ELF := $(BUILD_DIR)/lang/python.elf
PYTHON_APP_BIN := $(BUILD_DIR)/lang/python.app

JAVA_APP_BUILD_DIR := $(BUILD_DIR)/lang/java
JAVA_APP_OBJS := \
	$(JAVA_APP_BUILD_DIR)/app_entry.o \
	$(JAVA_APP_BUILD_DIR)/app_runtime.o \
	$(JAVA_APP_BUILD_DIR)/java_main.o
JAVA_APP_ELF := $(BUILD_DIR)/lang/java.elf
JAVA_APP_BIN := $(BUILD_DIR)/lang/java.app

JAVAC_APP_BUILD_DIR := $(BUILD_DIR)/lang/javac
JAVAC_APP_OBJS := \
	$(JAVAC_APP_BUILD_DIR)/app_entry.o \
	$(JAVAC_APP_BUILD_DIR)/app_runtime.o \
	$(JAVAC_APP_BUILD_DIR)/javac_main.o
JAVAC_APP_ELF := $(BUILD_DIR)/lang/javac.elf
JAVAC_APP_BIN := $(BUILD_DIR)/lang/javac.app

LUA_APP_BUILD_DIR := $(BUILD_DIR)/lang/lua
LUA_APP_SRCS := \
	$(USERLAND_DIR)/lua/lua_app_main.c \
	$(USERLAND_DIR)/lua/lua_app_entry.c \
	$(USERLAND_DIR)/modules/console.c \
	$(USERLAND_DIR)/modules/fs.c \
	$(USERLAND_DIR)/modules/utils.c \
	$(USERLAND_DIR)/modules/syscalls.c \
	$(USERLAND_DIR)/lua/lua_main.c \
	$(USERLAND_DIR)/lua/lua_repl.c \
	$(USERLAND_DIR)/lua/lua_runner.c \
	$(USERLAND_DIR)/lua/lua_runtime.c \
	$(USERLAND_DIR)/lua/lua_bindings_console.c \
	$(USERLAND_DIR)/lua/lua_bindings_sys.c \
	$(USERLAND_DIR)/lua/lua_port.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/lapi.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/lauxlib.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/lbaselib.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/lcode.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/lctype.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/ldebug.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/ldump.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/ldo.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/lfunc.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/lgc.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/llex.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/lmem.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/lobject.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/lopcodes.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/lparser.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/lstate.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/lstring.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/ltable.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/ltm.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/lundump.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/lvm.c \
	$(USERLAND_DIR)/lua/vendor/lua-5.4.6/src/lzio.c
LUA_APP_OBJS := $(patsubst %.c,$(LUA_APP_BUILD_DIR)/%.o,$(LUA_APP_SRCS))
LUA_APP_ELF := $(BUILD_DIR)/lang/lua.elf
LUA_APP_BIN := $(BUILD_DIR)/lang/lua.app

DESKTOP_RUNTIME_BASE_SRCS := \
	$(USERLAND_DIR)/modules/shell.c \
	$(USERLAND_DIR)/modules/busybox.c \
	$(USERLAND_DIR)/modules/console.c \
	$(USERLAND_DIR)/modules/fs.c \
	$(USERLAND_DIR)/modules/bmp.c \
	$(USERLAND_DIR)/modules/image.c \
	$(USERLAND_DIR)/modules/icon_theme.c \
	$(USERLAND_DIR)/modules/lang_loader.c \
	$(USERLAND_DIR)/modules/utils.c \
	$(USERLAND_DIR)/modules/syscalls.c \
	$(USERLAND_DIR)/modules/ui.c \
	$(USERLAND_DIR)/modules/dirty_rects.c \
	$(USERLAND_DIR)/modules/ui_clip.c \
	$(USERLAND_DIR)/modules/ui_cursor.c \
	$(USERLAND_DIR)/modules/microui.c \
	$(USERLAND_DIR)/modules/mu_renderer.c \
	$(USERLAND_DIR)/applications/desktop_math_compat.c \
	$(USERLAND_DIR)/applications/desktop.c \
	$(USERLAND_DIR)/applications/terminal.c \
	$(USERLAND_DIR)/applications/clock.c \
	$(USERLAND_DIR)/applications/filemanager.c \
	$(USERLAND_DIR)/applications/editor.c \
	$(USERLAND_DIR)/applications/taskmgr.c \
	$(USERLAND_DIR)/applications/calculator.c \
	$(USERLAND_DIR)/applications/imageviewer.c \
	$(USERLAND_DIR)/applications/audioplayer.c \
	$(USERLAND_DIR)/applications/sketchpad.c \
	$(USERLAND_DIR)/applications/games/snake.c \
	$(USERLAND_DIR)/applications/games/tetris.c \
	$(USERLAND_DIR)/applications/games/game_2048.c \
	$(USERLAND_DIR)/applications/games/minesweeper.c \
	$(USERLAND_DIR)/applications/games/pacman.c \
	$(USERLAND_DIR)/applications/games/space_invaders.c \
	$(USERLAND_DIR)/applications/games/pong.c \
	$(USERLAND_DIR)/applications/games/donkey_kong.c \
	$(USERLAND_DIR)/applications/games/brick_race.c \
	$(USERLAND_DIR)/applications/games/flap_birb.c \
	$(USERLAND_DIR)/applications/games/doom.c \
	$(USERLAND_DIR)/applications/games/craft/craft_app.c \
	$(USERLAND_DIR)/applications/games/craft/craft_gl_compat.c \
	$(USERLAND_DIR)/applications/games/craft/craft_glfw_compat.c \
	$(USERLAND_DIR)/applications/games/craft/craft_curl_compat.c \
	$(USERLAND_DIR)/applications/games/craft/craft_thread_compat.c \
	$(USERLAND_DIR)/applications/games/craft/craft_auth_compat.c \
	$(USERLAND_DIR)/applications/games/craft/craft_client_compat.c \
	$(USERLAND_DIR)/applications/games/craft/craft_db_compat.c \
	$(USERLAND_DIR)/applications/games/craft/upstream/deps/lodepng/lodepng.c \
	$(USERLAND_DIR)/applications/games/craft/world.c \
	$(USERLAND_DIR)/applications/games/craft/noise.c \
	$(USERLAND_DIR)/applications/games/doom_port/doom_port_main.c \
	$(USERLAND_DIR)/applications/games/doom_port/doom_libc_shim.c \
	$(USERLAND_DIR)/applications/games/doom_port/i_system_vibe.c \
	$(USERLAND_DIR)/applications/games/doom_port/i_video_vibe.c \
	$(USERLAND_DIR)/applications/games/doom_port/i_sound_vibe.c \
	$(USERLAND_DIR)/applications/games/doom_port/i_net_vibe.c
DESKTOP_RUNTIME_EXTRA_SRCS :=
ifeq ($(CRAFT_UPSTREAM_EXPERIMENTAL),1)
DESKTOP_RUNTIME_EXTRA_SRCS += \
	$(USERLAND_DIR)/applications/games/craft/craft_math_compat.c \
	$(USERLAND_DIR)/applications/games/craft/craft_util_compat.c \
	$(USERLAND_DIR)/applications/games/craft/craft_upstream_map.c \
	$(USERLAND_DIR)/applications/games/craft/craft_upstream_matrix.c \
	$(USERLAND_DIR)/applications/games/craft/craft_upstream_ring.c \
	$(USERLAND_DIR)/applications/games/craft/craft_upstream_sign.c \
	$(USERLAND_DIR)/applications/games/craft/craft_upstream_item.c \
	$(USERLAND_DIR)/applications/games/craft/craft_upstream_cube.c \
	$(USERLAND_DIR)/applications/games/craft/craft_upstream_runner.c
endif
DESKTOP_RUNTIME_SRCS = $(DESKTOP_RUNTIME_BASE_SRCS) $(DESKTOP_RUNTIME_EXTRA_SRCS)
DESKTOP_RUNTIME_OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(DESKTOP_RUNTIME_SRCS)) \
	$(DOOM_CORE_OBJS)
DESKTOP_APP_MAIN_OBJ := $(BUILD_DIR)/lang/desktop_app_main.o
DESKTOP_APP_RUNTIME_OBJ := $(BUILD_DIR)/lang/desktop_app_runtime.o
DESKTOP_LAUNCHER_APPS := \
	startx \
	edit \
	nano \
	terminal \
	clock \
	filemanager \
	editor \
	taskmgr \
	calculator \
	imageviewer \
	sketchpad \
	snake \
	tetris \
	2048 \
	minesweeper \
	pacman \
	space_invaders \
	pong \
	donkey_kong \
	brick_race \
	flap_birb \
	doom \
	craft \
	personalize

STARTX_APP_BIN := $(BUILD_DIR)/lang/startx.app
EDIT_APP_BIN := $(BUILD_DIR)/lang/edit.app
NANO_APP_BIN := $(BUILD_DIR)/lang/nano.app
TERMINAL_APP_BIN := $(BUILD_DIR)/lang/terminal.app
CLOCK_APP_BIN := $(BUILD_DIR)/lang/clock.app
FILEMANAGER_APP_BIN := $(BUILD_DIR)/lang/filemanager.app
EDITOR_APP_BIN := $(BUILD_DIR)/lang/editor.app
TASKMGR_APP_BIN := $(BUILD_DIR)/lang/taskmgr.app
CALCULATOR_APP_BIN := $(BUILD_DIR)/lang/calculator.app
IMAGEVIEWER_APP_BIN := $(BUILD_DIR)/lang/imageviewer.app
SKETCHPAD_APP_BIN := $(BUILD_DIR)/lang/sketchpad.app
SNAKE_APP_BIN := $(BUILD_DIR)/lang/snake.app
TETRIS_APP_BIN := $(BUILD_DIR)/lang/tetris.app
GAME2048_APP_BIN := $(BUILD_DIR)/lang/2048.app
MINESWEEPER_APP_BIN := $(BUILD_DIR)/lang/minesweeper.app
PACMAN_APP_BIN := $(BUILD_DIR)/lang/pacman.app
SPACE_INVADERS_APP_BIN := $(BUILD_DIR)/lang/space_invaders.app
PONG_APP_BIN := $(BUILD_DIR)/lang/pong.app
DONKEY_KONG_APP_BIN := $(BUILD_DIR)/lang/donkey_kong.app
BRICK_RACE_APP_BIN := $(BUILD_DIR)/lang/brick_race.app
FLAP_BIRB_APP_BIN := $(BUILD_DIR)/lang/flap_birb.app
DOOM_APP_BIN := $(BUILD_DIR)/lang/doom.app
CRAFT_APP_BIN := $(BUILD_DIR)/lang/craft.app
PERSONALIZE_APP_BIN := $(BUILD_DIR)/lang/personalize.app

DESKTOP_LAUNCHER_HEAP_DEFAULT := 8388608u
DESKTOP_LAUNCHER_HEAP_doom := 8388608u
DESKTOP_LAUNCHER_HEAP_craft := 8388608u

SECTORC_APP_BUILD_DIR := $(BUILD_DIR)/lang/sectorc
SECTORC_APP_SRCS := \
	$(USERLAND_DIR)/sectorc/sectorc_app_main.c \
	$(USERLAND_DIR)/modules/console.c \
	$(USERLAND_DIR)/modules/fs.c \
	$(USERLAND_DIR)/modules/utils.c \
	$(USERLAND_DIR)/modules/syscalls.c \
	$(USERLAND_DIR)/sectorc/sectorc_main.c \
	$(USERLAND_DIR)/sectorc/sectorc_driver.c \
	$(USERLAND_DIR)/sectorc/sectorc_port.c \
	$(USERLAND_DIR)/sectorc/sectorc_runtime.c \
	$(USERLAND_DIR)/sectorc/sectorc_exec.c
SECTORC_APP_OBJS := $(patsubst %.c,$(SECTORC_APP_BUILD_DIR)/%.o,$(SECTORC_APP_SRCS)) \
	$(SECTORC_APP_BUILD_DIR)/app_entry.o \
	$(SECTORC_APP_BUILD_DIR)/app_runtime.o
SECTORC_APP_ELF := $(BUILD_DIR)/lang/sectorc.elf
SECTORC_APP_BIN := $(BUILD_DIR)/lang/sectorc.app

USERLAND_BOOT_APP_BUILD_DIR := $(BUILD_DIR)/lang/userland_app
USERLAND_BOOT_APP_SRCS := \
	$(USERLAND_DIR)/userland.c \
	$(USERLAND_DIR)/modules/shell.c \
	$(USERLAND_DIR)/modules/busybox.c \
	$(USERLAND_DIR)/modules/console.c \
	$(USERLAND_DIR)/modules/fs.c \
	$(USERLAND_DIR)/modules/lang_loader.c \
	$(USERLAND_DIR)/modules/utils.c \
	$(USERLAND_DIR)/modules/syscalls.c
USERLAND_BOOT_APP_OBJS := $(patsubst %.c,$(USERLAND_BOOT_APP_BUILD_DIR)/%.o,$(USERLAND_BOOT_APP_SRCS)) \
	$(USERLAND_BOOT_APP_BUILD_DIR)/app_entry.o \
	$(USERLAND_BOOT_APP_BUILD_DIR)/app_runtime.o
USERLAND_BOOT_APP_ELF := $(BUILD_DIR)/lang/userland.elf
USERLAND_BOOT_APP_BIN := $(BUILD_DIR)/lang/userland.app

ECHO_APP_BIN := $(BUILD_DIR)/ported/echo.app
CAT_APP_BIN := $(BUILD_DIR)/ported/cat.app
WC_APP_BIN := $(BUILD_DIR)/ported/wc.app
HEAD_APP_BIN := $(BUILD_DIR)/ported/head.app
TAIL_APP_BIN := $(BUILD_DIR)/ported/tail.app
GREP_APP_BIN := $(BUILD_DIR)/ported/grep.app
LOADKEYS_APP_BIN := $(BUILD_DIR)/ported/loadkeys.app
PWD_APP_BIN := $(BUILD_DIR)/ported/pwd.app
SLEEP_APP_BIN := $(BUILD_DIR)/ported/sleep.app
RMDIR_APP_BIN := $(BUILD_DIR)/ported/rmdir.app
MKDIR_APP_BIN := $(BUILD_DIR)/ported/mkdir.app
TRUE_APP_BIN := $(BUILD_DIR)/ported/true.app
FALSE_APP_BIN := $(BUILD_DIR)/ported/false.app
PRINTF_APP_BIN := $(BUILD_DIR)/ported/printf.app
SED_APP_BIN := $(BUILD_DIR)/ported/sed.app
UNAME_APP_BIN := $(BUILD_DIR)/ported/uname.app
SYNC_APP_BIN := $(BUILD_DIR)/ported/sync.app
TR_APP_BIN := $(BUILD_DIR)/ported/tr.app
IFCONFIG_APP_BIN := $(BUILD_DIR)/ported/ifconfig.app
ROUTE_APP_BIN := $(BUILD_DIR)/ported/route.app
NETSTAT_APP_BIN := $(BUILD_DIR)/ported/netstat.app
PING_APP_BIN := $(BUILD_DIR)/ported/ping.app
HOST_APP_BIN := $(BUILD_DIR)/ported/host.app
DIG_APP_BIN := $(BUILD_DIR)/ported/dig.app
FTP_APP_BIN := $(BUILD_DIR)/ported/ftp.app
CURL_APP_BIN := $(BUILD_DIR)/ported/curl.app
PORTED_APPS_STAMP := $(BUILD_DIR)/.ported_apps.stamp
ADVENTURE_APP_BIN := $(BUILD_DIR)/ported/adventure.app
ARITHMETIC_APP_BIN := $(BUILD_DIR)/ported/arithmetic.app
ATC_APP_BIN := $(BUILD_DIR)/ported/atc.app
BACKGAMMON_APP_BIN := $(BUILD_DIR)/ported/backgammon.app
BANNER_APP_BIN := $(BUILD_DIR)/ported/banner.app
BCD_APP_BIN := $(BUILD_DIR)/ported/bcd.app
BATTLESTAR_APP_BIN := $(BUILD_DIR)/ported/battlestar.app
BOGGLE_APP_BIN := $(BUILD_DIR)/ported/boggle.app
BS_APP_BIN := $(BUILD_DIR)/ported/bs.app
CAESAR_APP_BIN := $(BUILD_DIR)/ported/caesar.app
CANFIELD_APP_BIN := $(BUILD_DIR)/ported/canfield.app
CRIBBAGE_APP_BIN := $(BUILD_DIR)/ported/cribbage.app
FACTOR_APP_BIN := $(BUILD_DIR)/ported/factor.app
FISH_APP_BIN := $(BUILD_DIR)/ported/fish.app
FORTUNE_APP_BIN := $(BUILD_DIR)/ported/fortune.app
GOMOKU_APP_BIN := $(BUILD_DIR)/ported/gomoku.app
GRDC_APP_BIN := $(BUILD_DIR)/ported/grdc.app
HACK_APP_BIN := $(BUILD_DIR)/ported/hack.app
HANGMAN_APP_BIN := $(BUILD_DIR)/ported/hangman.app
MILLE_APP_BIN := $(BUILD_DIR)/ported/mille.app
MONOP_APP_BIN := $(BUILD_DIR)/ported/monop.app
MORSE_APP_BIN := $(BUILD_DIR)/ported/morse.app
NUMBER_APP_BIN := $(BUILD_DIR)/ported/number.app
PHANTASIA_APP_BIN := $(BUILD_DIR)/ported/phantasia.app
PIG_APP_BIN := $(BUILD_DIR)/ported/pig.app
POM_APP_BIN := $(BUILD_DIR)/ported/pom.app
PPT_APP_BIN := $(BUILD_DIR)/ported/ppt.app
PRIMES_APP_BIN := $(BUILD_DIR)/ported/primes.app
QUIZ_APP_BIN := $(BUILD_DIR)/ported/quiz.app
RAIN_APP_BIN := $(BUILD_DIR)/ported/rain.app
RANDOM_APP_BIN := $(BUILD_DIR)/ported/random.app
ROBOTS_APP_BIN := $(BUILD_DIR)/ported/robots.app
SAIL_APP_BIN := $(BUILD_DIR)/ported/sail.app
SNAKE_BSD_APP_BIN := $(BUILD_DIR)/ported/snake-bsd.app
TEACHGAMMON_APP_BIN := $(BUILD_DIR)/ported/teachgammon.app
TETRIS_BSD_APP_BIN := $(BUILD_DIR)/ported/tetris-bsd.app
TREK_APP_BIN := $(BUILD_DIR)/ported/trek.app
WARGAMES_APP_BIN := $(BUILD_DIR)/ported/wargames.app
WORM_APP_BIN := $(BUILD_DIR)/ported/worm.app
WORMS_APP_BIN := $(BUILD_DIR)/ported/worms.app
WUMP_APP_BIN := $(BUILD_DIR)/ported/wump.app
BSD_GAMES_APPS_STAMP := $(BUILD_DIR)/.bsd_games_apps.stamp

$(shell mkdir -p $(APP_CATALOG_GENERATED_DIR))
$(shell $(PYTHON) tools/generate_app_catalog.py --manifest $(APP_CATALOG_MANIFEST) --mk $(APP_CATALOG_GENERATED_MK) --header $(APP_CATALOG_GENERATED_H))
-include $(APP_CATALOG_GENERATED_MK)

LANG_APP_BINS := $(APP_CATALOG_APP_BINS)
IMAGE_APP_BINS ?= $(LANG_APP_BINS)
BOOT_SMOKE_APP_BINS := \
	$(USERLAND_BOOT_APP_BIN) \
	$(HELLO_APP_BIN) \
	$(LUA_APP_BIN) \
	$(SECTORC_APP_BIN) \
	$(SOUNDCTL_APP_BIN) \
	$(AUDIOSVC_APP_BIN) \
	$(NETMGRD_APP_BIN) \
	$(NETCTL_APP_BIN) \
	$(FOXXOD_APP_BIN)

# Include compatibility layer build rules
include Build.compat.mk

REQUIRED_BUILD_TOOLS := $(AS) $(CC) $(LD) $(NM) $(OBJCOPY) $(AR) $(RANLIB) $(PYTHON)
REQUIRED_IMAGE_TOOLS := $(MKFS_FAT_TOOL)

all: check-tools $(IMAGE)
# Optional legacy monolithic payload for experiments outside the default image.
all-monolith: check-tools $(IMAGE) $(USERLAND_MAIN_BIN)
userland-main: check-tools $(USERLAND_MAIN_BIN)
legacy-data-img: check-tools $(DATA_IMAGE)
boot-manifest: check-tools $(BOOT_VOLUME_MANIFEST)
# Note: glibc separate build available via: make -f Build.glibc.mk glibc-build
# To link glibc instead of stubs, modify KERNEL_ELF dependencies below

check-tools:
	@echo "Toolchain mode: $(TOOLCHAIN_MODE) ($(UNAME_S))"; \
	for tool in $(REQUIRED_BUILD_TOOLS); do \
		if ! command -v $$tool >/dev/null 2>&1; then \
			echo "Erro: '$$tool' nao encontrado no PATH."; \
			if [ "$(UNAME_S)" = "Darwin" ]; then \
				echo "macOS: use uma cross-toolchain i686-elf/x86_64-elf e instale nasm/qemu."; \
			else \
				echo "Linux: instale binutils/gcc 32-bit + nasm + qemu-system-x86"; \
				echo "Ou use toolchain cruzada i686-elf-*."; \
			fi; \
			exit 1; \
		fi; \
	done; \
	for tool in $(REQUIRED_IMAGE_TOOLS); do \
		if ! command -v $$tool >/dev/null 2>&1; then \
			echo "Erro: '$$tool' nao encontrado no PATH."; \
			if [ "$(UNAME_S)" = "Darwin" ]; then \
				echo "macOS: instale um formatador FAT32 compativel como mkfs.fat (dosfstools) ou use newfs_msdos."; \
			else \
				echo "Instale um formatador FAT32 compativel (ex.: dosfstools)."; \
			fi; \
			exit 1; \
		fi; \
	done

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(MBR_BIN): $(BOOT_DIR)/mbr.asm | $(BUILD_DIR)
	$(AS) -f bin $(BOOT_NASM_DEFINES) \
		-DIMAGE_TOTAL_SECTORS=$(IMAGE_TOTAL_SECTORS) \
		-DBOOT_PARTITION_START_LBA=$(BOOT_PARTITION_START_LBA) \
		-DBOOT_PARTITION_SECTORS=$(BOOT_PARTITION_SECTORS) \
		-DSOFTWARE_PARTITION_START_LBA=$(DATA_PARTITION_START_LBA) \
		-DSOFTWARE_PARTITION_SECTORS=$$(( $(IMAGE_TOTAL_SECTORS) - $(DATA_PARTITION_START_LBA) )) \
		$< -o $@
	@mbr_size=$$(wc -c < $@); \
	if [ "$$mbr_size" -ne 512 ]; then \
		echo "Erro: MBR precisa ter 512 bytes (atual: $$mbr_size)."; \
		exit 1; \
	fi

$(STAGE2_BIN): $(BOOT_DIR)/stage2.asm | $(BUILD_DIR)
	$(AS) -f bin $(BOOT_NASM_DEFINES) $< -o $@
	@stage2_size=$$(wc -c < $@); \
	stage2_sectors=$$(((stage2_size + 511) / 512)); \
	if [ "$$stage2_sectors" -ge "$(BOOT_KERNEL_START_SECTOR)" ]; then \
		echo "Erro: stage2 excede a janela reservada antes do kernel ($$stage2_sectors setores)."; \
		exit 1; \
	fi

ifneq ($(BOOTLOADER_BG_BIN),$(BOOTLOADER_BG_SMOKE_BIN))
$(BOOTLOADER_BG_BIN): $(BOOTLOADER_BG_SRC) tools/build_boot_palette_asset.py Makefile | $(BUILD_DIR)
	$(PYTHON) tools/build_boot_palette_asset.py \
		--input $(BOOTLOADER_BG_SRC) \
		--output $@ \
		--width $(BOOTLOADER_BG_WIDTH) \
		--height $(BOOTLOADER_BG_HEIGHT) \
		--resample $(BOOTLOADER_BG_RESAMPLE)
endif

$(BOOTLOADER_BG_SMOKE_BIN): | $(BUILD_DIR)
	$(PYTHON) -c 'from pathlib import Path; p = Path(r"$@"); p.parent.mkdir(parents=True, exist_ok=True); p.write_bytes(bytes($(BOOTLOADER_BG_WIDTH) * $(BOOTLOADER_BG_HEIGHT)))'

$(BOOT_BIN): $(BOOT_DIR)/stage1.asm $(STAGE2_BIN) | $(BUILD_DIR)
	@stage2_sectors=$$((($$(wc -c < $(STAGE2_BIN)) + 511) / 512)); \
	$(AS) -f bin -DSTAGE2_START_LBA=$(BOOT_STAGE2_START_SECTOR) -DSTAGE2_SECTORS=$$stage2_sectors $< -o $@
	@boot_size=$$(wc -c < $@); \
	if [ "$$boot_size" -ne 512 ]; then \
		echo "Erro: stage1 precisa ter 512 bytes (atual: $$boot_size)."; \
		exit 1; \
	fi

# Prevent GNU make from applying its built-in "link a single .o into an
# executable with the same basename" rule to build/userland/userland.
$(BUILD_DIR)/userland/userland:
	@true
$(BUILD_DIR)/userland/applications/games/doom:
	@true
$(BUILD_DIR)/userland/applications/games/craft:
	@true
$(BUILD_DIR)/userland/applications/games/DOOM:
	@true
# NOTE: video.o must stay at the i586 baseline. The only SSE2 code is
# kernel_video_copy_row_movntdq (per-function target attribute); a whole-TU
# -msse2 made GCC emit CMOVcc into boot-path functions and broke Pentium.

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(if $(filter kernel/%,$<),$(KERNEL_CFLAGS),$(CFLAGS)) -c $< -o $@

$(BUILD_DIR)/userland/applications/games/craft/upstream/deps/lodepng/lodepng.o: userland/applications/games/craft/upstream/deps/lodepng/lodepng.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DLODEPNG_NO_COMPILE_DISK -c $< -o $@

$(APP_CATALOG_GENERATED_MK) $(APP_CATALOG_GENERATED_H): $(APP_CATALOG_MANIFEST) tools/generate_app_catalog.py | $(BUILD_DIR)
	@mkdir -p $(APP_CATALOG_GENERATED_DIR)
	$(PYTHON) tools/generate_app_catalog.py --manifest $(APP_CATALOG_MANIFEST) --mk $(APP_CATALOG_GENERATED_MK) --header $(APP_CATALOG_GENERATED_H)

$(BUILD_DIR)/userland/modules/busybox.o: $(APP_CATALOG_GENERATED_H)
$(BUILD_DIR)/userland/modules/fs.o: $(APP_CATALOG_GENERATED_H)
$(USERLAND_BOOT_APP_BUILD_DIR)/userland/modules/busybox.o: $(APP_CATALOG_GENERATED_H)
$(USERLAND_BOOT_APP_BUILD_DIR)/userland/modules/fs.o: $(APP_CATALOG_GENERATED_H)


$(BUILD_DIR)/%.o: %.asm
	@mkdir -p $(dir $@)
	$(AS) -f elf32 $< -o $@

$(HELLO_APP_BUILD_DIR)/app_entry.o: lang/sdk/app_entry.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DVIBE_APP_BUILD_NAME=\"hello\" -DVIBE_APP_BUILD_HEAP_SIZE=32768u -c $< -o $@

$(HELLO_APP_BUILD_DIR)/app_runtime.o: lang/sdk/app_runtime.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(HELLO_APP_BUILD_DIR)/hello_main.o: lang/apps/hello/hello_main.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(SOUNDCTL_APP_BUILD_DIR)/app_entry.o: lang/sdk/app_entry.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DVIBE_APP_BUILD_NAME=\"soundctl\" -DVIBE_APP_BUILD_HEAP_SIZE=32768u -c $< -o $@

$(SOUNDCTL_APP_BUILD_DIR)/app_runtime.o: lang/sdk/app_runtime.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(SOUNDCTL_APP_BUILD_DIR)/soundctl_main.o: lang/apps/soundctl/soundctl_main.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(AUDIOSVC_APP_BUILD_DIR)/app_entry.o: lang/sdk/app_entry.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DVIBE_APP_BUILD_NAME=\"audiosvc\" -DVIBE_APP_BUILD_HEAP_SIZE=32768u -c $< -o $@

$(AUDIOSVC_APP_BUILD_DIR)/app_runtime.o: lang/sdk/app_runtime.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(AUDIOSVC_APP_BUILD_DIR)/audiosvc_main.o: lang/apps/audiosvc/audiosvc_main.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(NETMGRD_APP_BUILD_DIR)/app_entry.o: lang/sdk/app_entry.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DVIBE_APP_BUILD_NAME=\"netmgrd\" -DVIBE_APP_BUILD_HEAP_SIZE=32768u -c $< -o $@

$(NETMGRD_APP_BUILD_DIR)/app_runtime.o: lang/sdk/app_runtime.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(NETMGRD_APP_BUILD_DIR)/netmgrd_main.o: lang/apps/netmgrd/netmgrd_main.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(NETCTL_APP_BUILD_DIR)/app_entry.o: lang/sdk/app_entry.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DVIBE_APP_BUILD_NAME=\"netctl\" -DVIBE_APP_BUILD_HEAP_SIZE=32768u -c $< -o $@

$(NETCTL_APP_BUILD_DIR)/app_runtime.o: lang/sdk/app_runtime.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(NETCTL_APP_BUILD_DIR)/netctl_main.o: lang/apps/netctl/netctl_main.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(FOXXOD_APP_BUILD_DIR)/app_entry.o: lang/sdk/app_entry.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DVIBE_APP_BUILD_NAME=\"foxxod\" -DVIBE_APP_BUILD_HEAP_SIZE=65536u -c $< -o $@

$(FOXXOD_APP_BUILD_DIR)/app_runtime.o: lang/sdk/app_runtime.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(FOXXOD_APP_BUILD_DIR)/foxxod_main.o: lang/apps/foxxod/foxxod_main.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(JS_APP_BUILD_DIR)/app_entry.o: lang/sdk/app_entry.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DVIBE_APP_BUILD_NAME=\"js\" -DVIBE_APP_BUILD_HEAP_SIZE=65536u -c $< -o $@

$(JS_APP_BUILD_DIR)/app_runtime.o: lang/sdk/app_runtime.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(JS_APP_BUILD_DIR)/js_main.o: lang/apps/js/js_main.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(RUBY_APP_BUILD_DIR)/app_entry.o: lang/sdk/app_entry.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DVIBE_APP_BUILD_NAME=\"ruby\" -DVIBE_APP_BUILD_HEAP_SIZE=65536u -c $< -o $@

$(RUBY_APP_BUILD_DIR)/app_runtime.o: lang/sdk/app_runtime.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(RUBY_APP_BUILD_DIR)/ruby_main.o: lang/apps/ruby/ruby_main.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(PYTHON_APP_BUILD_DIR)/app_entry.o: lang/sdk/app_entry.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DVIBE_APP_BUILD_NAME=\"python\" -DVIBE_APP_BUILD_HEAP_SIZE=65536u -c $< -o $@

$(PYTHON_APP_BUILD_DIR)/app_runtime.o: lang/sdk/app_runtime.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(PYTHON_APP_BUILD_DIR)/python_main.o: lang/apps/python/python_main.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(JAVA_APP_BUILD_DIR)/app_entry.o: lang/sdk/app_entry.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DVIBE_APP_BUILD_NAME=\"java\" -DVIBE_APP_BUILD_HEAP_SIZE=262144u -c $< -o $@

$(JAVA_APP_BUILD_DIR)/app_runtime.o: lang/sdk/app_runtime.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(JAVA_APP_BUILD_DIR)/java_main.o: lang/apps/java/java_main.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(JAVAC_APP_BUILD_DIR)/app_entry.o: lang/sdk/app_entry.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DVIBE_APP_BUILD_NAME=\"javac\" -DVIBE_APP_BUILD_HEAP_SIZE=262144u -c $< -o $@

$(JAVAC_APP_BUILD_DIR)/app_runtime.o: lang/sdk/app_runtime.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(JAVAC_APP_BUILD_DIR)/javac_main.o: lang/apps/javac/javac_main.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(LUA_APP_BUILD_DIR):
	@mkdir -p $(LUA_APP_BUILD_DIR)/userland/lua
	@mkdir -p $(LUA_APP_BUILD_DIR)/userland/modules
	@mkdir -p $(LUA_APP_BUILD_DIR)/userland/lua/vendor/lua-5.4.6/src

$(LUA_APP_BUILD_DIR)/%.o: %.c | $(BUILD_DIR) $(LUA_APP_BUILD_DIR)
	@mkdir -p $(dir $@)
	# Vendored Lua VM + its shim link WITHOUT lang/sdk/app_runtime.c, so the
	# shared-CFLAGS stack protector would reference symbols this binary has
	# no provider for (same reasoning as DOOM_CFLAGS keeping
	# -fno-stack-protector): the SSP probe targets our kernel and SDK, not
	# the vendored interpreter.
	$(CC) $(CFLAGS) -fno-stack-protector $(LUA_CFLAGS) -DVIBE_USERLAND_APP -c $< -o $@

$(SECTORC_APP_BUILD_DIR)/app_entry.o: lang/sdk/app_entry.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DVIBE_APP_BUILD_NAME=\"sectorc\" -DVIBE_APP_BUILD_HEAP_SIZE=131072u -c $< -o $@

$(SECTORC_APP_BUILD_DIR)/app_runtime.o: lang/sdk/app_runtime.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(SECTORC_APP_BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DVIBE_USERLAND_APP -c $< -o $@

$(AP_TRAMPOLINE_BIN): $(BOOT_DIR)/ap_trampoline.asm | $(BUILD_DIR)
	$(AS) -f bin $< -o $@

$(AP_TRAMPOLINE_OBJ): $(AP_TRAMPOLINE_BIN) | $(BUILD_DIR)
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 $< $@

$(KERNEL_ELF): $(KERNEL_OBJS) $(KERNEL_ASM_OBJS) $(AP_TRAMPOLINE_OBJ) $(KERNEL_USERLAND_OBJS) $(LINKER_DIR)/kernel.ld $(COMPAT_LIB)
	$(LD) $(LDFLAGS_KERNEL) $(KERNEL_OBJS) $(KERNEL_ASM_OBJS) $(AP_TRAMPOLINE_OBJ) $(KERNEL_USERLAND_OBJS)  $(COMPAT_LIB) -o $@

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@
	@kernel_sectors=$$((($$(wc -c < $@) + 511) / 512)); \
	if [ "$$kernel_sectors" -gt "$$(( $(BOOT_PARTITION_RESERVED_SECTORS) - 1 ))" ]; then \
		echo "Erro: kernel.bin excede a area reservada do boot FAT32 ($$kernel_sectors > $$(( $(BOOT_PARTITION_RESERVED_SECTORS) - 1 )) setores)."; \
		exit 1; \
	fi

$(USERLAND_MAIN_ELF): $(USERLAND_OBJS) $(LINKER_DIR)/userland.ld $(COMPAT_LIB)
	$(LD) $(LDFLAGS_USERLAND) $(USERLAND_OBJS)  $(COMPAT_LIB) -o $@

$(USERLAND_MAIN_BIN): $(USERLAND_MAIN_ELF)
	$(OBJCOPY) -O binary $< $@

$(HELLO_APP_ELF): $(HELLO_APP_OBJS) $(LINKER_DIR)/app.ld
	$(LD) $(LDFLAGS_APP) $(HELLO_APP_OBJS) -o $@ $(LIBGCC_A)

$(HELLO_APP_BIN): $(HELLO_APP_ELF)
	cp $< $<.keep
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $<.keep --bin $@
	rm -f $<.keep

$(SOUNDCTL_APP_ELF): $(SOUNDCTL_APP_OBJS) $(LINKER_DIR)/app.ld
	$(LD) $(LDFLAGS_APP) $(SOUNDCTL_APP_OBJS) -o $@ $(LIBGCC_A)

$(SOUNDCTL_APP_BIN): $(SOUNDCTL_APP_ELF)
	cp $< $<.keep
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $<.keep --bin $@
	rm -f $<.keep

$(AUDIOSVC_APP_ELF): $(AUDIOSVC_APP_OBJS) $(LINKER_DIR)/app.ld
	$(LD) $(LDFLAGS_APP) $(AUDIOSVC_APP_OBJS) -o $@ $(LIBGCC_A)

$(AUDIOSVC_APP_BIN): $(AUDIOSVC_APP_ELF)
	cp $< $<.keep
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $<.keep --bin $@
	rm -f $<.keep

$(NETMGRD_APP_ELF): $(NETMGRD_APP_OBJS) $(LINKER_DIR)/app.ld
	$(LD) $(LDFLAGS_APP) $(NETMGRD_APP_OBJS) -o $@ $(LIBGCC_A)

$(NETMGRD_APP_BIN): $(NETMGRD_APP_ELF)
	cp $< $<.keep
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $<.keep --bin $@
	rm -f $<.keep

$(NETCTL_APP_ELF): $(NETCTL_APP_OBJS) $(LINKER_DIR)/app.ld
	$(LD) $(LDFLAGS_APP) $(NETCTL_APP_OBJS) -o $@ $(LIBGCC_A)

$(NETCTL_APP_BIN): $(NETCTL_APP_ELF)
	cp $< $<.keep
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $<.keep --bin $@
	rm -f $<.keep

$(FOXXOD_APP_ELF): $(FOXXOD_APP_OBJS) $(LINKER_DIR)/app.ld
	$(LD) $(LDFLAGS_APP) $(FOXXOD_APP_OBJS) -o $@ $(LIBGCC_A)

$(FOXXOD_APP_BIN): $(FOXXOD_APP_ELF)
	cp $< $<.keep
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $<.keep --bin $@
	rm -f $<.keep

$(JS_APP_ELF): $(JS_APP_OBJS) $(LINKER_DIR)/app.ld
	$(LD) $(LDFLAGS_APP) $(JS_APP_OBJS) -o $@ $(LIBGCC_A)

$(JS_APP_BIN): $(JS_APP_ELF)
	cp $< $<.keep
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $<.keep --bin $@
	rm -f $<.keep

$(RUBY_APP_ELF): $(RUBY_APP_OBJS) $(LINKER_DIR)/app.ld
	$(LD) $(LDFLAGS_APP) $(RUBY_APP_OBJS) -o $@ $(LIBGCC_A)

$(RUBY_APP_BIN): $(RUBY_APP_ELF)
	cp $< $<.keep
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $<.keep --bin $@
	rm -f $<.keep

$(PYTHON_APP_ELF): $(PYTHON_APP_OBJS) $(LINKER_DIR)/app.ld
	$(LD) $(LDFLAGS_APP) $(PYTHON_APP_OBJS) -o $@ $(LIBGCC_A)

$(PYTHON_APP_BIN): $(PYTHON_APP_ELF)
	cp $< $<.keep
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $<.keep --bin $@
	rm -f $<.keep

$(JAVA_APP_ELF): $(JAVA_APP_OBJS) $(LINKER_DIR)/app.ld
	$(LD) $(LDFLAGS_APP) $(JAVA_APP_OBJS) -o $@ $(LIBGCC_A)

$(JAVA_APP_BIN): $(JAVA_APP_ELF)
	cp $< $<.keep
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $<.keep --bin $@
	rm -f $<.keep

$(JAVAC_APP_ELF): $(JAVAC_APP_OBJS) $(LINKER_DIR)/app.ld
	$(LD) $(LDFLAGS_APP) $(JAVAC_APP_OBJS) -o $@ $(LIBGCC_A)

$(JAVAC_APP_BIN): $(JAVAC_APP_ELF)
	cp $< $<.keep
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $<.keep --bin $@
	rm -f $<.keep

$(LUA_APP_ELF): $(LUA_APP_OBJS) $(LINKER_DIR)/app.ld $(COMPAT_LIB)
	$(LD) $(LDFLAGS_APP) $(LUA_APP_OBJS)  $(COMPAT_LIB) -o $@ $(LIBGCC_A)

$(LUA_APP_BIN): $(LUA_APP_ELF)
	cp $< $<.keep
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $<.keep --bin $@
	rm -f $<.keep

$(DESKTOP_APP_MAIN_OBJ): $(USERLAND_DIR)/applications/desktop_app_main.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(DESKTOP_APP_RUNTIME_OBJ): $(USERLAND_DIR)/applications/desktop_app_runtime.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

define DESKTOP_LAUNCHER_RULES
$(BUILD_DIR)/lang/$(1)_app_entry.o: lang/sdk/app_entry.c | $(BUILD_DIR)
	@mkdir -p $$(dir $$@)
	$$(CC) $$(CFLAGS) -DVIBE_APP_BUILD_NAME=\"$(1)\" -DVIBE_APP_BUILD_HEAP_SIZE=$$(or $$(DESKTOP_LAUNCHER_HEAP_$(1)),$$(DESKTOP_LAUNCHER_HEAP_DEFAULT)) -c $$< -o $$@

$(BUILD_DIR)/lang/$(1).elf: $$(DESKTOP_APP_MAIN_OBJ) $$(DESKTOP_APP_RUNTIME_OBJ) $$(DESKTOP_RUNTIME_OBJS) $(BUILD_DIR)/lang/$(1)_app_entry.o $$(LINKER_DIR)/app_desktop.ld $ $(COMPAT_LIB)
	$$(LD) -m elf_i386 -T $$(LINKER_DIR)/app_desktop.ld -nostdlib -N --allow-multiple-definition $$(DESKTOP_APP_MAIN_OBJ) $$(DESKTOP_APP_RUNTIME_OBJ) $$(DESKTOP_RUNTIME_OBJS) $(BUILD_DIR)/lang/$(1)_app_entry.o $ $(COMPAT_LIB) -o $$@ $$(LIBGCC_A)

$(BUILD_DIR)/lang/$(1).app: $(BUILD_DIR)/lang/$(1).elf
	cp $$< $$<.keep
	$$(OBJCOPY) -O binary $$< $$@
	$$(PYTHON) tools/patch_app_header.py --nm $$(NM) --elf $$<.keep --bin $$@
	rm -f $$<.keep
endef

$(foreach app,$(DESKTOP_LAUNCHER_APPS),$(eval $(call DESKTOP_LAUNCHER_RULES,$(app))))

$(SECTORC_APP_ELF): $(SECTORC_APP_OBJS) $(LINKER_DIR)/app.ld $(COMPAT_LIB)
	$(LD) $(LDFLAGS_APP) $(SECTORC_APP_OBJS)  $(COMPAT_LIB) -o $@ $(LIBGCC_A)

$(SECTORC_APP_BIN): $(SECTORC_APP_ELF)
	cp $< $<.keep
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $<.keep --bin $@
	rm -f $<.keep

$(USERLAND_BOOT_APP_BUILD_DIR)/app_entry.o: lang/sdk/app_entry.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DVIBE_APP_BUILD_NAME=\"userland\" -DVIBE_APP_BUILD_HEAP_SIZE=131072u -c $< -o $@

$(USERLAND_BOOT_APP_BUILD_DIR)/app_runtime.o: lang/sdk/app_runtime.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(USERLAND_BOOT_APP_BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DVIBE_USERLAND_APP -c $< -o $@

$(USERLAND_BOOT_APP_ELF): $(USERLAND_BOOT_APP_OBJS) $(LINKER_DIR)/app_boot.ld $(COMPAT_LIB)
	$(LD) -m elf_i386 -T $(LINKER_DIR)/app_boot.ld -nostdlib -N $(USERLAND_BOOT_APP_OBJS)  $(COMPAT_LIB) -o $@ $(LIBGCC_A)

$(USERLAND_BOOT_APP_BIN): $(USERLAND_BOOT_APP_ELF)
	cp $< $<.keep
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $<.keep --bin $@
	rm -f $<.keep

# Ported GNU apps (echo, cat, wc, head, tail, grep, etc)
# Build once via stamp to avoid parallel duplicate sub-make executions.
# The stamp must depend on the app sources AND on the shared ported headers:
# without them, editing applications/ported/<tool>/*.c left a stale .app in
# the image (found when dig/host gained a real DNS client and the artifact was
# not rebuilt). Wildcards keep the list correct as ports are added.
PORTED_APP_SRCS := $(wildcard applications/ported/*/*.c) $(wildcard applications/ported/include/*.h)

$(PORTED_APPS_STAMP): $(PORTED_APP_SRCS) $(COMPAT_LIB) Build.ported.mk lang/sdk/app_entry.c lang/sdk/app_runtime.c lang/include/vibe_app.h tools/patch_app_header.py
	@mkdir -p $(dir $@)
	$(MAKE) -j1 -f Build.ported.mk \
		CC="$(CC)" LD="$(LD)" OBJCOPY="$(OBJCOPY)" NM="$(NM)" AR="$(AR)" RANLIB="$(RANLIB)" \
		ported-all
	@touch $@

$(ECHO_APP_BIN) $(CAT_APP_BIN) $(WC_APP_BIN) $(PWD_APP_BIN) $(HEAD_APP_BIN) $(SLEEP_APP_BIN) $(RMDIR_APP_BIN) $(MKDIR_APP_BIN) $(TAIL_APP_BIN) $(GREP_APP_BIN) $(SED_APP_BIN) $(LOADKEYS_APP_BIN) $(TRUE_APP_BIN) $(FALSE_APP_BIN) $(PRINTF_APP_BIN) $(UNAME_APP_BIN) $(SYNC_APP_BIN) $(TR_APP_BIN) $(IFCONFIG_APP_BIN) $(ROUTE_APP_BIN) $(NETSTAT_APP_BIN) $(PING_APP_BIN) $(HOST_APP_BIN) $(DIG_APP_BIN) $(FTP_APP_BIN) $(CURL_APP_BIN): $(PORTED_APPS_STAMP)

$(BSD_GAMES_APPS_STAMP): $(COMPAT_LIB) Build.bsdgames.mk lang/sdk/app_entry.c lang/sdk/app_runtime.c lang/include/vibe_app.h tools/patch_app_header.py \
	applications/ported/bsdgames/vibe_bsdgame_main.c applications/ported/bsdgames/vibe_bsdgame_compat.c applications/ported/bsdgames/vibe_bsdgame_shim.h
	@mkdir -p $(dir $@)
	$(MAKE) -j1 -f Build.bsdgames.mk \
		CC="$(CC)" LD="$(LD)" OBJCOPY="$(OBJCOPY)" NM="$(NM)" AR="$(AR)" RANLIB="$(RANLIB)" \
		bsdgames-all
	@touch $@

$(ADVENTURE_APP_BIN) $(ARITHMETIC_APP_BIN) $(ATC_APP_BIN) $(BACKGAMMON_APP_BIN) $(BANNER_APP_BIN) $(BCD_APP_BIN) \
$(BATTLESTAR_APP_BIN) $(BOGGLE_APP_BIN) $(BS_APP_BIN) $(CAESAR_APP_BIN) $(CANFIELD_APP_BIN) \
$(CRIBBAGE_APP_BIN) $(FACTOR_APP_BIN) $(FISH_APP_BIN) $(FORTUNE_APP_BIN) $(GOMOKU_APP_BIN) \
$(GRDC_APP_BIN) $(HACK_APP_BIN) $(HANGMAN_APP_BIN) $(MILLE_APP_BIN) $(MONOP_APP_BIN) $(MORSE_APP_BIN) \
$(NUMBER_APP_BIN) $(PHANTASIA_APP_BIN) $(PIG_APP_BIN) $(POM_APP_BIN) $(PPT_APP_BIN) \
$(PRIMES_APP_BIN) $(QUIZ_APP_BIN) $(RAIN_APP_BIN) $(RANDOM_APP_BIN) $(ROBOTS_APP_BIN) $(SAIL_APP_BIN) \
$(SNAKE_BSD_APP_BIN) $(TEACHGAMMON_APP_BIN) $(TETRIS_BSD_APP_BIN) $(TREK_APP_BIN) \
$(WARGAMES_APP_BIN) $(WORM_APP_BIN) $(WORMS_APP_BIN) $(WUMP_APP_BIN): $(BSD_GAMES_APPS_STAMP)

ifneq ($(WALLPAPER_RUNTIME_PNG),$(WALLPAPER_RUNTIME_SMOKE_PNG))
$(WALLPAPER_RUNTIME_PNG): $(WALLPAPER_SRC) tools/build_runtime_png_asset.py Makefile | $(BUILD_DIR)
	$(PYTHON) tools/build_runtime_png_asset.py \
		--input $(WALLPAPER_SRC) \
		--output $@ \
		--width $(WALLPAPER_RUNTIME_W) \
		--height $(WALLPAPER_RUNTIME_H)
endif

$(WALLPAPER_RUNTIME_SMOKE_PNG): $(WALLPAPER_SRC) | $(BUILD_DIR)
	cp $(WALLPAPER_SRC) $@

$(VIBE_BOOTLOADER_AUDIO_RAW): $(VIBE_BOOT_WAV_SRC) | $(BUILD_DIR)
	$(PYTHON) tools/build_boot_audio_asset.py \
		--input $< \
		--output $@ \
		--sample-rate $(VIBE_BOOTLOADER_AUDIO_RATE)

$(DATA_IMAGE): $(IMAGE_APP_BINS) $(DOOM_WAD_SRC) $(CRAFT_TEXTURE_SRC) $(CRAFT_FONT_SRC) $(CRAFT_SKY_SRC) $(CRAFT_SIGN_SRC) $(WALLPAPER_RUNTIME_PNG) $(VIBE_BOOT_WAV_SRC) $(VIBE_DESKTOP_WAV_SRC) $(BOOTLOADER_BG_SRC) $(ICON_THEME_INDEX_SRC) $(ICON_APP_DEFAULT_48_SRC) $(ICON_APP_DEFAULT_16_SRC) $(ICON_TERMINAL_16_SRC) $(ICON_CLOCK_16_SRC) $(ICON_CALCULATOR_16_SRC) $(ICON_EDITOR_24_SRC) $(ICON_TASKS_16_SRC) $(ICON_IMAGEVIEWER_16_SRC) $(ICON_WALLPAPER_16_SRC) $(ICON_PREFERENCES_16_SRC) $(ICON_APP_MENU_16_SRC) $(ICON_NETWORK_16_SRC) $(ICON_TEXT_16_SRC) $(ICON_AUDIO_PLAYER_16_SRC) $(ICON_AUDIO_PLAYER_24_SRC) $(ICON_SKETCHPAD_22_SRC) $(ICON_GAME_SNAKE_16_SRC) $(ICON_GAME_BOARD_16_SRC) $(ICON_GAME_MINES_16_SRC) $(ICON_GAME_ARCADE_16_SRC) $(ICON_GAME_INVADERS_16_SRC) $(ICON_CRAFT_24_SRC) $(ICON_CRAFT_48_SRC) $(ICON_FOLDER_16_SRC) $(ICON_TRASH_16_SRC) $(ICON_FOLDER_OPEN_32_SRC) $(ICON_FOLDER_DOCS_32_SRC) $(ICON_FOLDER_MUSIC_32_SRC) $(ICON_FOLDER_PICTURES_32_SRC) $(ICON_FOLDER_VIDEO_32_SRC) $(ICON_ACTION_GO_UP_16_SRC) $(ICON_ACTION_OPEN_16_SRC) $(ICON_ACTION_NEW_16_SRC) $(ICON_ACTION_COPY_16_SRC) $(ICON_ACTION_PASTE_16_SRC) $(ICON_ACTION_RENAME_16_SRC) $(ICON_ACTION_SAVE_16_SRC) $(ICON_ACTION_SAVE_AS_16_SRC) $(ICON_ACTION_EXPORT_16_SRC) $(ICON_ACTION_CANCEL_16_SRC) $(ICON_ACTION_APPLY_16_SRC) $(ICON_ACTION_WINDOW_CLOSE_16_SRC) $(ICON_ACTION_WINDOW_NEW_16_SRC) $(ICON_ACTION_GO_DOWN_16_SRC) $(ICON_ACTION_TRASH_16_SRC) $(ICON_FOLDER_48_SRC) $(ICON_TRASH_48_SRC) $(ICON_AUDIO_ZERO_16_SRC) $(ICON_AUDIO_LOW_16_SRC) $(ICON_AUDIO_MEDIUM_16_SRC) $(ICON_AUDIO_HIGH_16_SRC) $(ICON_WIFI_NONE_16_SRC) $(ICON_WIFI_MEDIUM_16_SRC) $(ICON_WIFI_FULL_16_SRC) $(ICON_ETHERNET_CONNECTED_24_SRC) $(ICON_ETHERNET_DISCONNECTED_24_SRC)
	$(PYTHON) tools/build_data_partition.py \
		--image $@ \
		--image-total-sectors $(DATA_PARTITION_SECTORS) \
		--directory-lba $(APPFS_DIRECTORY_LBA) \
		--directory-sectors $(APPFS_DIRECTORY_SECTORS) \
		--app-area-sectors $(APPFS_APP_AREA_SECTORS) \
		--persist-sectors $(PERSIST_SECTOR_COUNT) \
		--manifest $(DATA_IMAGE_MANIFEST) \
		--asset "$(DOOM_WAD_SRC):$(DOOM_WAD_IMAGE_LBA):DOOM.WAD" \
		--asset "$(CRAFT_TEXTURE_SRC):$(CRAFT_TEXTURE_IMAGE_LBA):texture.png" \
		--asset "$(CRAFT_FONT_SRC):$(CRAFT_FONT_IMAGE_LBA):font.png" \
		--asset "$(CRAFT_SKY_SRC):$(CRAFT_SKY_IMAGE_LBA):sky.png" \
		--asset "$(CRAFT_SIGN_SRC):$(CRAFT_SIGN_IMAGE_LBA):sign.png" \
		--asset "$(WALLPAPER_RUNTIME_PNG):$(WALLPAPER_IMAGE_LBA):wallpaper.png" \
		--asset "$(VIBE_BOOT_WAV_SRC):$(VIBE_BOOT_WAV_IMAGE_LBA):vibe_os_boot.wav" \
		--asset "$(VIBE_DESKTOP_WAV_SRC):$(VIBE_DESKTOP_WAV_IMAGE_LBA):vibe_os_desktop.wav" \
		--asset "$(BOOTLOADER_BG_SRC):$(BOOTLOADER_BG_IMAGE_LBA):bootloader_background.png" \
		--asset "$(ICON_APP_DEFAULT_48_SRC):$(ICON_APP_DEFAULT_48_IMAGE_LBA):icons/apps/48/application-default-icon.png" \
		--asset "$(ICON_APP_DEFAULT_16_SRC):$(ICON_APP_DEFAULT_16_IMAGE_LBA):icons/apps/16/application-default-icon.png" \
		--asset "$(ICON_TERMINAL_16_SRC):$(ICON_TERMINAL_16_IMAGE_LBA):icons/apps/16/utilities-terminal.png" \
		--asset "$(ICON_CLOCK_16_SRC):$(ICON_CLOCK_16_IMAGE_LBA):icons/apps/16/clock.png" \
		--asset "$(ICON_CALCULATOR_16_SRC):$(ICON_CALCULATOR_16_IMAGE_LBA):icons/apps/16/accessories-calculator.png" \
		--asset "$(ICON_EDITOR_24_SRC):$(ICON_EDITOR_24_IMAGE_LBA):icons/apps/24/accessories-text-editor.png" \
		--asset "$(ICON_TASKS_16_SRC):$(ICON_TASKS_16_IMAGE_LBA):icons/apps/16/utilities-system-monitor.png" \
		--asset "$(ICON_IMAGEVIEWER_16_SRC):$(ICON_IMAGEVIEWER_16_IMAGE_LBA):icons/apps/16/camera-photo.png" \
		--asset "$(ICON_WALLPAPER_16_SRC):$(ICON_WALLPAPER_16_IMAGE_LBA):icons/apps/16/preferences-desktop-wallpaper.png" \
		--asset "$(ICON_PREFERENCES_16_SRC):$(ICON_PREFERENCES_16_IMAGE_LBA):icons/apps/16/preferences-desktop.png" \
		--asset "$(ICON_APP_MENU_16_SRC):$(ICON_APP_MENU_16_IMAGE_LBA):icons/apps/16/package_applications.png" \
		--asset "$(ICON_NETWORK_16_SRC):$(ICON_NETWORK_16_IMAGE_LBA):icons/apps/16/preferences-system-network.png" \
		--asset "$(ICON_TEXT_16_SRC):$(ICON_TEXT_16_IMAGE_LBA):icons/apps/16/text.png" \
		--asset "$(ICON_AUDIO_PLAYER_16_SRC):$(ICON_AUDIO_PLAYER_16_IMAGE_LBA):icons/apps/16/multimedia-audio-player.png" \
		--asset "$(ICON_AUDIO_PLAYER_24_SRC):$(ICON_AUDIO_PLAYER_24_IMAGE_LBA):icons/apps/24/audio-player.png" \
		--asset "$(ICON_SKETCHPAD_22_SRC):$(ICON_SKETCHPAD_22_IMAGE_LBA):icons/apps/22/preferences-desktop-theme.png" \
		--asset "$(ICON_GAME_SNAKE_16_SRC):$(ICON_GAME_SNAKE_16_IMAGE_LBA):icons/apps/16/ksnake.png" \
		--asset "$(ICON_GAME_BOARD_16_SRC):$(ICON_GAME_BOARD_16_IMAGE_LBA):icons/apps/16/package_games_board.png" \
		--asset "$(ICON_GAME_MINES_16_SRC):$(ICON_GAME_MINES_16_IMAGE_LBA):icons/apps/16/kmines.png" \
		--asset "$(ICON_GAME_ARCADE_16_SRC):$(ICON_GAME_ARCADE_16_IMAGE_LBA):icons/apps/16/package_games_arcade.png" \
		--asset "$(ICON_GAME_INVADERS_16_SRC):$(ICON_GAME_INVADERS_16_IMAGE_LBA):icons/apps/16/kspaceduel.png" \
		--asset "$(ICON_CRAFT_24_SRC):$(ICON_CRAFT_24_IMAGE_LBA):icons/apps/24/craft.png" \
		--asset "$(ICON_CRAFT_48_SRC):$(ICON_CRAFT_48_IMAGE_LBA):icons/apps/48/craft.png" \
		--asset "$(ICON_FOLDER_16_SRC):$(ICON_FOLDER_16_IMAGE_LBA):icons/places/16/folder.png" \
		--asset "$(ICON_TRASH_16_SRC):$(ICON_TRASH_16_IMAGE_LBA):icons/places/16/user-trash.png" \
		--asset "$(ICON_FOLDER_OPEN_32_SRC):$(ICON_FOLDER_OPEN_32_IMAGE_LBA):icons/places/32/folder_open.png" \
		--asset "$(ICON_FOLDER_DOCS_32_SRC):$(ICON_FOLDER_DOCS_32_IMAGE_LBA):icons/places/32/folder-documents.png" \
		--asset "$(ICON_FOLDER_MUSIC_32_SRC):$(ICON_FOLDER_MUSIC_32_IMAGE_LBA):icons/places/32/folder-music.png" \
		--asset "$(ICON_FOLDER_PICTURES_32_SRC):$(ICON_FOLDER_PICTURES_32_IMAGE_LBA):icons/places/32/folder-pictures.png" \
		--asset "$(ICON_FOLDER_VIDEO_32_SRC):$(ICON_FOLDER_VIDEO_32_IMAGE_LBA):icons/places/32/folder-video.png" \
		--asset "$(ICON_ACTION_GO_UP_16_SRC):$(ICON_ACTION_GO_UP_16_IMAGE_LBA):icons/actions/16/go-up.png" \
		--asset "$(ICON_ACTION_OPEN_16_SRC):$(ICON_ACTION_OPEN_16_IMAGE_LBA):icons/actions/16/document-open.png" \
		--asset "$(ICON_ACTION_NEW_16_SRC):$(ICON_ACTION_NEW_16_IMAGE_LBA):icons/actions/16/document-new.png" \
		--asset "$(ICON_ACTION_COPY_16_SRC):$(ICON_ACTION_COPY_16_IMAGE_LBA):icons/actions/16/edit-copy.png" \
		--asset "$(ICON_ACTION_PASTE_16_SRC):$(ICON_ACTION_PASTE_16_IMAGE_LBA):icons/actions/16/edit-paste.png" \
		--asset "$(ICON_ACTION_RENAME_16_SRC):$(ICON_ACTION_RENAME_16_IMAGE_LBA):icons/actions/16/item_rename.png" \
		--asset "$(ICON_ACTION_SAVE_16_SRC):$(ICON_ACTION_SAVE_16_IMAGE_LBA):icons/actions/16/filesave.png" \
		--asset "$(ICON_ACTION_SAVE_AS_16_SRC):$(ICON_ACTION_SAVE_AS_16_IMAGE_LBA):icons/actions/16/filesaveas.png" \
		--asset "$(ICON_ACTION_EXPORT_16_SRC):$(ICON_ACTION_EXPORT_16_IMAGE_LBA):icons/actions/16/document-export.png" \
		--asset "$(ICON_ACTION_CANCEL_16_SRC):$(ICON_ACTION_CANCEL_16_IMAGE_LBA):icons/actions/16/cancel.png" \
		--asset "$(ICON_ACTION_APPLY_16_SRC):$(ICON_ACTION_APPLY_16_IMAGE_LBA):icons/actions/16/gtk-apply.png" \
		--asset "$(ICON_ACTION_WINDOW_CLOSE_16_SRC):$(ICON_ACTION_WINDOW_CLOSE_16_IMAGE_LBA):icons/actions/16/window-close.png" \
		--asset "$(ICON_ACTION_WINDOW_NEW_16_SRC):$(ICON_ACTION_WINDOW_NEW_16_IMAGE_LBA):icons/actions/16/window-new.png" \
		--asset "$(ICON_ACTION_GO_DOWN_16_SRC):$(ICON_ACTION_GO_DOWN_16_IMAGE_LBA):icons/actions/16/go-down.png" \
		--asset "$(ICON_ACTION_TRASH_16_SRC):$(ICON_ACTION_TRASH_16_IMAGE_LBA):icons/actions/16/edittrash.png" \
		--asset "$(ICON_THEME_INDEX_SRC):$(ICON_THEME_INDEX_IMAGE_LBA):icons/index.theme" \
		--asset "$(ICON_FOLDER_48_SRC):$(ICON_FOLDER_48_IMAGE_LBA):icons/places/48/folder.png" \
		--asset "$(ICON_TRASH_48_SRC):$(ICON_TRASH_48_IMAGE_LBA):icons/places/48/user-trash.png" \
		--asset "$(ICON_AUDIO_ZERO_16_SRC):$(ICON_AUDIO_ZERO_16_IMAGE_LBA):icons/panel/16/audio-volume-low-zero.png" \
		--asset "$(ICON_AUDIO_LOW_16_SRC):$(ICON_AUDIO_LOW_16_IMAGE_LBA):icons/panel/16/audio-volume-low.png" \
		--asset "$(ICON_AUDIO_MEDIUM_16_SRC):$(ICON_AUDIO_MEDIUM_16_IMAGE_LBA):icons/panel/16/audio-volume-medium.png" \
		--asset "$(ICON_AUDIO_HIGH_16_SRC):$(ICON_AUDIO_HIGH_16_IMAGE_LBA):icons/panel/16/audio-volume-high.png" \
		--asset "$(ICON_WIFI_NONE_16_SRC):$(ICON_WIFI_NONE_16_IMAGE_LBA):icons/notifications/16/notification-network-wireless-none.png" \
		--asset "$(ICON_WIFI_MEDIUM_16_SRC):$(ICON_WIFI_MEDIUM_16_IMAGE_LBA):icons/notifications/16/notification-network-wireless-medium.png" \
		--asset "$(ICON_WIFI_FULL_16_SRC):$(ICON_WIFI_FULL_16_IMAGE_LBA):icons/notifications/16/notification-network-wireless-full.png" \
		--asset "$(ICON_ETHERNET_CONNECTED_24_SRC):$(ICON_ETHERNET_CONNECTED_24_IMAGE_LBA):icons/notifications/24/notification-network-ethernet-connected.png" \
		--asset "$(ICON_ETHERNET_DISCONNECTED_24_SRC):$(ICON_ETHERNET_DISCONNECTED_24_IMAGE_LBA):icons/notifications/24/notification-network-ethernet-disconnected.png" \
		$(IMAGE_APP_BINS)
	@cp $(DATA_IMAGE_MANIFEST) $(IMAGE_ASSET_MANIFEST)

$(BOOT_VOLUME_MANIFEST): $(KERNEL_BIN) $(STAGE2_BIN) $(DATA_IMAGE)
	@mkdir -p $(dir $@)
	@kernel_size=$$(wc -c < "$(KERNEL_BIN)" | tr -d '[:space:]'); \
	stage2_size=$$(wc -c < "$(STAGE2_BIN)" | tr -d '[:space:]'); \
	data_size=$$(wc -c < "$(DATA_IMAGE)" | tr -d '[:space:]'); \
	printf "# vibeOS FAT32 boot volume layout\nboot_partition_start_lba=%s\nboot_partition_sectors=%s\nboot_reserved_sectors=%s\nboot_stage2_sector=%s\nboot_kernel_sector=%s\ndata_partition_start_lba=%s\ndata_partition_sectors=%s\nkernel_bytes=%s\nstage2_bytes=%s\ndata_partition_bytes=%s\n" \
		"$(BOOT_PARTITION_START_LBA)" "$(BOOT_PARTITION_SECTORS)" "$(BOOT_PARTITION_RESERVED_SECTORS)" \
		"$(BOOT_STAGE2_START_SECTOR)" "$(BOOT_KERNEL_START_SECTOR)" \
		"$(DATA_PARTITION_START_LBA)" "$(DATA_PARTITION_SECTORS)" \
		"$$kernel_size" "$$stage2_size" "$$data_size" > $@

$(BOOT_POLICY_MANIFEST): $(KERNEL_BIN) $(STAGE2_BIN) $(DATA_IMAGE)
	@mkdir -p $(dir $@)
	@printf "# vibeOS USB boot/loading strategy\nbios_disk_transport=edd-int13\nboot_partition_fs=fat32\nstage2_path=/STAGE2.BIN\nkernel_path=/KERNEL.BIN\nruntime_storage_probe=ahci-then-ata\ndata_volume_resolution=mbr-data-partition-with-raw-layout-fallback\nusb_scope=phase4-relies-on-bios-mass-storage-before-native-usb-service\n" > $@

$(IMAGE): $(MBR_BIN) $(BOOT_BIN) $(STAGE2_BIN) $(KERNEL_BIN) $(DATA_IMAGE) $(BOOT_VOLUME_MANIFEST) $(BOOT_POLICY_MANIFEST) $(BOOTLOADER_BG_BIN) $(VIBE_BOOTLOADER_AUDIO_RAW)
	$(PYTHON) tools/build_partitioned_image.py \
		--image $@ \
		--mkfs-fat $(MKFS_FAT_TOOL) \
		--mcopy $(MCOPY_TOOL) \
		--mmd $(MMD_TOOL) \
		--mbr $(MBR_BIN) \
		--vbr $(BOOT_BIN) \
		--stage2 $(STAGE2_BIN) \
		--kernel $(KERNEL_BIN) \
		--image-total-sectors $(IMAGE_TOTAL_SECTORS) \
		--boot-partition-start-lba $(BOOT_PARTITION_START_LBA) \
		--boot-partition-sectors $(BOOT_PARTITION_SECTORS) \
		--boot-partition-reserved-sectors $(BOOT_PARTITION_RESERVED_SECTORS) \
		--boot-stage2-start-sector $(BOOT_STAGE2_START_SECTOR) \
		--boot-kernel-start-sector $(BOOT_KERNEL_START_SECTOR) \
		--data-partition-start-lba $(DATA_PARTITION_START_LBA) \
		--data-partition-sectors $(DATA_PARTITION_SECTORS) \
		--data-partition-image $(DATA_IMAGE) \
		--boot-file "$(KERNEL_BIN)::/KERNEL.BIN" \
		--boot-file "$(STAGE2_BIN)::/STAGE2.BIN" \
		--boot-file "$(BOOTLOADER_BG_BIN)::/VIBEBG.BIN" \
		--boot-file "$(VIBE_BOOTLOADER_AUDIO_RAW)::/VIBEBOOT.RAW" \
		--boot-file "$(BOOT_VOLUME_MANIFEST)::/LAYOUT.TXT" \
		--boot-file "$(BOOT_POLICY_MANIFEST)::/BOOTPOL.TXT" \
		--boot-file "$(DATA_IMAGE_MANIFEST)::/DATAINFO.TXT"
	@echo "Imagem gerada: $(IMAGE)"

run: $(IMAGE)
	@if command -v $(QEMU) >/dev/null 2>&1; then \
		$(QEMU) $(QEMU_RUN_COMMON_AUDIO_OPTS) -m $(QEMU_MEMORY_MB) -drive $(QEMU_IMAGE_OPTS) -boot c $(QEMU_NET_OPTS) $(QEMU_AUDIO_LIVE_OPTS); \
	else \
		echo "Aviso: $(QEMU) não encontrado. Tentando qemu-system-x86_64..."; \
		if command -v qemu-system-x86_64 >/dev/null 2>&1; then \
			echo "Usando qemu-system-x86_64"; \
			qemu-system-x86_64 $(QEMU_RUN_COMMON_AUDIO_OPTS) -m $(QEMU_MEMORY_MB) -drive $(QEMU_IMAGE_OPTS) -boot c $(QEMU_NET_OPTS) $(QEMU_AUDIO_LIVE_OPTS); \
		else \
			echo "Erro: QEMU não encontrado no sistema."; \
			echo "macOS (Homebrew): brew install qemu"; \
			exit 1; \
		fi; \
	fi

run-debug: run-debug-gui

run-debug-gui: $(IMAGE)
	@mkdir -p $(dir $(QEMU_SERIAL_LOG))
	@rm -f $(QEMU_SERIAL_LOG)
	@echo "QEMU GUI debug ativo. Serial do kernel: $(QEMU_SERIAL_LOG)"
	@if command -v $(QEMU) >/dev/null 2>&1; then \
		$(QEMU) $(QEMU_RUN_COMMON_AUDIO_OPTS) -m $(QEMU_MEMORY_MB) -drive $(QEMU_IMAGE_OPTS) -boot c $(QEMU_NET_OPTS) $(QEMU_AUDIO_LIVE_OPTS) -serial file:$(QEMU_SERIAL_LOG) -monitor none; \
	else \
		if command -v qemu-system-x86_64 >/dev/null 2>&1; then \
			qemu-system-x86_64 $(QEMU_RUN_COMMON_AUDIO_OPTS) -m $(QEMU_MEMORY_MB) -drive $(QEMU_IMAGE_OPTS) -boot c $(QEMU_NET_OPTS) $(QEMU_AUDIO_LIVE_OPTS) -serial file:$(QEMU_SERIAL_LOG) -monitor none; \
		else \
			echo "Erro: QEMU não encontrado"; \
			exit 1; \
		fi; \
	fi

run-headless-debug: $(IMAGE)
	@if command -v $(QEMU) >/dev/null 2>&1; then \
		$(QEMU) $(QEMU_RUN_COMMON_OPTS) -m $(QEMU_MEMORY_MB) -drive $(QEMU_IMAGE_OPTS) -boot c $(QEMU_NET_OPTS) -display none -serial stdio -monitor none; \
	else \
		if command -v qemu-system-x86_64 >/dev/null 2>&1; then \
			qemu-system-x86_64 $(QEMU_RUN_COMMON_OPTS) -m $(QEMU_MEMORY_MB) -drive $(QEMU_IMAGE_OPTS) -boot c $(QEMU_NET_OPTS) -display none -serial stdio -monitor none; \
		else \
			echo "Erro: QEMU não encontrado"; \
			exit 1; \
		fi; \
	fi

run-headless-audio-debug: $(IMAGE)
	@mkdir -p $(dir $(QEMU_AUDIO_CAPTURE_WAV))
	@rm -f $(QEMU_AUDIO_CAPTURE_WAV)
	@echo "QEMU headless audio debug ativo. Captura WAV: $(QEMU_AUDIO_CAPTURE_WAV)"
	@if command -v $(QEMU) >/dev/null 2>&1; then \
		$(QEMU) $(QEMU_RUN_COMMON_AUDIO_OPTS) -m $(QEMU_MEMORY_MB) -drive $(QEMU_IMAGE_OPTS) -boot c \
			$(QEMU_NET_OPTS) \
			-display none -serial stdio -monitor none \
			$(QEMU_AUDIO_CAPTURE_OPTS); \
	else \
		if command -v qemu-system-x86_64 >/dev/null 2>&1; then \
			qemu-system-x86_64 $(QEMU_RUN_COMMON_AUDIO_OPTS) -m $(QEMU_MEMORY_MB) -drive $(QEMU_IMAGE_OPTS) -boot c \
				$(QEMU_NET_OPTS) \
				-display none -serial stdio -monitor none \
				$(QEMU_AUDIO_CAPTURE_OPTS); \
		else \
			echo "Erro: QEMU não encontrado"; \
			exit 1; \
		fi; \
	fi

run-azalia: $(IMAGE)
	@echo "Perfil: QEMU live audio via Intel HDA/Azalia com Core 2 Duo SMP"
	@$(MAKE) --no-print-directory run \
		QEMU_RUN_CPU=core2duo \
		QEMU_RUN_SMP='2,sockets=1,cores=2,threads=1,maxcpus=2' \
		QEMU_AUDIO_LIVE_OPTS="$(QEMU_AUDIO_HDA_LIVE_OPTS)"

run-debug-azalia: $(IMAGE)
	@echo "Perfil: debug GUI com Intel HDA/Azalia e serial em $(QEMU_SERIAL_LOG)"
	@$(MAKE) --no-print-directory run-debug-gui \
		QEMU_RUN_CPU=core2duo \
		QEMU_RUN_SMP='2,sockets=1,cores=2,threads=1,maxcpus=2' \
		QEMU_AUDIO_LIVE_OPTS="$(QEMU_AUDIO_HDA_LIVE_OPTS)"

run-gpu-intel: $(IMAGE)
	@echo "Preset GPU Intel-like: plataforma q35 + std-vga/bochs (aproximacao QEMU)"
	@$(MAKE) --no-print-directory run \
		QEMU_RUN_MACHINE=q35 \
		QEMU_RUN_CPU=core2duo \
		QEMU_RUN_SMP='2,sockets=1,cores=2,threads=1,maxcpus=2' \
		QEMU_RUN_VGA=std \
		QEMU_RUN_GPU_OPTS=

run-gpu-nvidia: $(IMAGE)
	@echo "Preset GPU Nvidia-like: plataforma pc + vmware SVGA (aproximacao discreta no QEMU)"
	@$(MAKE) --no-print-directory run \
		QEMU_RUN_MACHINE=pc \
		QEMU_RUN_CPU=core2duo \
		QEMU_RUN_SMP='2,sockets=1,cores=2,threads=1,maxcpus=2' \
		QEMU_RUN_VGA=vmware \
		QEMU_RUN_GPU_OPTS=

run-gpu-amd: $(IMAGE)
	@echo "Preset GPU AMD/ATI-like: plataforma q35 + ati-vga"
	@$(MAKE) --no-print-directory run \
		QEMU_RUN_MACHINE=q35 \
		QEMU_RUN_CPU=core2duo \
		QEMU_RUN_SMP='2,sockets=1,cores=2,threads=1,maxcpus=2' \
		QEMU_RUN_VGA=none \
		QEMU_RUN_GPU_OPTS='-device ati-vga'

run-t61: $(IMAGE)
	@echo "Perfil ThinkPad T61: Core 2 Duo, 2 GiB, Intel HDA, GPU Intel-like"
	@$(MAKE) --no-print-directory run \
		QEMU_MEMORY_MB=2048 \
		QEMU_RUN_MACHINE=pc \
		QEMU_RUN_CPU=core2duo \
		QEMU_RUN_SMP='2,sockets=1,cores=2,threads=1,maxcpus=2' \
		QEMU_RUN_VGA=std \
		QEMU_AUDIO_LIVE_OPTS="$(QEMU_AUDIO_HDA_LIVE_OPTS)"

run-t400: $(IMAGE)
	@echo "Perfil ThinkPad T400: Core 2 Duo, 4 GiB, Intel HDA, chipset q35, GPU Intel-like"
	@$(MAKE) --no-print-directory run \
		QEMU_MEMORY_MB=4096 \
		QEMU_RUN_MACHINE=q35 \
		QEMU_RUN_CPU=core2duo \
		QEMU_RUN_SMP='2,sockets=1,cores=2,threads=1,maxcpus=2' \
		QEMU_RUN_VGA=std \
		QEMU_AUDIO_LIVE_OPTS="$(QEMU_AUDIO_HDA_LIVE_OPTS)"

run-acer: $(IMAGE)
	@echo "Perfil Acer Aspire-like: Core 2 Duo, 3 GiB, Intel HDA, GPU AMD/ATI-like"
	@$(MAKE) --no-print-directory run \
		QEMU_MEMORY_MB=3072 \
		QEMU_RUN_MACHINE=q35 \
		QEMU_RUN_CPU=core2duo \
		QEMU_RUN_SMP='2,sockets=1,cores=2,threads=1,maxcpus=2' \
		QEMU_RUN_VGA=none \
		QEMU_RUN_GPU_OPTS='-device ati-vga' \
		QEMU_AUDIO_LIVE_OPTS="$(QEMU_AUDIO_HDA_LIVE_OPTS)"

run-dell: $(IMAGE)
	@echo "Perfil Dell Latitude-like: Core 2 Duo, 4 GiB, Intel HDA, GPU Nvidia-like"
	@$(MAKE) --no-print-directory run \
		QEMU_MEMORY_MB=4096 \
		QEMU_RUN_MACHINE=pc \
		QEMU_RUN_CPU=core2duo \
		QEMU_RUN_SMP='2,sockets=1,cores=2,threads=1,maxcpus=2' \
		QEMU_RUN_VGA=vmware \
		QEMU_AUDIO_LIVE_OPTS="$(QEMU_AUDIO_HDA_LIVE_OPTS)"

run-headless-core2duo-debug: $(IMAGE)
	@if command -v $(QEMU) >/dev/null 2>&1; then \
		$(QEMU) -cpu core2duo -m $(QEMU_MEMORY_MB) -drive $(QEMU_IMAGE_OPTS) -boot c $(QEMU_NET_OPTS) -display none -serial stdio -monitor none; \
	else \
		if command -v qemu-system-x86_64 >/dev/null 2>&1; then \
			qemu-system-x86_64 -cpu core2duo -m $(QEMU_MEMORY_MB) -drive $(QEMU_IMAGE_OPTS) -boot c $(QEMU_NET_OPTS) -display none -serial stdio -monitor none; \
		else \
			echo "Erro: QEMU não encontrado"; \
			exit 1; \
		fi; \
	fi

run-headless-pentium-debug: $(IMAGE)
	@if command -v $(QEMU) >/dev/null 2>&1; then \
		$(QEMU) -cpu pentium -m $(QEMU_MEMORY_MB) -drive $(QEMU_IMAGE_OPTS) -boot c $(QEMU_NET_OPTS) -display none -serial stdio -monitor none; \
	else \
		if command -v qemu-system-x86_64 >/dev/null 2>&1; then \
			qemu-system-x86_64 -cpu pentium -m $(QEMU_MEMORY_MB) -drive $(QEMU_IMAGE_OPTS) -boot c $(QEMU_NET_OPTS) -display none -serial stdio -monitor none; \
		else \
			echo "Erro: QEMU não encontrado"; \
			exit 1; \
		fi; \
	fi

run-headless-atom-debug: $(IMAGE)
	@if command -v $(QEMU) >/dev/null 2>&1; then \
		$(QEMU) -cpu n270 -m $(QEMU_MEMORY_MB) -drive $(QEMU_IMAGE_OPTS) -boot c $(QEMU_NET_OPTS) -display none -serial stdio -monitor none; \
	else \
		if command -v qemu-system-x86_64 >/dev/null 2>&1; then \
			qemu-system-x86_64 -cpu n270 -m $(QEMU_MEMORY_MB) -drive $(QEMU_IMAGE_OPTS) -boot c $(QEMU_NET_OPTS) -display none -serial stdio -monitor none; \
		else \
			echo "Erro: QEMU não encontrado"; \
			exit 1; \
		fi; \
	fi

run-headless-ahci-debug: $(IMAGE)
	@if command -v $(QEMU) >/dev/null 2>&1; then \
		$(QEMU) -machine q35 -m $(QEMU_MEMORY_MB) \
			-drive if=none,id=bootdisk,$(QEMU_IMAGE_OPTS) \
			$(QEMU_NET_OPTS) \
			-device ahci,id=ahci \
			-device ide-hd,drive=bootdisk,bus=ahci.0,bootindex=0 \
			-boot c -display none -serial stdio -monitor none; \
	else \
		if command -v qemu-system-x86_64 >/dev/null 2>&1; then \
			qemu-system-x86_64 -machine q35 -m $(QEMU_MEMORY_MB) \
				-drive if=none,id=bootdisk,$(QEMU_IMAGE_OPTS) \
				$(QEMU_NET_OPTS) \
				-device ahci,id=ahci \
				-device ide-hd,drive=bootdisk,bus=ahci.0,bootindex=0 \
				-boot c -display none -serial stdio -monitor none; \
		else \
			echo "Erro: QEMU não encontrado"; \
			exit 1; \
		fi; \
	fi

run-headless-usb-debug: $(IMAGE)
	@if command -v $(QEMU) >/dev/null 2>&1; then \
		$(QEMU) -m $(QEMU_MEMORY_MB) \
			-drive if=none,id=usbdisk,$(QEMU_IMAGE_OPTS) \
			-usb \
			-device usb-storage,drive=usbdisk,bootindex=0 \
			-boot menu=off -display none -serial stdio -monitor none; \
	else \
		if command -v qemu-system-x86_64 >/dev/null 2>&1; then \
			qemu-system-x86_64 -m $(QEMU_MEMORY_MB) \
				-drive if=none,id=usbdisk,$(QEMU_IMAGE_OPTS) \
				-usb \
				-device usb-storage,drive=usbdisk,bootindex=0 \
				-boot menu=off -display none -serial stdio -monitor none; \
		else \
			echo "Erro: QEMU não encontrado"; \
			exit 1; \
		fi; \
	fi

run-headless-virtio-net-debug: $(IMAGE)
	@if command -v $(QEMU) >/dev/null 2>&1; then \
		$(QEMU) -m $(QEMU_MEMORY_MB) \
			-drive $(QEMU_IMAGE_OPTS) \
			-netdev user,id=net0 \
			-device virtio-net-pci,netdev=net0 \
			-boot c -display none -serial stdio -monitor none; \
	else \
		if command -v qemu-system-x86_64 >/dev/null 2>&1; then \
			qemu-system-x86_64 -m $(QEMU_MEMORY_MB) \
				-drive $(QEMU_IMAGE_OPTS) \
				-netdev user,id=net0 \
				-device virtio-net-pci,netdev=net0 \
				-boot c -display none -serial stdio -monitor none; \
		else \
			echo "Erro: QEMU não encontrado"; \
			exit 1; \
		fi; \
	fi

validate-phase6: $(IMAGE)
	$(PYTHON) tools/validate_phase6.py --image $(IMAGE) --report $(PHASE6_REPORT) --qemu $(QEMU) --memory-mb $(QEMU_MEMORY_MB) --timeout $(PHASE6_TIMEOUT)

validate-smp: $(IMAGE)
	$(PYTHON) tools/validate_smp.py --image $(IMAGE) --report $(SMP_REPORT) --qemu $(QEMU) --memory-mb $(QEMU_MEMORY_MB)

boot-smoke-image: check-tools
	$(MAKE) IMAGE="$(BOOT_SMOKE_IMAGE)" \
		DATA_IMAGE="$(BOOT_SMOKE_DATA_IMAGE)" \
		DATA_IMAGE_MANIFEST="$(BOOT_SMOKE_DATA_IMAGE_MANIFEST)" \
		IMAGE_ASSET_MANIFEST="$(BOOT_SMOKE_IMAGE_ASSET_MANIFEST)" \
		BOOT_VOLUME_MANIFEST="$(BOOT_SMOKE_VOLUME_MANIFEST)" \
		BOOT_POLICY_MANIFEST="$(BOOT_SMOKE_POLICY_MANIFEST)" \
		WALLPAPER_RUNTIME_PNG="$(WALLPAPER_RUNTIME_SMOKE_PNG)" \
		BOOTLOADER_BG_BIN="$(BOOTLOADER_BG_SMOKE_BIN)" \
		IMAGE_APP_BINS="$(BOOT_SMOKE_APP_BINS)" \
		"$(BOOT_SMOKE_IMAGE)"

run-headless-boot-smoke-debug: boot-smoke-image
	@if command -v $(QEMU) >/dev/null 2>&1; then \
		$(QEMU) -m $(QEMU_MEMORY_MB) -drive format=raw,file=$(BOOT_SMOKE_IMAGE),snapshot=on -boot c -display none -serial stdio -monitor none; \
	else \
		if command -v qemu-system-x86_64 >/dev/null 2>&1; then \
			qemu-system-x86_64 -m $(QEMU_MEMORY_MB) -drive format=raw,file=$(BOOT_SMOKE_IMAGE),snapshot=on -boot c -display none -serial stdio -monitor none; \
		else \
			echo "Erro: QEMU não encontrado"; \
			exit 1; \
		fi; \
	fi

validate-phase6-boot-smoke: boot-smoke-image
	$(PYTHON) tools/validate_phase6.py --image $(BOOT_SMOKE_IMAGE) --report $(BOOT_SMOKE_PHASE6_REPORT) --qemu $(QEMU) --memory-mb $(QEMU_MEMORY_MB) --timeout $(PHASE6_TIMEOUT)

# Cross-boot filesystem persistence gate (COMPLETION_ACTION_PLAN P2). Boots
# the SAME image twice WITHOUT snapshot and checks that boot 2 reads back the
# image boot 1 wrote ("fs: persistent image valid", no rebuild). Every other
# QEMU target uses snapshot=on, which discards writes, so this is the only
# automated proof of the disk write path + durability. QEMU is the necessary
# gate; real-media durability still needs physical hardware (P0).
STORAGE_PERSIST_REPORT ?= $(BUILD_DIR)/storage-persist-validation.md
STORAGE_PERSIST_TIMEOUT ?= 130
validate-storage-persist: $(IMAGE)
	$(PYTHON) tools/validate_storage_persist.py --image $(IMAGE) --report $(STORAGE_PERSIST_REPORT) --qemu $(QEMU) --memory-mb $(QEMU_MEMORY_MB) --timeout $(STORAGE_PERSIST_TIMEOUT)

validate-modular-apps: $(IMAGE)
	$(PYTHON) tools/validate_modular_apps.py --image $(IMAGE) --report $(MODULAR_APPS_REPORT) --qemu $(QEMU) --memory-mb $(QEMU_MEMORY_MB)

validate-phase-d: $(IMAGE)
	$(PYTHON) tools/validate_modular_apps.py --image $(IMAGE) --report $(PHASED_REPORT) --qemu $(QEMU) --memory-mb $(QEMU_MEMORY_MB) \
		--scenario desktop-visual-proof \
		--scenario vidmodes-shell \
		--scenario video-restart-desktop \
		--scenario video-restart-mouse-desktop
	$(PYTHON) tools/validate_gpu_backends.py --image $(IMAGE) --report $(GPU_BACKENDS_REPORT) --qemu $(QEMU) --memory-mb $(QEMU_MEMORY_MB)

validate-phase-c: $(IMAGE)
	$(MAKE) validate-audio-stack

validate-network-surface: $(IMAGE)
	$(PYTHON) tools/validate_network_surface.py --image $(IMAGE) --report build/network-surface-validation.md --qemu $(QEMU) --memory-mb $(QEMU_MEMORY_MB)

validate-network-datapath: $(IMAGE)
	$(PYTHON) tools/validate_network_datapath.py --image $(IMAGE) --report build/network-datapath-validation$(if $(NIC),-$(NIC)).md --qemu $(QEMU) --memory-mb $(QEMU_MEMORY_MB) $(if $(NIC),--nic $(NIC))

validate-network-terminal-surface: $(IMAGE)
	$(PYTHON) tools/validate_modular_apps.py --image $(IMAGE) --report build/network-terminal-validation.md --qemu $(QEMU) --memory-mb $(QEMU_MEMORY_MB) \
		--scenario network-terminal-surface

validate-video:
	$(MAKE) validate-phase-d
	$(MAKE) validate-gpu-backends-recovery

validate-phase-g: $(IMAGE)
	$(PYTHON) tools/validate_modular_apps.py --image $(IMAGE) --report $(PHASEG_REPORT) --qemu $(QEMU) --memory-mb $(QEMU_MEMORY_MB) \
		--scenario startx-autoboot-desktop \
		--scenario rescue-shell-boot \
		--scenario grep-explicit-path \
		--scenario phase-e-writeback-shell \
		--scenario input-restart-desktop \
		--scenario audio-restart-desktop \
		--scenario network-restart-desktop \
		--scenario video-restart-desktop \
		--scenario input-restart-mouse-desktop \
		--scenario audio-restart-mouse-desktop \
		--scenario network-restart-mouse-desktop \
		--scenario video-restart-mouse-desktop

validate-audio-stack: $(IMAGE)
	$(PYTHON) tools/validate_audio_stack.py --image $(IMAGE) --report $(AUDIO_STACK_REPORT) --qemu $(QEMU) --memory-mb $(QEMU_MEMORY_MB)

validate-finalization-1-4:
	$(MAKE) validate-network-surface
	$(MAKE) validate-phase-c
	$(MAKE) validate-video
	$(MAKE) validate-phase-g

validate-audio-stack-long: $(IMAGE)
	$(PYTHON) tools/validate_audio_stack.py --image $(IMAGE) --report build/audio-stack-long-validation.md --qemu $(QEMU) --memory-mb $(QEMU_MEMORY_MB) --record-ms 3000

validate-audio-stack-roundtrip: $(IMAGE)
	$(PYTHON) tools/validate_audio_stack.py --image $(IMAGE) --report build/audio-stack-roundtrip-validation.md --qemu $(QEMU) --memory-mb $(QEMU_MEMORY_MB) --record-ms 1500 --verify-capture-playback

validate-audio-hda-output: $(IMAGE)
	$(PYTHON) tools/validate_audio_stack.py --image $(IMAGE) --report build/audio-hda-output-validation.md --qemu $(QEMU) --memory-mb $(QEMU_MEMORY_MB) --audio-device intel-hda --audio-device hda-output --expect-backend compat-azalia --skip-capture --verify-playback-path /assets/vibe_os_desktop.wav --require-path-programmed --require-hardware-diag --require-desktop-startup-sound

validate-audio-hda-smoke: $(IMAGE)
	$(PYTHON) tools/validate_audio_stack.py --image $(IMAGE) --report build/audio-hda-validation.md --qemu $(QEMU) --memory-mb $(QEMU_MEMORY_MB) --audio-device intel-hda --audio-device hda-duplex --expect-backend compat-azalia --skip-capture

validate-audio-hda-playback: $(IMAGE)
	$(PYTHON) tools/validate_audio_stack.py --image $(IMAGE) --report build/audio-hda-playback-validation.md --qemu $(QEMU) --memory-mb $(QEMU_MEMORY_MB) --audio-device intel-hda --audio-device hda-duplex --expect-backend compat-azalia --skip-capture --verify-playback-path /assets/vibe_os_desktop.wav --require-path-programmed --require-hardware-diag

validate-audio-hda-startup: $(IMAGE)
	$(PYTHON) tools/validate_audio_stack.py --image $(IMAGE) --report build/audio-hda-startup-validation.md --qemu $(QEMU) --memory-mb $(QEMU_MEMORY_MB) --audio-device intel-hda --audio-device hda-duplex --expect-backend compat-azalia --skip-capture --require-boot-startup-sound --require-desktop-startup-sound

validate-audio-pcspkr-startup: $(IMAGE)
	$(PYTHON) tools/validate_audio_stack.py --image $(IMAGE) --report build/audio-pcspkr-startup-validation.md --qemu $(QEMU) --memory-mb $(QEMU_MEMORY_MB) --machine 'pc,pcspk-audiodev=snd0' --no-default-audio-device --qemu-arg=-audiodev --qemu-arg=wav,id=snd0,path=build/audio-pcspkr-startup.wav --expect-backend pcspkr --skip-capture --require-boot-startup-sound --require-desktop-startup-sound

validate-audio-matrix: $(IMAGE)
	$(MAKE) validate-audio-stack
	$(MAKE) validate-audio-stack-roundtrip
	$(MAKE) validate-audio-hda-output
	$(MAKE) validate-audio-hda-smoke
	$(MAKE) validate-audio-hda-playback
	$(MAKE) validate-audio-hda-startup
	$(MAKE) validate-audio-pcspkr-startup

validate-gpu-backends: $(IMAGE)
	$(PYTHON) tools/validate_gpu_backends.py --image $(IMAGE) --report $(GPU_BACKENDS_REPORT) --qemu $(QEMU) --memory-mb $(QEMU_MEMORY_MB)

rebuild-image-for-flagged-gpu-validation:
	rm -rf $(BUILD_DIR)/kernel $(BUILD_DIR)/userland $(BUILD_DIR)/lang
	rm -f $(KERNEL_ELF) $(KERNEL_BIN) $(DATA_IMAGE) $(DATA_IMAGE_MANIFEST) $(IMAGE)

validate-gpu-backends-i915-exp:
	$(MAKE) rebuild-image-for-flagged-gpu-validation
	$(MAKE) INTEL_I915_EXPERIMENTAL_COMMIT=1 $(IMAGE)
	$(PYTHON) tools/validate_gpu_backends.py --image $(IMAGE) --report $(GPU_BACKENDS_I915_EXPERIMENTAL_REPORT) --qemu $(QEMU) --memory-mb $(QEMU_MEMORY_MB)

validate-gpu-backends-recovery:
	$(MAKE) rebuild-image-for-flagged-gpu-validation
	$(MAKE) VIDEO_DRM_TEST_FORCE_HANDOFF_FAIL=1 $(IMAGE)
	$(PYTHON) tools/validate_gpu_backends.py --expect-recovery --image $(IMAGE) --report $(GPU_BACKENDS_RECOVERY_REPORT) --qemu $(QEMU) --memory-mb $(QEMU_MEMORY_MB)

validate-gpu-boot-800x600:
	rm -f $(STAGE2_BIN) $(IMAGE)
	$(MAKE) BOOT_NASM_DEFINES="-DBOOT_PREFERRED_VIDEO_WIDTH=800 -DBOOT_PREFERRED_VIDEO_HEIGHT=600" $(IMAGE)
	$(PYTHON) tools/validate_gpu_backends.py --expect-boot-mode 800x600 --image $(IMAGE) --report $(GPU_BACKENDS_REPORT) --qemu $(QEMU) --memory-mb $(QEMU_MEMORY_MB)

validate-gpu-boot-1024x768:
	rm -f $(STAGE2_BIN) $(IMAGE)
	$(MAKE) BOOT_NASM_DEFINES="-DBOOT_PREFERRED_VIDEO_WIDTH=1024 -DBOOT_PREFERRED_VIDEO_HEIGHT=768" $(IMAGE)
	$(PYTHON) tools/validate_gpu_backends.py --expect-boot-mode 1024x768 --image $(IMAGE) --report $(GPU_BACKENDS_REPORT) --qemu $(QEMU) --memory-mb $(QEMU_MEMORY_MB)

validate-gpu-boot-1360x768:
	rm -f $(STAGE2_BIN) $(IMAGE)
	$(MAKE) BOOT_NASM_DEFINES="-DBOOT_PREFERRED_VIDEO_WIDTH=1360 -DBOOT_PREFERRED_VIDEO_HEIGHT=768" $(IMAGE)
	$(PYTHON) tools/validate_gpu_backends.py --expect-boot-mode 1360x768 --image $(IMAGE) --report $(GPU_BACKENDS_REPORT) --qemu $(QEMU) --memory-mb $(QEMU_MEMORY_MB)

validate-gpu-boot-1366x768:
	rm -f $(STAGE2_BIN) $(IMAGE)
	$(MAKE) BOOT_NASM_DEFINES="-DBOOT_PREFERRED_VIDEO_WIDTH=1366 -DBOOT_PREFERRED_VIDEO_HEIGHT=768" $(IMAGE)
	$(PYTHON) tools/validate_gpu_backends.py --expect-boot-mode 1366x768 --image $(IMAGE) --report $(GPU_BACKENDS_REPORT) --qemu $(QEMU) --memory-mb $(QEMU_MEMORY_MB)

validate-gpu-boot-1920x1080:
	rm -f $(STAGE2_BIN) $(IMAGE)
	$(MAKE) BOOT_NASM_DEFINES="-DBOOT_PREFERRED_VIDEO_WIDTH=1920 -DBOOT_PREFERRED_VIDEO_HEIGHT=1080" $(IMAGE)
	$(PYTHON) tools/validate_gpu_backends.py --expect-boot-mode 1920x1080 --image $(IMAGE) --report $(GPU_BACKENDS_REPORT) --qemu $(QEMU) --memory-mb $(QEMU_MEMORY_MB)

validate-startx-800x600:
	rm -f $(STAGE2_BIN) $(IMAGE)
	$(MAKE) BOOT_NASM_DEFINES="-DBOOT_PREFERRED_VIDEO_WIDTH=800 -DBOOT_PREFERRED_VIDEO_HEIGHT=600" $(IMAGE)
	$(PYTHON) tools/validate_modular_apps.py --scenario startx-autoboot-desktop --scenario vidmodes-shell --scenario input-restart-desktop --scenario audio-restart-desktop --scenario network-restart-desktop --scenario video-restart-desktop --expect-boot-mode 800x600 --image $(IMAGE) --report $(MODULAR_APPS_REPORT) --qemu $(QEMU) --memory-mb $(QEMU_MEMORY_MB)

validate-startx-1024x768:
	rm -f $(STAGE2_BIN) $(IMAGE)
	$(MAKE) BOOT_NASM_DEFINES="-DBOOT_PREFERRED_VIDEO_WIDTH=1024 -DBOOT_PREFERRED_VIDEO_HEIGHT=768" $(IMAGE)
	$(PYTHON) tools/validate_modular_apps.py --scenario startx-autoboot-desktop --scenario vidmodes-shell --scenario input-restart-desktop --scenario audio-restart-desktop --scenario network-restart-desktop --scenario video-restart-desktop --expect-boot-mode 1024x768 --image $(IMAGE) --report $(MODULAR_APPS_REPORT) --qemu $(QEMU) --memory-mb $(QEMU_MEMORY_MB)

validate-abi-compat-surface:
	rm -f $(STAGE2_BIN) $(IMAGE)
	$(MAKE) BOOT_NASM_DEFINES="-DBOOT_PREFERRED_VIDEO_WIDTH=800 -DBOOT_PREFERRED_VIDEO_HEIGHT=600" $(IMAGE)
	$(PYTHON) tools/validate_modular_apps.py --scenario bsd-utils-terminal-surface --scenario vi-compat-editor --scenario mg-compat-editor --expect-boot-mode 800x600 --image $(IMAGE) --report $(MODULAR_APPS_REPORT) --qemu $(QEMU) --memory-mb $(QEMU_MEMORY_MB)

validate-abi-final:
	$(MAKE) compat-build
	$(PYTHON) tools/validate_abi_contracts.py
	$(MAKE) validate-abi-compat-surface

validate-doom-800x600:
	rm -f $(STAGE2_BIN) $(IMAGE)
	$(MAKE) BOOT_NASM_DEFINES="-DBOOT_PREFERRED_VIDEO_WIDTH=800 -DBOOT_PREFERRED_VIDEO_HEIGHT=600" $(IMAGE)
	$(PYTHON) tools/validate_modular_apps.py --scenario doom-assets-app --expect-boot-mode 800x600 --image $(IMAGE) --report $(MODULAR_APPS_REPORT) --qemu $(QEMU) --memory-mb $(QEMU_MEMORY_MB)

validate-doom-1024x768:
	rm -f $(STAGE2_BIN) $(IMAGE)
	$(MAKE) BOOT_NASM_DEFINES="-DBOOT_PREFERRED_VIDEO_WIDTH=1024 -DBOOT_PREFERRED_VIDEO_HEIGHT=768" $(IMAGE)
	$(PYTHON) tools/validate_modular_apps.py --scenario doom-assets-app --expect-boot-mode 1024x768 --image $(IMAGE) --report $(MODULAR_APPS_REPORT) --qemu $(QEMU) --memory-mb $(QEMU_MEMORY_MB)

validate-craft-800x600:
	rm -f $(STAGE2_BIN) $(IMAGE)
	$(MAKE) BOOT_NASM_DEFINES="-DBOOT_PREFERRED_VIDEO_WIDTH=800 -DBOOT_PREFERRED_VIDEO_HEIGHT=600" $(IMAGE)
	$(PYTHON) tools/validate_modular_apps.py --scenario craft-assets-app --expect-boot-mode 800x600 --image $(IMAGE) --report $(MODULAR_APPS_REPORT) --qemu $(QEMU) --memory-mb $(QEMU_MEMORY_MB)

validate-craft-1024x768:
	rm -f $(STAGE2_BIN) $(IMAGE)
	$(MAKE) BOOT_NASM_DEFINES="-DBOOT_PREFERRED_VIDEO_WIDTH=1024 -DBOOT_PREFERRED_VIDEO_HEIGHT=768" $(IMAGE)
	$(PYTHON) tools/validate_modular_apps.py --scenario craft-assets-app --expect-boot-mode 1024x768 --image $(IMAGE) --report $(MODULAR_APPS_REPORT) --qemu $(QEMU) --memory-mb $(QEMU_MEMORY_MB)

debug: $(IMAGE)
	@echo "QEMU pausado para GDB em tcp::1234. Serial do kernel no terminal."
	@if command -v $(QEMU) >/dev/null 2>&1; then \
		$(QEMU) -m $(QEMU_MEMORY_MB) -drive $(QEMU_IMAGE_OPTS) -boot c -serial stdio -monitor none -s -S; \
	else \
		echo "Aviso: $(QEMU) não encontrado. Tentando qemu-system-x86_64..."; \
		if command -v qemu-system-x86_64 >/dev/null 2>&1; then \
			echo "Usando qemu-system-x86_64 com debug"; \
			qemu-system-x86_64 -m $(QEMU_MEMORY_MB) -drive $(QEMU_IMAGE_OPTS) -boot c -serial stdio -monitor none -s -S; \
		else \
			echo "Erro: QEMU não encontrado no sistema."; \
			echo "macOS (Homebrew): brew install qemu"; \
			exit 1; \
		fi; \
	fi

# ============ GLIBC & APPS BUILD TARGETS ============

# Build glibc library (optional, standalone)
# Usage: make glibc       (full glibc)
#        make glibc-core  (core subset only)
glibc:
	@echo "Building full glibc..."
	$(MAKE) -f Build.glibc.mk glibc-build

glibc-core:
	@echo "Building glibc-core (810 core functions)..."
	$(MAKE) -f Build.glibc.mk glibc-build

# Build language apps as standalone build/bin executables
# Apps link glibc.a statically (or can load glibc.so dynamically at runtime)
apps: glibc-core | $(APP_BIN_DIR) $(APP_LIB_DIR)
	@echo "Building language runtime apps to $(APP_BIN_DIR)..."
	@mkdir -p $(APP_BIN_DIR) $(APP_LIB_DIR)
	@echo "$(HELLO_APP_BIN) -> $(APP_BIN_DIR)/hello"
	@cp $(HELLO_APP_BIN) $(APP_BIN_DIR)/hello || echo "WARNING: hello app not found"
	@if [ -f "$(JS_APP_BIN)" ]; then cp $(JS_APP_BIN) $(APP_BIN_DIR)/js; else echo "WARNING: js app not found"; fi
	@if [ -f "$(RUBY_APP_BIN)" ]; then cp $(RUBY_APP_BIN) $(APP_BIN_DIR)/ruby; else echo "WARNING: ruby app not found"; fi
	@if [ -f "$(PYTHON_APP_BIN)" ]; then cp $(PYTHON_APP_BIN) $(APP_BIN_DIR)/python; else echo "WARNING: python app not found"; fi
	@if [ -f "$(JAVA_APP_BIN)" ]; then cp $(JAVA_APP_BIN) $(APP_BIN_DIR)/java; else echo "WARNING: java app not found"; fi
	@if [ -f "$(JAVAC_APP_BIN)" ]; then cp $(JAVAC_APP_BIN) $(APP_BIN_DIR)/javac; else echo "WARNING: javac app not found"; fi
	@if [ -f "build/libglibc-full.a" ]; then cp build/libglibc-full.a $(APP_LIB_DIR)/libglibc.a; else cp build/libglibc-core.a $(APP_LIB_DIR)/libglibc.a; fi || true
	@echo "Apps built to $(APP_BIN_DIR)"

$(APP_BIN_DIR):
	mkdir -p $(APP_BIN_DIR)

$(APP_LIB_DIR):
	mkdir -p $(APP_LIB_DIR)

glibc-clean:
	@echo "Cleaning glibc build..."
	$(MAKE) -f Build.glibc.mk glibc-clean

apps-clean:
	@echo "Cleaning apps..."
	rm -rf $(APP_BIN_DIR) $(APP_LIB_DIR)
	rm -f $(HELLO_APP_OBJS) $(HELLO_APP_ELF) $(HELLO_APP_BIN)
	rm -f $(JS_APP_OBJS) $(JS_APP_ELF) $(JS_APP_BIN)
	rm -f $(RUBY_APP_OBJS) $(RUBY_APP_ELF) $(RUBY_APP_BIN)
	rm -f $(PYTHON_APP_OBJS) $(PYTHON_APP_ELF) $(PYTHON_APP_BIN)
	rm -f $(JAVA_APP_OBJS) $(JAVA_APP_ELF) $(JAVA_APP_BIN)
	rm -f $(JAVAC_APP_OBJS) $(JAVAC_APP_ELF) $(JAVAC_APP_BIN)

# Standalone app compilation (requires vendor builds)
# These compile each app to build/bin independently
app-hello: $(HELLO_APP_BIN) | $(APP_BIN_DIR)
	@echo "Copying hello to $(APP_BIN_DIR)..."
	@cp $(HELLO_APP_BIN) $(APP_BIN_DIR)/hello
	@echo "✓ $(APP_BIN_DIR)/hello ready"

app-js: $(JS_APP_BIN) | $(APP_BIN_DIR)
	@echo "Copying js to $(APP_BIN_DIR)..."
	@cp $(JS_APP_BIN) $(APP_BIN_DIR)/js
	@echo "✓ $(APP_BIN_DIR)/js ready"

app-ruby:
	@if [ -f "$(RUBY_APP_BIN)" ]; then mkdir -p $(APP_BIN_DIR); cp $(RUBY_APP_BIN) $(APP_BIN_DIR)/ruby; echo "✓ $(APP_BIN_DIR)/ruby ready"; else echo "ℹ ruby.app not built. Requires mruby vendor. See BUILD_LANGS.md"; fi

app-python:
	@if [ -f "$(PYTHON_APP_BIN)" ]; then mkdir -p $(APP_BIN_DIR); cp $(PYTHON_APP_BIN) $(APP_BIN_DIR)/python; echo "✓ $(APP_BIN_DIR)/python ready"; else echo "ℹ python.app not built. Requires micropython vendor. See BUILD_LANGS.md"; fi

app-java: $(JAVA_APP_BIN) | $(APP_BIN_DIR)
	@echo "Copying java to $(APP_BIN_DIR)..."
	@cp $(JAVA_APP_BIN) $(APP_BIN_DIR)/java
	@echo "✓ $(APP_BIN_DIR)/java ready"

app-javac: $(JAVAC_APP_BIN) | $(APP_BIN_DIR)
	@echo "Copying javac to $(APP_BIN_DIR)..."
	@cp $(JAVAC_APP_BIN) $(APP_BIN_DIR)/javac
	@echo "✓ $(APP_BIN_DIR)/javac ready"

clean:
	rm -rf $(BUILD_DIR)
	find kernel -type f \( -name '*.o' -o -name '*.d' \) -delete
	rm -f "lang/vendor/glibc/benchtests/strcoll-inputs/filelist#C 2"

full: clean all

img: $(IMAGE)
	@echo "Imagem pronta: $(IMAGE)"

run-imb: all
	$(MAKE) run
	$(MAKE) imb

imb: $(IMAGE)
	@echo "Copiando imagem para build/vibe-os-usb.img"
	@cp $(IMAGE) build/vibe-os-usb.img
	@echo "Imagem para hardware real pronta: build/vibe-os-usb.img"

# Host-side unit tests for the freestanding app runtime. These compile
# lang/sdk/app_runtime.c against a fake host vtable and run on the build
# host - no QEMU, no kernel, seconds to run.
HOST_TEST_CFLAGS := -m32 -ffreestanding -fno-builtin -I. -Ilang/include

test-fdopen:
	@mkdir -p $(BUILD_DIR)
	$(CC) $(HOST_TEST_CFLAGS) tests/host_test_fdopen.c lang/sdk/app_runtime.c -o $(BUILD_DIR)/host_test_fdopen
	$(BUILD_DIR)/host_test_fdopen

test-dns:
	@mkdir -p $(BUILD_DIR)
	$(CC) $(HOST_TEST_CFLAGS) -Icompat/include -Iapplications/ported/include -Iheaders tests/host_test_dns.c lang/sdk/app_runtime.c -o $(BUILD_DIR)/host_test_dns
	$(BUILD_DIR)/host_test_dns

test-host: test-fdopen test-dns
	@echo "host unit tests passed"

.PHONY: test-host

# Links2 Web Browser port
LINKS2_APP_BIN := build/links2/links2.app
LINKS2_STAMP := build/links2.stamp

$(LINKS2_STAMP): $(COMPAT_LIB) Build.links2.mk lang/sdk/app_entry.c lang/sdk/app_runtime.c lang/include/vibe_app.h tools/patch_app_header.py \
	userland/applications/network/links2/config-vibeos.h \
	userland/applications/network/links2/vibeos.c userland/applications/network/links2/vibeos.h
	@mkdir -p $(dir $@)
	$(MAKE) -j1 -f Build.links2.mk \
		CC="$(CC)" LD="$(LD)" OBJCOPY="$(OBJCOPY)" NM="$(NM)" AR="$(AR)" RANLIB="$(RANLIB)" \
		ported-links2
	@touch $@

$(LINKS2_APP_BIN): $(LINKS2_STAMP)

ported-links2: $(LINKS2_APP_BIN)

ported-links2-clean:
	rm -f $(LINKS2_STAMP)
	$(MAKE) -f Build.links2.mk ported-links2-clean

-include $(shell test -d $(BUILD_DIR) && find $(BUILD_DIR) -name '*.d' ! -name '* *' -print)

