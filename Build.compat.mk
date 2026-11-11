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
	compat/lib/libc/strerror.c \
	compat/lib/libc/strutils.c \
	compat/src/libc/string.c \
	compat/src/libc/stdlib.c \
	compat/src/libc/ctype.c \
	compat/src/libc/stdio.c \
	compat/src/libc/assert.c \
	compat/src/posix/errno.c \
	compat/src/posix/fd_state.c \
	compat/src/posix/unistd.c \
	compat/src/posix/tty.c \
	compat/src/posix/utsname.c \
	compat/src/posix/time.c \
	compat/src/posix/signal.c \
	compat/src/posix/socket.c \
	compat/src/posix/select.c \
	compat/src/posix/sched.c \
	compat/src/posix/pthread.c \
	compat/src/posix/process.c \
	compat/src/posix/mman.c \
	compat/src/posix/ipc.c \
	compat/src/posix/dirent.c \
	compat/src/posix/semaphore.c \
	compat/src/posix/spawn.c \
	compat/src/posix/syslog.c \
	compat/src/posix/wordexp.c \
	compat/src/posix/monetary.c \
	compat/src/posix/aio.c \
	compat/src/posix/utmpx.c \
	compat/src/posix/ulimit.c \
	compat/src/posix/mqueue.c \
	compat/src/posix/wchar.c \
	compat/src/posix/iconv.c \
	compat/src/posix/sysv_ipc.c \
	compat/src/posix/fmtmsg.c \
	compat/src/posix/arpa_inet.c \
	compat/src/posix/dlfcn.c \
	compat/src/posix/fnmatch.c \
	compat/src/posix/ftw.c \
	compat/src/posix/glob.c \
	compat/src/posix/grp.c \
	compat/src/posix/langinfo.c \
	compat/src/posix/libgen.c \
	compat/src/posix/ndbm.c \
	compat/src/posix/nl_types.c \
	compat/src/posix/pwd.c \
	compat/src/posix/search.c \
	compat/src/posix/setjmp.c \
	compat/src/posix/strings.c \
	compat/src/posix/utime.c \
	compat/src/posix/wctype.c \
	compat/src/libc/math.c \
	compat/src/x11/xlib.c \
	compat/lib/libc/regex/regcomp.c \
	compat/lib/libc/regex/regexec.c \
	compat/lib/libc/regex/regfree.c \
	compat/lib/libc/regex/regerror.c

COMPAT_OBJS := $(COMPAT_SRCS:%.c=build/%.o)
COMPAT_LIB := build/libksh.a build/libcompat.a
CPU_ARCH_CFLAGS := -march=i586 -mtune=generic -mno-mmx -mno-sse -mno-sse2

# Compilation flags (same as kernel/apps)
COMPAT_CFLAGS := -m32 $(CPU_ARCH_CFLAGS) -Os -ffreestanding -fno-pic -fno-pie \
	-fno-stack-protector -fno-builtin -nostdlib \
	-Wall -Wextra -Werror \
	-I. -Icompat/include -Icompat/lib/libc/regex -Iheaders -Ilang/include \
	-D'DEF_WEAK(x)='
COMPAT_REGEX_CFLAGS := $(COMPAT_CFLAGS) \
	-Icompat/sys \
	-D__BSD_VISIBLE=1 -D__POSIX_VISIBLE=200809L -D__XPG_VISIBLE=700 -D__ISO_C_VISIBLE=2011 \
	-D__mode_t=mode_t \
	-Wno-error=sign-compare \
	-Wno-error=maybe-uninitialized \
	-Wno-error=unused-but-set-variable

# Pattern rule for compat objects
build/compat/lib/libc/regex/%.o: compat/lib/libc/regex/%.c | build
	@mkdir -p $(dir $@)
	$(CC) $(COMPAT_REGEX_CFLAGS) -c $< -o $@

build/compat/%.o: compat/%.c | build
	@mkdir -p $(dir $@)
	$(CC) $(COMPAT_CFLAGS) -c $< -o $@

# Archive compat library. NOTE: $(COMPAT_LIB) lists build/libksh.a too, but
# libksh.a has its own rule in compat/bin/ksh/Build.ksh.mk (own objects/flags).
# A rule here keyed on $(COMPAT_LIB) would archive the wrong objects into
# libksh.a AND conflict with Build.ksh.mk (make "overriding commands" warning).
build/libcompat.a: $(COMPAT_OBJS)
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
