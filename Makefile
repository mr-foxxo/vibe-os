SHELL := /bin/sh
.DEFAULT_GOAL := all

# ARCHITECTURE:
# - make            : Build kernel (no glibc, uses stubs)
# - make glibc      : Build glibc library (lang/vendor/glibc -> lib/libglibc.*)
# - make apps       : Build language runtimes in /bin
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

ifeq ($(strip $(AS)),)
AS := nasm
endif
ifeq ($(strip $(QEMU)),)
QEMU := qemu-system-i386
endif
ifeq ($(strip $(PYTHON)),)
PYTHON := python3
endif

ifeq ($(strip $(CC)),)
ifeq ($(HAS_CROSS_TOOLCHAIN),1)
CC := $(TOOLCHAIN_PREFIX)gcc
else
CC := gcc
endif
endif

ifeq ($(strip $(LD)),)
ifeq ($(HAS_CROSS_TOOLCHAIN),1)
LD := $(TOOLCHAIN_PREFIX)ld
else
LD := ld
endif
endif

ifeq ($(strip $(NM)),)
ifeq ($(HAS_CROSS_TOOLCHAIN),1)
NM := $(TOOLCHAIN_PREFIX)nm
else
NM := nm
endif
endif

ifeq ($(strip $(OBJCOPY)),)
ifeq ($(HAS_CROSS_TOOLCHAIN),1)
OBJCOPY := $(TOOLCHAIN_PREFIX)objcopy
else
OBJCOPY := objcopy
endif
endif

ifeq ($(strip $(AR)),)
ifeq ($(HAS_CROSS_TOOLCHAIN),1)
AR := $(TOOLCHAIN_PREFIX)ar
else
AR := ar
endif
endif

ifeq ($(strip $(RANLIB)),)
ifeq ($(HAS_CROSS_TOOLCHAIN),1)
RANLIB := $(TOOLCHAIN_PREFIX)ranlib
else
RANLIB := ranlib
endif
endif

TOOLCHAIN_MODE := host
ifeq ($(HAS_CROSS_TOOLCHAIN),1)
TOOLCHAIN_MODE := cross
endif

ifeq ($(UNAME_S),Darwin)
ifneq ($(HAS_CROSS_TOOLCHAIN),1)
ifneq ($(ALLOW_HOST_TOOLCHAIN),1)
$(error Toolchain 'i686-elf-*' nao encontrada no macOS. Instale com 'brew install i686-elf-gcc' (ou rode com ALLOW_HOST_TOOLCHAIN=1 e toolchain host por sua conta))
endif
endif
endif

BUILD_DIR := build
BOOT_DIR := boot
USERLAND_DIR := userland
LINKER_DIR := linker
BOOT_KERNEL_SECTORS := 1024
APPFS_DIRECTORY_LBA := 1025
APPFS_DIRECTORY_SECTORS := 8
APPFS_APP_AREA_SECTORS := 1536

# Kernel sources - kernel only, no stage2
KERNEL_SRCS := $(shell find kernel -name '*.c')
KEYMAP_SRCS := $(shell find kernel/drivers/input/keymaps -name '*.c')
KERNEL_OBJS := $(addprefix $(BUILD_DIR)/,$(patsubst %.c,%.o,$(KERNEL_SRCS)))
KEYMAP_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(KEYMAP_SRCS))

KERNEL_ASM_SRCS := $(shell find kernel_asm -name '*.asm')
KERNEL_ASM_OBJS := $(patsubst kernel_asm/%.asm,$(BUILD_DIR)/kernel_asm/%.o,$(KERNEL_ASM_SRCS))

