# Build System for Ported GNU Applications
# Compile GNU apps with VibeOS compatibility layer
#
# Usage: make -f Build.ported.mk ported-echo
#        make -f Build.ported.mk ported-cat
#        etc

TOOLCHAIN_PREFIX ?= i686-elf-
HAS_CROSS_TOOLCHAIN := $(shell command -v $(TOOLCHAIN_PREFIX)gcc >/dev/null 2>&1 && command -v $(TOOLCHAIN_PREFIX)ld >/dev/null 2>&1 && command -v $(TOOLCHAIN_PREFIX)objcopy >/dev/null 2>&1 && command -v $(TOOLCHAIN_PREFIX)nm >/dev/null 2>&1 && echo 1 || echo 0)

CC_ORIGIN := $(origin CC)
LD_ORIGIN := $(origin LD)
OBJCOPY_ORIGIN := $(origin OBJCOPY)
NM_ORIGIN := $(origin NM)

ifeq ($(CC_ORIGIN),default)
CC :=
endif
ifeq ($(LD_ORIGIN),default)
LD :=
endif
ifeq ($(OBJCOPY_ORIGIN),default)
OBJCOPY :=
endif
ifeq ($(NM_ORIGIN),default)
NM :=
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

ifeq ($(strip $(OBJCOPY)),)
ifeq ($(HAS_CROSS_TOOLCHAIN),1)
OBJCOPY := $(TOOLCHAIN_PREFIX)objcopy
else
OBJCOPY := objcopy
endif
endif

ifeq ($(strip $(NM)),)
ifeq ($(HAS_CROSS_TOOLCHAIN),1)
NM := $(TOOLCHAIN_PREFIX)nm
else
NM := nm
endif
endif

PYTHON ?= python3

# Compiler flags - same as other apps
CFLAGS := -m32 -Os -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
	-fno-builtin -nostdlib -Wall -Wextra
INCLUDES := -I. -Icompat/include -Ilang/include -Iapplications/ported/include -Iheaders
LDFLAGS := -m elf_i386 -T linker/app.ld -nostdlib -N --allow-multiple-definition

# Ported app SDK
APP_ENTRY := lang/sdk/app_entry.c
APP_RUNTIME := lang/sdk/app_runtime.c

# Compat library (built via Build.compat.mk)
COMPAT_LIB := build/libcompat.a
include Build.compat.mk

# === ECHO APP ===

ECHO_SRCS := applications/ported/echo/echo.c
ECHO_OBJS := build/ported/echo.o \
	build/app_entry_echo.o \
	build/app_runtime_echo.o

ECHO_ELF := build/ported/echo.elf
ECHO_APP := build/ported/echo.app

build/app_entry_echo.o: $(APP_ENTRY) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) \
		-DVIBE_APP_BUILD_NAME=\"echo\" \
		-DVIBE_APP_BUILD_HEAP_SIZE=65536u \
		-c $< -o $@

build/app_runtime_echo.o: $(APP_RUNTIME) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/ported/echo.o: $(ECHO_SRCS) $(COMPAT_LIB) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(ECHO_ELF): $(ECHO_OBJS) $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(ECHO_OBJS) $(COMPAT_LIB) -o $@

$(ECHO_APP): $(ECHO_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ Echo app: $@"

ported-echo: $(ECHO_APP)

# === CAT APP ===

CAT_SRCS := applications/ported/cat/cat.c
CAT_OBJS := build/ported/cat.o \
	build/app_entry_cat.o \
	build/app_runtime_cat.o

CAT_ELF := build/ported/cat.elf
CAT_APP := build/ported/cat.app

build/app_entry_cat.o: $(APP_ENTRY) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) \
		-DVIBE_APP_BUILD_NAME=\"cat\" \
		-DVIBE_APP_BUILD_HEAP_SIZE=65536u \
		-c $< -o $@

build/app_runtime_cat.o: $(APP_RUNTIME) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/ported/cat.o: $(CAT_SRCS) $(COMPAT_LIB) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(CAT_ELF): $(CAT_OBJS) $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(CAT_OBJS) $(COMPAT_LIB) -o $@

$(CAT_APP): $(CAT_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ Cat app: $@"

ported-cat: $(CAT_APP)

# === WC APP ===

WC_SRCS := applications/ported/wc/wc.c
WC_OBJS := build/ported/wc.o \
	build/app_entry_wc.o \
	build/app_runtime_wc.o

WC_ELF := build/ported/wc.elf
WC_APP := build/ported/wc.app

build/app_entry_wc.o: $(APP_ENTRY) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) \
		-DVIBE_APP_BUILD_NAME=\"wc\" \
		-DVIBE_APP_BUILD_HEAP_SIZE=65536u \
		-c $< -o $@

