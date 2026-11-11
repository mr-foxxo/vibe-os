# Build System for Links2 Web Browser on VibeOS
# Uses VibeOS compatibility layer and graphics driver

TOOLCHAIN_PREFIX ?= i686-elf-
HAS_CROSS_TOOLCHAIN := $(shell command -v $(TOOLCHAIN_PREFIX)gcc >/dev/null 2>&1 && echo 1 || echo 0)

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
CPU_ARCH_CFLAGS := -march=i586 -mtune=generic -mno-mmx -mno-sse -mno-sse2
CFLAGS := -m32 $(CPU_ARCH_CFLAGS) -Os -ffreestanding -fno-pic -fno-pie \
	-fno-stack-protector -fno-builtin -nostdlib -nostdinc -Wall -Wextra \
	-Wno-pointer-sign -Wno-unused-parameter -Wno-unused-variable
CFLAGS += -DHAVE_CONFIG_H -DG -DNO_IE
CFLAGS += -Iuserland/applications/network/links2/compat
CFLAGS += -Iuserland/applications/network/links2
CFLAGS += -Iheaders -Ilang/include
CFLAGS += -I$(shell $(CC) -m32 -print-file-name=include 2>/dev/null)

LDFLAGS := -m elf_i386 -T linker/app.ld -nostdlib -N --allow-multiple-definition

UNAME_S := $(shell uname -s 2>/dev/null || echo Unknown)
LIBGCC_A := $(shell $(CC) -m32 $(CPU_ARCH_CFLAGS) -print-libgcc-file-name 2>/dev/null)

APP_ENTRY := lang/sdk/app_entry.c
APP_RUNTIME := lang/sdk/app_runtime.c
COMPAT_LIB := build/libcompat.a

LINKS2_DIR := userland/applications/network/links2

# Links2 core sources (excluding platform-specific drivers)
LINKS2_CORE_SRCS := \
	$(LINKS2_DIR)/main.c \
	$(LINKS2_DIR)/os_dep.c \
	$(LINKS2_DIR)/memory.c \
	$(LINKS2_DIR)/error.c \
	$(LINKS2_DIR)/string.c \
	$(LINKS2_DIR)/select.c \
	$(LINKS2_DIR)/kbd.c \
	$(LINKS2_DIR)/terminal.c \
	$(LINKS2_DIR)/drivers.c \
	$(LINKS2_DIR)/session.c \
	$(LINKS2_DIR)/bfu.c \
	$(LINKS2_DIR)/menu.c \
	$(LINKS2_DIR)/default.c \
	$(LINKS2_DIR)/html.c \
	$(LINKS2_DIR)/html_r.c \
	$(LINKS2_DIR)/html_gr.c \
	$(LINKS2_DIR)/html_tbl.c \
	$(LINKS2_DIR)/http.c \
	$(LINKS2_DIR)/https.c \
	$(LINKS2_DIR)/sched.c \
	$(LINKS2_DIR)/connect.c \
	$(LINKS2_DIR)/url.c \
	$(LINKS2_DIR)/dns.c \
	$(LINKS2_DIR)/doh.c \
	$(LINKS2_DIR)/cache.c \
	$(LINKS2_DIR)/cookies.c \
	$(LINKS2_DIR)/auth.c \
	$(LINKS2_DIR)/suffix.c \
	$(LINKS2_DIR)/file.c \
	$(LINKS2_DIR)/data.c \
	$(LINKS2_DIR)/finger.c \
	$(LINKS2_DIR)/ftp.c \
	$(LINKS2_DIR)/smb.c \
	$(LINKS2_DIR)/mailto.c \
	$(LINKS2_DIR)/dip.c \
	$(LINKS2_DIR)/dither.c \
	$(LINKS2_DIR)/lru.c \
	$(LINKS2_DIR)/view.c \
	$(LINKS2_DIR)/view_gr.c \
	$(LINKS2_DIR)/font_inc.c \
	$(LINKS2_DIR)/gif.c \
	$(LINKS2_DIR)/xbm.c \
	$(LINKS2_DIR)/img.c \
	$(LINKS2_DIR)/imgcache.c \
	$(LINKS2_DIR)/types.c \
	$(LINKS2_DIR)/listedit.c \
	$(LINKS2_DIR)/bookmark.c \
	$(LINKS2_DIR)/block.c \
	$(LINKS2_DIR)/compress.c \
	$(LINKS2_DIR)/charsets.c \
	$(LINKS2_DIR)/language.c \
	$(LINKS2_DIR)/objreq.c \
	$(LINKS2_DIR)/fn_impl.c \
	$(LINKS2_DIR)/af_unix.c \
	$(LINKS2_DIR)/vibeos.c

LINKS2_OBJS := $(patsubst userland/applications/network/links2/%.c,build/links2/%.o,$(LINKS2_CORE_SRCS))

LINKS2_ELF := build/links2/links2.elf
LINKS2_APP := build/links2/links2.app

LINKS2_HEAP_SIZE ?= 4194304

build:
	@mkdir -p $@

build/links2:
	@mkdir -p $@

build/app_entry_links2.o: $(APP_ENTRY) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) \
		-DVIBE_APP_BUILD_NAME=\"links2\" \
		-DVIBE_APP_BUILD_HEAP_SIZE=$(LINKS2_HEAP_SIZE)u \
		-c $< -o $@

build/app_runtime_links2.o: $(APP_RUNTIME) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/links2/%.o: $(LINKS2_DIR)/%.c $(COMPAT_LIB) | build/links2
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(LINKS2_ELF): $(LINKS2_OBJS) build/app_entry_links2.o build/app_runtime_links2.o $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) build/app_entry_links2.o build/app_runtime_links2.o $(LINKS2_OBJS) $(COMPAT_LIB) -o $@ $(LIBGCC_A)

$(LINKS2_APP): $(LINKS2_ELF)
	@mkdir -p $(dir $@)
	cp $< $<.keep
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $<.keep --bin $@
	rm -f $<.keep
	@echo "Links2 app: $@"

ported-links2: $(LINKS2_APP)

ported-links2-clean:
	rm -f $(LINKS2_OBJS) $(LINKS2_ELF) $(LINKS2_APP)
	rm -rf build/links2 build/app_entry_links2.o build/app_runtime_links2.o

.PHONY: ported-links2 ported-links2-clean