# Userland linked into the kernel image.
USERLAND_SRCS := \
	$(USERLAND_DIR)/userland.c \
	$(USERLAND_DIR)/modules/shell.c \
	$(USERLAND_DIR)/modules/busybox.c \
	$(USERLAND_DIR)/modules/console.c \
	$(USERLAND_DIR)/modules/fs.c \
	$(USERLAND_DIR)/modules/bmp.c \
	$(USERLAND_DIR)/modules/lang_loader.c \
	$(USERLAND_DIR)/modules/utils.c \
	$(USERLAND_DIR)/modules/syscalls.c \
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
	$(USERLAND_DIR)/applications/sketchpad.c \
	$(USERLAND_DIR)/applications/games/snake.c \
	$(USERLAND_DIR)/applications/games/tetris.c \
	$(USERLAND_DIR)/applications/games/pacman.c \
	$(USERLAND_DIR)/applications/games/space_invaders.c \
	$(USERLAND_DIR)/applications/games/pong.c \
	$(USERLAND_DIR)/applications/games/donkey_kong.c \
	$(USERLAND_DIR)/applications/games/brick_race.c \
	$(USERLAND_DIR)/applications/games/flap_birb.c \
	$(USERLAND_DIR)/applications/games/doom.c \
	$(USERLAND_DIR)/applications/games/doom_port/doom_port_main.c \
	$(USERLAND_DIR)/applications/games/doom_port/doom_libc_shim.c \
	$(USERLAND_DIR)/applications/games/doom_port/i_system_vibe.c \
	$(USERLAND_DIR)/applications/games/doom_port/i_video_vibe.c \
	$(USERLAND_DIR)/applications/games/doom_port/i_sound_vibe.c \
	$(USERLAND_DIR)/applications/games/doom_port/i_net_vibe.c
USERLAND_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(USERLAND_SRCS))

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
DOOM_CFLAGS = -m32 -Os -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-builtin -nostdlib -Wall -Wextra -I. -Iheaders -Iuserland -Ilang/include -Iuserland/lua/include -Iuserland/lua/vendor/lua-5.4.6/src -Ilang/vendor/quickjs-ng -Ilang/vendor/mruby/include -Ilang/vendor/micropython -Ilang/glibc/include -DNORMALUNIX -DLINUX -DSEEK_SET=0 -DSEEK_CUR=1 -DSEEK_END=2 -include stdio.h -include stdlib.h -include string.h -Wno-sequence-point -Wno-unused-const-variable -Wno-unused-but-set-variable $(DOOM_SYMBOL_REMAP)
DOOM_PORT_SRC_DIR := $(USERLAND_DIR)/applications/games/doom_port
DOOM_PORT_CFLAGS = $(CFLAGS) $(DOOM_SYMBOL_REMAP)

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
KERNEL_ELF := $(BUILD_DIR)/kernel.elf
KERNEL_BIN := $(BUILD_DIR)/kernel.bin
USERLAND_MAIN_ELF := $(BUILD_DIR)/userland-main.elf
USERLAND_MAIN_BIN := $(BUILD_DIR)/userland-main.bin
IMAGE := $(BUILD_DIR)/boot.img

CFLAGS := -m32 -Os -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-builtin -nostdlib -Wall -Wextra -Werror -I. -Iheaders -Iuserland -Ilang/include -Iuserland/lua/include -Iuserland/lua/vendor/lua-5.4.6/src -Ilang/vendor/quickjs-ng -Ilang/vendor/mruby/include -Ilang/vendor/micropython
CFLAGS += -Ilang/glibc/include
CFLAGS += -MMD -MP
LDFLAGS_KERNEL := -m elf_i386 -T $(LINKER_DIR)/kernel.ld -nostdlib -N --allow-multiple-definition
LDFLAGS_USERLAND := -m elf_i386 -T $(LINKER_DIR)/userland.ld -nostdlib -N
LDFLAGS_APP := -m elf_i386 -T $(LINKER_DIR)/app.ld -nostdlib -N

HELLO_APP_BUILD_DIR := $(BUILD_DIR)/lang/hello
HELLO_APP_OBJS := \
	$(HELLO_APP_BUILD_DIR)/app_entry.o \
	$(HELLO_APP_BUILD_DIR)/app_runtime.o \
	$(HELLO_APP_BUILD_DIR)/hello_main.o