build/app_runtime_wc.o: $(APP_RUNTIME) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/ported/wc.o: $(WC_SRCS) $(COMPAT_LIB) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(WC_ELF): $(WC_OBJS) $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(WC_OBJS) $(COMPAT_LIB) -o $@

$(WC_APP): $(WC_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ Wc app: $@"

ported-wc: $(WC_APP)

# === PWD APP ===

PWD_SRCS := applications/ported/pwd/pwd.c
PWD_OBJS := build/ported/pwd.o \
	build/app_entry_pwd.o \
	build/app_runtime_pwd.o

PWD_ELF := build/ported/pwd.elf
PWD_APP := build/ported/pwd.app

build/app_entry_pwd.o: $(APP_ENTRY) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) \
		-DVIBE_APP_BUILD_NAME=\"pwd\" \
		-DVIBE_APP_BUILD_HEAP_SIZE=65536u \
		-c $< -o $@

build/app_runtime_pwd.o: $(APP_RUNTIME) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/ported/pwd.o: $(PWD_SRCS) $(COMPAT_LIB) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(PWD_ELF): $(PWD_OBJS) $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(PWD_OBJS) $(COMPAT_LIB) -o $@

$(PWD_APP): $(PWD_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ Pwd app: $@"

ported-pwd: $(PWD_APP)

# === HEAD APP ===

HEAD_SRCS := applications/ported/head/head.c
HEAD_OBJS := build/ported/head.o \
	build/app_entry_head.o \
	build/app_runtime_head.o

HEAD_ELF := build/ported/head.elf
HEAD_APP := build/ported/head.app

build/app_entry_head.o: $(APP_ENTRY) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) \
		-DVIBE_APP_BUILD_NAME=\"head\" \
		-DVIBE_APP_BUILD_HEAP_SIZE=65536u \
		-c $< -o $@

build/app_runtime_head.o: $(APP_RUNTIME) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/ported/head.o: $(HEAD_SRCS) $(COMPAT_LIB) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(HEAD_ELF): $(HEAD_OBJS) $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(HEAD_OBJS) $(COMPAT_LIB) -o $@

$(HEAD_APP): $(HEAD_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ Head app: $@"

ported-head: $(HEAD_APP)

# === SLEEP APP ===

SLEEP_SRCS := applications/ported/sleep/sleep.c
SLEEP_OBJS := build/ported/sleep.o \
	build/app_entry_sleep.o \
	build/app_runtime_sleep.o

SLEEP_ELF := build/ported/sleep.elf
SLEEP_APP := build/ported/sleep.app

build/app_entry_sleep.o: $(APP_ENTRY) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) \
		-DVIBE_APP_BUILD_NAME=\"sleep\" \
		-DVIBE_APP_BUILD_HEAP_SIZE=65536u \
		-c $< -o $@

build/app_runtime_sleep.o: $(APP_RUNTIME) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/ported/sleep.o: $(SLEEP_SRCS) $(COMPAT_LIB) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(SLEEP_ELF): $(SLEEP_OBJS) $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(SLEEP_OBJS) $(COMPAT_LIB) -o $@

$(SLEEP_APP): $(SLEEP_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ Sleep app: $@"

ported-sleep: $(SLEEP_APP)

# === RMDIR APP ===

RMDIR_SRCS := applications/ported/rmdir/rmdir.c
RMDIR_OBJS := build/ported/rmdir.o \
	build/app_entry_rmdir.o \
	build/app_runtime_rmdir.o

RMDIR_ELF := build/ported/rmdir.elf
RMDIR_APP := build/ported/rmdir.app

build/app_entry_rmdir.o: $(APP_ENTRY) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) \
		-DVIBE_APP_BUILD_NAME=\"rmdir\" \
		-DVIBE_APP_BUILD_HEAP_SIZE=65536u \
		-c $< -o $@

build/app_runtime_rmdir.o: $(APP_RUNTIME) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/ported/rmdir.o: $(RMDIR_SRCS) $(COMPAT_LIB) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(RMDIR_ELF): $(RMDIR_OBJS) $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(RMDIR_OBJS) $(COMPAT_LIB) -o $@

