# Build for VibeOS Compatibility Layer
# 
# Compiles libc/POSIX compatibility layer used by ported GNU apps

TOOLCHAIN_PREFIX ?= i686-elf-
HAS_CROSS_TOOLCHAIN := $(shell command -v $(TOOLCHAIN_PREFIX)gcc >/dev/null 2>&1 && command -v $(TOOLCHAIN_PREFIX)ar >/dev/null 2>&1 && command -v $(TOOLCHAIN_PREFIX)ranlib >/dev/null 2>&1 && echo 1 || echo 0)

CC_ORIGIN := $(origin CC)
AR_ORIGIN := $(origin AR)
RANLIB_ORIGIN := $(origin RANLIB)

ifeq ($(CC_ORIGIN),default)
CC :=
endif
ifeq ($(AR_ORIGIN),default)
AR :=
endif
ifeq ($(RANLIB_ORIGIN),default)
RANLIB :=
endif

ifeq ($(strip $(CC)),)
ifeq ($(HAS_CROSS_TOOLCHAIN),1)
CC := $(TOOLCHAIN_PREFIX)gcc
else
CC := gcc
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

COMPAT_SRCS := \
	compat/src/libc/string.c \
	compat/src/libc/stdlib.c \
	compat/src/libc/ctype.c \
	compat/src/libc/stdio.c \
	compat/src/libc/assert.c \
	compat/src/posix/errno.c \
	compat/src/posix/unistd.c

COMPAT_OBJS := $(COMPAT_SRCS:%.c=build/%.o)
COMPAT_LIB := build/libcompat.a

# Compilation flags (same as kernel/apps)
COMPAT_CFLAGS := -m32 -Os -ffreestanding -fno-pic -fno-pie \
	-fno-stack-protector -fno-builtin -nostdlib \
	-Wall -Wextra -Werror \
	-I. -Icompat/include -Iheaders -Ilang/include

# Pattern rule for compat objects
build/compat/%.o: compat/%.c | build
	@mkdir -p $(dir $@)
	$(CC) $(COMPAT_CFLAGS) -c $< -o $@

# Archive compat library
$(COMPAT_LIB): $(COMPAT_OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $(COMPAT_OBJS)
	$(RANLIB) $@
	@echo "✓ compat library: $@"
	@ls -lh $@

compat-build: $(COMPAT_LIB)
	@echo "✓ compat layer built"

compat-clean:
	rm -rf build/compat $(COMPAT_LIB)

.PHONY: compat-build compat-clean