HELLO_APP_ELF := $(BUILD_DIR)/lang/hello.elf
HELLO_APP_BIN := $(BUILD_DIR)/lang/hello.app

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
TRUE_APP_BIN := $(BUILD_DIR)/ported/true.app
FALSE_APP_BIN := $(BUILD_DIR)/ported/false.app
PRINTF_APP_BIN := $(BUILD_DIR)/ported/printf.app
PORTED_APPS_STAMP := $(BUILD_DIR)/.ported_apps.stamp

LANG_APP_BINS := $(HELLO_APP_BIN) $(JS_APP_BIN) $(RUBY_APP_BIN) $(PYTHON_APP_BIN) $(ECHO_APP_BIN) $(CAT_APP_BIN) $(WC_APP_BIN) $(PWD_APP_BIN) $(HEAD_APP_BIN) $(SLEEP_APP_BIN) $(RMDIR_APP_BIN) $(TAIL_APP_BIN) $(GREP_APP_BIN) $(LOADKEYS_APP_BIN) $(TRUE_APP_BIN) $(FALSE_APP_BIN) $(PRINTF_APP_BIN)

# Include compatibility layer build rules
include Build.compat.mk

REQUIRED_BUILD_TOOLS := $(AS) $(CC) $(LD) $(NM) $(OBJCOPY) $(AR) $(RANLIB) $(PYTHON)

all: check-tools $(IMAGE) $(USERLAND_MAIN_BIN)
# Note: glibc separate build available via: make -f Build.glibc.mk glibc-build
# To link glibc instead of stubs, modify KERNEL_ELF dependencies below

check-tools:
	@echo "Toolchain mode: $(TOOLCHAIN_MODE) ($(UNAME_S))"; \
	for tool in $(REQUIRED_BUILD_TOOLS); do \
		if ! command -v $$tool >/dev/null 2>&1; then \
			echo "Erro: '$$tool' nao encontrado no PATH."; \
			if [ "$(UNAME_S)" = "Darwin" ]; then \
				echo "macOS (Homebrew): brew install nasm i686-elf-gcc qemu"; \
			else \
				echo "Linux: instale binutils/gcc 32-bit + nasm + qemu-system-x86"; \
				echo "Ou use toolchain cruzada i686-elf-*."; \
			fi; \
			exit 1; \
		fi; \
	done

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BOOT_BIN): $(BOOT_DIR)/stage1.asm | $(BUILD_DIR)
	$(AS) -f bin $< -o $@
	@boot_size=$$(wc -c < $@); \
	if [ "$$boot_size" -ne 512 ]; then \
		echo "Erro: boot sector precisa ter 512 bytes (atual: $$boot_size)."; \
		exit 1; \
	fi

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

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

$(KERNEL_ELF): $(KERNEL_OBJS) $(KERNEL_ASM_OBJS) $(USERLAND_OBJS) $(LINKER_DIR)/kernel.ld $(COMPAT_LIB)
	$(LD) $(LDFLAGS_KERNEL) $(KERNEL_OBJS) $(KERNEL_ASM_OBJS) $(USERLAND_OBJS) $(COMPAT_LIB) -o $@

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@
	@kernel_sectors=$$((($$(wc -c < $@) + 511) / 512)); \
	if [ "$$kernel_sectors" -gt "$(BOOT_KERNEL_SECTORS)" ]; then \
		echo "Erro: kernel.bin excede o limite do bootloader ($$kernel_sectors > $(BOOT_KERNEL_SECTORS) setores)."; \
		exit 1; \
	fi

$(USERLAND_MAIN_ELF): $(USERLAND_OBJS) $(LINKER_DIR)/userland.ld
	$(LD) $(LDFLAGS_USERLAND) $(USERLAND_OBJS) -o $@

$(USERLAND_MAIN_BIN): $(USERLAND_MAIN_ELF)
	$(OBJCOPY) -O binary $< $@

$(HELLO_APP_ELF): $(HELLO_APP_OBJS) $(LINKER_DIR)/app.ld
	$(LD) $(LDFLAGS_APP) $(HELLO_APP_OBJS) -o $@