$(RMDIR_APP): $(RMDIR_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ Rmdir app: $@"

ported-rmdir: $(RMDIR_APP)

# === TAIL APP ===

TAIL_SRCS := applications/ported/tail/tail.c
TAIL_OBJS := build/ported/tail.o \
	build/app_entry_tail.o \
	build/app_runtime_tail.o

TAIL_ELF := build/ported/tail.elf
TAIL_APP := build/ported/tail.app

build/app_entry_tail.o: $(APP_ENTRY) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) \
		-DVIBE_APP_BUILD_NAME=\"tail\" \
		-DVIBE_APP_BUILD_HEAP_SIZE=65536u \
		-c $< -o $@

build/app_runtime_tail.o: $(APP_RUNTIME) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/ported/tail.o: $(TAIL_SRCS) $(COMPAT_LIB) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(TAIL_ELF): $(TAIL_OBJS) $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(TAIL_OBJS) $(COMPAT_LIB) -o $@

$(TAIL_APP): $(TAIL_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ Tail app: $@"

ported-tail: $(TAIL_APP)

# === GREP APP ===

GREP_SRCS := applications/ported/grep/grep.c
GREP_OBJS := build/ported/grep.o \
	build/app_entry_grep.o \
	build/app_runtime_grep.o

GREP_ELF := build/ported/grep.elf
GREP_APP := build/ported/grep.app

build/app_entry_grep.o: $(APP_ENTRY) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) \
		-DVIBE_APP_BUILD_NAME=\"grep\" \
		-DVIBE_APP_BUILD_HEAP_SIZE=65536u \
		-c $< -o $@

build/app_runtime_grep.o: $(APP_RUNTIME) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/ported/grep.o: $(GREP_SRCS) $(COMPAT_LIB) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(GREP_ELF): $(GREP_OBJS) $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(GREP_OBJS) $(COMPAT_LIB) -o $@

$(GREP_APP): $(GREP_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ Grep app: $@"

ported-grep: $(GREP_APP)

# === LOADKEYS APP ===

LOADKEYS_SRCS := applications/ported/loadkeys/loadkeys.c
LOADKEYS_OBJS := build/ported/loadkeys.o \
	build/app_entry_loadkeys.o \
	build/app_runtime_loadkeys.o

LOADKEYS_ELF := build/ported/loadkeys.elf
LOADKEYS_APP := build/ported/loadkeys.app

build/app_entry_loadkeys.o: $(APP_ENTRY) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) \
		-DVIBE_APP_BUILD_NAME=\"loadkeys\" \
		-DVIBE_APP_BUILD_HEAP_SIZE=65536u \
		-c $< -o $@

build/app_runtime_loadkeys.o: $(APP_RUNTIME) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/ported/loadkeys.o: $(LOADKEYS_SRCS) $(COMPAT_LIB) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(LOADKEYS_ELF): $(LOADKEYS_OBJS) $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(LOADKEYS_OBJS) $(COMPAT_LIB) -o $@

$(LOADKEYS_APP): $(LOADKEYS_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ Loadkeys app: $@"

ported-loadkeys: $(LOADKEYS_APP)

# === TRUE APP ===

TRUE_SRCS := applications/ported/true/true.c
TRUE_OBJS := build/ported/true.o \
	build/app_entry_true.o \
	build/app_runtime_true.o

TRUE_ELF := build/ported/true.elf
TRUE_APP := build/ported/true.app

build/app_entry_true.o: $(APP_ENTRY) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) \
		-DVIBE_APP_BUILD_NAME=\"true\" \
		-DVIBE_APP_BUILD_HEAP_SIZE=65536u \
		-c $< -o $@

build/app_runtime_true.o: $(APP_RUNTIME) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/ported/true.o: $(TRUE_SRCS) $(COMPAT_LIB) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(TRUE_ELF): $(TRUE_OBJS) $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(TRUE_OBJS) $(COMPAT_LIB) -o $@

$(TRUE_APP): $(TRUE_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ True app: $@"

ported-true: $(TRUE_APP)

# === FALSE APP ===

FALSE_SRCS := applications/ported/false/false.c
FALSE_OBJS := build/ported/false.o \
	build/app_entry_false.o \
	build/app_runtime_false.o

FALSE_ELF := build/ported/false.elf
FALSE_APP := build/ported/false.app

build/app_entry_false.o: $(APP_ENTRY) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) \
		-DVIBE_APP_BUILD_NAME=\"false\" \
		-DVIBE_APP_BUILD_HEAP_SIZE=65536u \
		-c $< -o $@

build/app_runtime_false.o: $(APP_RUNTIME) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/ported/false.o: $(FALSE_SRCS) $(COMPAT_LIB) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(FALSE_ELF): $(FALSE_OBJS) $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(FALSE_OBJS) $(COMPAT_LIB) -o $@

