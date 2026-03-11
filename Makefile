SHELL := /bin/sh

AS := nasm
CC := i686-elf-gcc
LD := i686-elf-ld
OBJCOPY := i686-elf-objcopy
QEMU := qemu-system-i386

BUILD_DIR := build
BOOT_DIR := boot
STAGE2_DIR := stage2
USERLAND_DIR := userland
LINKER_DIR := linker
INCLUDE_DIR := include

# keep a generous sector allocation for growing kernel modules
STAGE2_SECTORS := 40
SECTOR_SIZE := 512

BOOT_BIN := $(BUILD_DIR)/boot.bin
STAGE2_SRCS := $(shell find $(STAGE2_DIR) -maxdepth 1 -name '*.c')
# gather all C files in kernel directory recursively
KERNEL_SRCS := $(shell find kernel -name '*.c')

STAGE2_OBJS := $(patsubst $(STAGE2_DIR)/%.c,$(BUILD_DIR)/stage2_%.o,$(STAGE2_SRCS))
KERNEL_OBJS := $(patsubst kernel/%.c,$(BUILD_DIR)/kernel_%.o,$(KERNEL_SRCS))
# legacy stage2 interrupt stubs are now provided by kernel_asm/isr.asm
# keep the file for reference but do not compile it into the final image
STAGE2_ISR_OBJ :=
KERNEL_ASM_SRCS := $(shell find kernel_asm -name '*.asm')
KERNEL_ASM_OBJS := $(patsubst kernel_asm/%.asm,$(BUILD_DIR)/kernel_asm_%.o,$(KERNEL_ASM_SRCS))
USERLAND_SRCS := $(shell find $(USERLAND_DIR) -name '*.c')
USERLAND_OBJS := $(patsubst $(USERLAND_DIR)/%.c,$(BUILD_DIR)/%.o,$(USERLAND_SRCS))
USERLAND_ELF := $(BUILD_DIR)/userland.elf
USERLAND_BIN := $(BUILD_DIR)/userland.bin
USERLAND_BLOB_OBJ := $(BUILD_DIR)/userland_blob.o
STAGE2_ELF := $(BUILD_DIR)/stage2.elf
STAGE2_BIN := $(BUILD_DIR)/stage2.bin
IMAGE := $(BUILD_DIR)/boot.img

CFLAGS := -m32 -Os -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-builtin -nostdlib -Wall -Wextra -Werror -Iuserland -Iuserland/include -Iuserland/modules/include -Iuserland/applications/include -I$(INCLUDE_DIR) -I$(STAGE2_DIR)/include -Ikernel/include
LDFLAGS_STAGE2 := -m elf_i386 -T $(LINKER_DIR)/stage2.ld -nostdlib
LDFLAGS_USERLAND := -m elf_i386 -T $(LINKER_DIR)/userland.ld -nostdlib

all: $(IMAGE)

check-tools:
	@for tool in $(REQUIRED_TOOLS); do \
		if ! command -v $$tool >/dev/null 2>&1; then \
			echo "Erro: '$$tool' nao encontrado no PATH."; \
			echo "macOS (Homebrew): brew install nasm i686-elf-gcc qemu"; \
			exit 1; \
		fi; \
	done

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BOOT_BIN): $(BOOT_DIR)/stage1.asm | $(BUILD_DIR)
	$(AS) -f bin -DSTAGE2_SECTORS=$(STAGE2_SECTORS) $< -o $@
	@boot_size=$$(wc -c < $@); \
	if [ "$$boot_size" -ne 512 ]; then \
		echo "Erro: boot sector precisa ter 512 bytes (atual: $$boot_size)."; \
		exit 1; \
	fi

$(STAGE2_OBJS): $(BUILD_DIR)/stage2_%.o: $(STAGE2_DIR)/%.c $(INCLUDE_DIR)/userland_api.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_OBJS): $(BUILD_DIR)/kernel_%.o: kernel/%.c $(INCLUDE_DIR)/userland_api.h | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# no longer building stage2/isr.asm; kernel_asm/isr.asm supplies IRQ/exn stubs
#$(STAGE2_ISR_OBJ): $(STAGE2_DIR)/isr.asm | $(BUILD_DIR)
#	$(AS) -f elf32 $< -o $@

$(KERNEL_ASM_OBJS): $(BUILD_DIR)/kernel_asm_%.o: kernel_asm/%.asm | $(BUILD_DIR)
	$(AS) -f elf32 $< -o $@

$(USERLAND_OBJS): $(INCLUDE_DIR)/userland_api.h | $(BUILD_DIR)
	mkdir -p $(dir $@)
	# compile any C source under userland directory matching the object path
	$(CC) $(CFLAGS) -c $(USERLAND_DIR)/$(patsubst $(BUILD_DIR)/%,%,$(subst .o,.c,$@)) -o $@

$(USERLAND_ELF): $(USERLAND_OBJS) $(LINKER_DIR)/userland.ld
	$(LD) $(LDFLAGS_USERLAND) $(USERLAND_OBJS) -o $@

$(USERLAND_BIN): $(USERLAND_ELF)
	$(OBJCOPY) -O binary $< $@

$(USERLAND_BLOB_OBJ): $(USERLAND_BIN)
	cd $(BUILD_DIR) && $(LD) -m elf_i386 -r -b binary userland.bin -o userland_blob.o

$(STAGE2_ELF): $(STAGE2_OBJS) $(KERNEL_OBJS) $(KERNEL_ASM_OBJS) $(USERLAND_BLOB_OBJ) $(LINKER_DIR)/stage2.ld
	$(LD) $(LDFLAGS_STAGE2) $(STAGE2_OBJS) $(KERNEL_OBJS) $(KERNEL_ASM_OBJS) $(USERLAND_BLOB_OBJ) -o $@

$(STAGE2_BIN): $(STAGE2_ELF)
	$(OBJCOPY) -O binary $< $@
	@stage2_size=$$(wc -c < $@); \
	limit=$$(( $(STAGE2_SECTORS) * $(SECTOR_SIZE) )); \
	if [ "$$stage2_size" -gt "$$limit" ]; then \
		echo "Erro: stage2.bin ($$stage2_size bytes) excede $$limit bytes."; \
		exit 1; \
	fi

$(IMAGE): $(BOOT_BIN) $(STAGE2_BIN) | check-tools
	cp $(BOOT_BIN) $@
	cat $(STAGE2_BIN) >> $@
	@target_size=$$(( (1 + $(STAGE2_SECTORS)) * $(SECTOR_SIZE) )); \
	current_size=$$(wc -c < $@); \
	if [ "$$current_size" -lt "$$target_size" ]; then \
		padding=$$(( $$target_size - $$current_size )); \
		dd if=/dev/zero bs=1 count=$$padding >> $@ 2>/dev/null; \
	fi
	@echo "Imagem gerada: $(IMAGE)"

run: $(IMAGE)
	$(QEMU) -drive format=raw,file=$(IMAGE),if=floppy -boot a

debug: $(IMAGE)
	$(QEMU) -drive format=raw,file=$(IMAGE),if=floppy -boot a -s -S

clean:
	rm -rf $(BUILD_DIR)