$(HELLO_APP_BIN): $(HELLO_APP_ELF)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@

$(JS_APP_ELF): $(JS_APP_OBJS) $(LINKER_DIR)/app.ld
	$(LD) $(LDFLAGS_APP) $(JS_APP_OBJS) -o $@

$(JS_APP_BIN): $(JS_APP_ELF)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@

$(RUBY_APP_ELF): $(RUBY_APP_OBJS) $(LINKER_DIR)/app.ld
	$(LD) $(LDFLAGS_APP) $(RUBY_APP_OBJS) -o $@

$(RUBY_APP_BIN): $(RUBY_APP_ELF)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@

$(PYTHON_APP_ELF): $(PYTHON_APP_OBJS) $(LINKER_DIR)/app.ld
	$(LD) $(LDFLAGS_APP) $(PYTHON_APP_OBJS) -o $@

$(PYTHON_APP_BIN): $(PYTHON_APP_ELF)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@

# Ported GNU apps (echo, cat, wc, head, tail, grep, etc)
# Build once via stamp to avoid parallel duplicate sub-make executions.
$(PORTED_APPS_STAMP): $(COMPAT_LIB)
	@mkdir -p $(dir $@)
	$(MAKE) -j1 -f Build.ported.mk \
		CC="$(CC)" LD="$(LD)" OBJCOPY="$(OBJCOPY)" NM="$(NM)" AR="$(AR)" RANLIB="$(RANLIB)" \
		ported-echo ported-cat ported-wc ported-pwd ported-head ported-sleep ported-rmdir ported-tail ported-grep ported-loadkeys ported-true ported-false ported-printf
	@touch $@

$(ECHO_APP_BIN) $(CAT_APP_BIN) $(WC_APP_BIN) $(PWD_APP_BIN) $(HEAD_APP_BIN) $(SLEEP_APP_BIN) $(RMDIR_APP_BIN) $(TAIL_APP_BIN) $(GREP_APP_BIN) $(LOADKEYS_APP_BIN) $(TRUE_APP_BIN) $(FALSE_APP_BIN) $(PRINTF_APP_BIN): $(PORTED_APPS_STAMP)

$(IMAGE): $(BOOT_BIN) $(KERNEL_BIN) $(LANG_APP_BINS)
	dd if=/dev/zero of=$@ bs=1474560 count=1
	dd if=$(BOOT_BIN) of=$@ bs=512 count=1 conv=notrunc
	dd if=$(KERNEL_BIN) of=$@ bs=512 seek=1 conv=notrunc
	$(PYTHON) tools/build_appfs.py --image $@ --directory-lba $(APPFS_DIRECTORY_LBA) --directory-sectors $(APPFS_DIRECTORY_SECTORS) --app-area-sectors $(APPFS_APP_AREA_SECTORS) $(LANG_APP_BINS)
	@echo "Imagem gerada: $(IMAGE)"

run: $(IMAGE)
	@if command -v $(QEMU) >/dev/null 2>&1; then \
		$(QEMU) -drive format=raw,file=$(IMAGE) -boot c; \
	else \
		echo "Aviso: $(QEMU) não encontrado. Tentando qemu-system-x86_64..."; \
		if command -v qemu-system-x86_64 >/dev/null 2>&1; then \
			echo "Usando qemu-system-x86_64"; \
			qemu-system-x86_64 -drive format=raw,file=$(IMAGE) -boot c; \
		else \
			echo "Erro: QEMU não encontrado no sistema."; \
			echo "macOS (Homebrew): brew install qemu"; \
			exit 1; \
		fi; \
	fi

run-debug: $(IMAGE)
	@if command -v $(QEMU) >/dev/null 2>&1; then \
		$(QEMU) -drive format=raw,file=$(IMAGE) -boot c -serial stdio; \
	else \
		if command -v qemu-system-x86_64 >/dev/null 2>&1; then \
			qemu-system-x86_64 -drive format=raw,file=$(IMAGE) -boot c -serial stdio; \
		else \
			echo "Erro: QEMU não encontrado"; \
			exit 1; \
		fi; \
	fi