$(FALSE_APP): $(FALSE_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ False app: $@"

ported-false: $(FALSE_APP)

# === PRINTF APP ===

PRINTF_SRCS := applications/ported/printf/printf.c
PRINTF_OBJS := build/ported/printf.o \
	build/app_entry_printf.o \
	build/app_runtime_printf.o

PRINTF_ELF := build/ported/printf.elf
PRINTF_APP := build/ported/printf.app

build/app_entry_printf.o: $(APP_ENTRY) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) \
		-DVIBE_APP_BUILD_NAME=\"printf\" \
		-DVIBE_APP_BUILD_HEAP_SIZE=65536u \
		-c $< -o $@

build/app_runtime_printf.o: $(APP_RUNTIME) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/ported/printf.o: $(PRINTF_SRCS) $(COMPAT_LIB) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(PRINTF_ELF): $(PRINTF_OBJS) $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(PRINTF_OBJS) $(COMPAT_LIB) -o $@

$(PRINTF_APP): $(PRINTF_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ Printf app: $@"

ported-printf: $(PRINTF_APP)

# === SED APP ===

SED_SRCS := $(wildcard applications/ported/sed/*.c)
SED_OBJS := $(patsubst applications/ported/sed/%.c,build/ported/sed/%.o,$(SED_SRCS)) \
	build/app_entry_sed.o \
	build/app_runtime_sed.o

SED_ELF := build/ported/sed.elf
SED_APP := build/ported/sed.app

build/app_entry_sed.o: $(APP_ENTRY) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) \
		-DVIBE_APP_BUILD_NAME=\"sed\" \
		-DVIBE_APP_BUILD_HEAP_SIZE=262144u \
		-c $< -o $@

build/app_runtime_sed.o: $(APP_RUNTIME) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/ported/sed/%.o: applications/ported/sed/%.c $(COMPAT_LIB) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(SED_ELF): $(SED_OBJS) $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(SED_OBJS) $(COMPAT_LIB) -o $@

$(SED_APP): $(SED_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ Sed app: $@"

ported-sed: $(SED_APP)

# General rules
build:
	@mkdir -p $@

ported-echo-clean:
	rm -f $(ECHO_OBJS) $(ECHO_ELF) $(ECHO_APP)

ported-cat-clean:
	rm -f $(CAT_OBJS) $(CAT_ELF) $(CAT_APP)

ported-wc-clean:
	rm -f $(WC_OBJS) $(WC_ELF) $(WC_APP)

ported-pwd-clean:
	rm -f $(PWD_OBJS) $(PWD_ELF) $(PWD_APP)

ported-head-clean:
	rm -f $(HEAD_OBJS) $(HEAD_ELF) $(HEAD_APP)

ported-sleep-clean:
	rm -f $(SLEEP_OBJS) $(SLEEP_ELF) $(SLEEP_APP)

ported-rmdir-clean:
	rm -f $(RMDIR_OBJS) $(RMDIR_ELF) $(RMDIR_APP)

ported-tail-clean:
	rm -f $(TAIL_OBJS) $(TAIL_ELF) $(TAIL_APP)

ported-grep-clean:
	rm -f $(GREP_OBJS) $(GREP_ELF) $(GREP_APP)

ported-sed-clean:
	rm -f $(SED_OBJS) $(SED_ELF) $(SED_APP)

ported-loadkeys-clean:
	rm -f $(LOADKEYS_OBJS) $(LOADKEYS_ELF) $(LOADKEYS_APP)

ported-true-clean:
	rm -f $(TRUE_OBJS) $(TRUE_ELF) $(TRUE_APP)

ported-false-clean:
	rm -f $(FALSE_OBJS) $(FALSE_ELF) $(FALSE_APP)

ported-printf-clean:
	rm -f $(PRINTF_OBJS) $(PRINTF_ELF) $(PRINTF_APP)

ported-clean: ported-echo-clean ported-cat-clean ported-wc-clean ported-pwd-clean ported-head-clean ported-sleep-clean ported-rmdir-clean ported-tail-clean ported-grep-clean ported-sed-clean ported-loadkeys-clean ported-true-clean ported-false-clean ported-printf-clean

.PHONY: ported-echo ported-cat ported-wc ported-pwd ported-head ported-sleep ported-rmdir ported-tail ported-grep ported-sed ported-loadkeys ported-true ported-false ported-printf ported-clean ported-echo-clean ported-cat-clean ported-wc-clean ported-pwd-clean ported-head-clean ported-sleep-clean ported-rmdir-clean ported-tail-clean ported-grep-clean ported-sed-clean ported-loadkeys-clean ported-true-clean ported-false-clean ported-printf-clean