debug: $(IMAGE)
	@if command -v $(QEMU) >/dev/null 2>&1; then \
		$(QEMU) -drive format=raw,file=$(IMAGE) -boot c -s -S; \
	else \
		echo "Aviso: $(QEMU) não encontrado. Tentando qemu-system-x86_64..."; \
		if command -v qemu-system-x86_64 >/dev/null 2>&1; then \
			echo "Usando qemu-system-x86_64 com debug"; \
			qemu-system-x86_64 -drive format=raw,file=$(IMAGE) -boot c -s -S; \
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

# Build language apps as standalone /bin executables
# Apps link glibc.a statically (or can load glibc.so dynamically at runtime)
apps: glibc-core | bin lib
	@echo "Building language runtime apps to /bin..."
	@mkdir -p bin lib
	@echo "$(HELLO_APP_BIN) -> bin/hello"
	@cp $(HELLO_APP_BIN) bin/hello || echo "WARNING: hello app not found"
	@if [ -f "$(JS_APP_BIN)" ]; then cp $(JS_APP_BIN) bin/js; else echo "WARNING: js app not found"; fi
	@if [ -f "$(RUBY_APP_BIN)" ]; then cp $(RUBY_APP_BIN) bin/ruby; else echo "WARNING: ruby app not found"; fi
	@if [ -f "$(PYTHON_APP_BIN)" ]; then cp $(PYTHON_APP_BIN) bin/python; else echo "WARNING: python app not found"; fi
	@if [ -f "build/libglibc-full.a" ]; then cp build/libglibc-full.a lib/libglibc.a; else cp build/libglibc-core.a lib/libglibc.a; fi || true
	@echo "Apps built to /bin"

bin:
	mkdir -p bin

lib:
	mkdir -p lib

glibc-clean:
	@echo "Cleaning glibc build..."
	$(MAKE) -f Build.glibc.mk glibc-clean

apps-clean:
	@echo "Cleaning apps..."
	rm -rf bin/ lib/
	rm -f $(HELLO_APP_OBJS) $(HELLO_APP_ELF) $(HELLO_APP_BIN)
	rm -f $(JS_APP_OBJS) $(JS_APP_ELF) $(JS_APP_BIN)
	rm -f $(RUBY_APP_OBJS) $(RUBY_APP_ELF) $(RUBY_APP_BIN)
	rm -f $(PYTHON_APP_OBJS) $(PYTHON_APP_ELF) $(PYTHON_APP_BIN)

# Standalone app compilation (requires vendor builds)
# These compile each app to /bin independently
app-hello: $(HELLO_APP_BIN) | bin
	@echo "Copying hello to /bin..."
	@cp $(HELLO_APP_BIN) bin/hello
	@echo "✓ /bin/hello ready"

app-js: $(JS_APP_BIN) | bin
	@echo "Copying js to /bin..."
	@cp $(JS_APP_BIN) bin/js
	@echo "✓ /bin/js ready"

app-ruby:
	@if [ -f "$(RUBY_APP_BIN)" ]; then cp $(RUBY_APP_BIN) bin/ruby; echo "✓ /bin/ruby ready"; else echo "ℹ ruby.app not built. Requires mruby vendor. See BUILD_LANGS.md"; fi

app-python:
	@if [ -f "$(PYTHON_APP_BIN)" ]; then cp $(PYTHON_APP_BIN) bin/python; echo "✓ /bin/python ready"; else echo "ℹ python.app not built. Requires micropython vendor. See BUILD_LANGS.md"; fi

clean:
	rm -rf $(BUILD_DIR)

full: clean all

img: all

imb: $(IMAGE)
	@echo "Copiando imagem para build/vibe-os-usb.img"
	@cp $(IMAGE) build/vibe-os-usb.img
	@echo "Imagem para hardware real pronta: build/vibe-os-usb.img"

-include $(shell test -d $(BUILD_DIR) && find $(BUILD_DIR) -name '*.d' -print)
