SHELL := /bin/sh

AS := nasm
CC := i686-elf-gcc
LD := i686-elf-ld
OBJCOPY := i686-elf-objcopy
QEMU := qemu-system-i386

BUILD_DIR := build
BOOT_DIR := boot
USERLAND_DIR := userland
LINKER_DIR := linker

# Kernel sources - kernel only, no stage2
KERNEL_SRCS := $(shell find kernel -name '*.c')
KERNEL_OBJS := $(patsubst kernel/%.c,$(BUILD_DIR)/kernel_%.o,$(KERNEL_SRCS))

KERNEL_ASM_SRCS := $(shell find kernel_asm -name '*.asm')
KERNEL_ASM_OBJS := $(patsubst kernel_asm/%.asm,$(BUILD_DIR)/kernel_asm_%.o,$(KERNEL_ASM_SRCS))

USERLAND_SRCS := $(shell find $(USERLAND_DIR) -name '*.c')
USERLAND_OBJS := $(patsubst $(USERLAND_DIR)/%.c,$(BUILD_DIR)/%.o,$(USERLAND_SRCS))

BOOT_BIN := $(BUILD_DIR)/boot.bin
KERNEL_ELF := $(BUILD_DIR)/kernel.elf
KERNEL_BIN := $(BUILD_DIR)/kernel.bin
IMAGE := $(BUILD_DIR)/boot.img

CFLAGS := -m32 -Os -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-builtin -nostdlib -Wall -Wextra -Werror -Iheaders -Iuserland
LDFLAGS_KERNEL := -m elf_i386 -T $(LINKER_DIR)/kernel.ld -nostdlib -N
LDFLAGS_USERLAND := -m elf_i386 -T $(LINKER_DIR)/userland.ld -nostdlib -N

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
	$(AS) -f bin $< -o $@
	@boot_size=$$(wc -c < $@); \
	if [ "$$boot_size" -ne 512 ]; then \
		echo "Erro: boot sector precisa ter 512 bytes (atual: $$boot_size)."; \
		exit 1; \
	fi

$(BUILD_DIR)/kernel_%.o: kernel/%.c headers/include/userland_api.h | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel_shell_%.o: userland/modules/%.c headers/include/userland_api.h | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel_asm_%.o: kernel_asm/%.asm | $(BUILD_DIR)
	$(AS) -f elf32 $< -o $@

$(BUILD_DIR)/%.o: $(USERLAND_DIR)/%.c headers/include/userland_api.h | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_ELF): $(KERNEL_OBJS) $(KERNEL_ASM_OBJS) $(USERLAND_OBJS) $(LINKER_DIR)/kernel.ld
	$(LD) $(LDFLAGS_KERNEL) $(KERNEL_OBJS) $(KERNEL_ASM_OBJS) $(USERLAND_OBJS) -o $@

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

$(IMAGE): $(BOOT_BIN) $(KERNEL_BIN)
	cp $(BOOT_BIN) $@
	cat $(KERNEL_BIN) >> $@
	@echo "Imagem gerada: $(IMAGE)"

run: $(IMAGE)
	@if command -v $(QEMU) >/dev/null 2>&1; then \
		$(QEMU) -drive format=raw,file=$(IMAGE),if=floppy -boot a; \
	else \
		echo "Aviso: $(QEMU) não encontrado. Tentando qemu-system-x86_64..."; \
		if command -v qemu-system-x86_64 >/dev/null 2>&1; then \
			echo "Usando qemu-system-x86_64"; \
			qemu-system-x86_64 -drive format=raw,file=$(IMAGE),if=floppy -boot a; \
		else \
			echo "Erro: QEMU não encontrado no sistema."; \
			echo "macOS (Homebrew): brew install qemu"; \
			exit 1; \
		fi; \
	fi

run-debug: $(IMAGE)
	@if command -v $(QEMU) >/dev/null 2>&1; then \
		$(QEMU) -drive format=raw,file=$(IMAGE),if=floppy -boot a -serial stdio; \
	else \
		if command -v qemu-system-x86_64 >/dev/null 2>&1; then \
			qemu-system-x86_64 -drive format=raw,file=$(IMAGE),if=floppy -boot a -serial stdio; \
		else \
			echo "Erro: QEMU não encontrado"; \
			exit 1; \
		fi; \
	fi

debug: $(IMAGE)
	@if command -v $(QEMU) >/dev/null 2>&1; then \
		$(QEMU) -drive format=raw,file=$(IMAGE),if=floppy -boot a -s -S; \
	else \
		echo "Aviso: $(QEMU) não encontrado. Tentando qemu-system-x86_64..."; \
		if command -v qemu-system-x86_64 >/dev/null 2>&1; then \
			echo "Usando qemu-system-x86_64 com debug"; \
			qemu-system-x86_64 -drive format=raw,file=$(IMAGE),if=floppy -boot a -s -S; \
		else \
			echo "Erro: QEMU não encontrado no sistema."; \
			echo "macOS (Homebrew): brew install qemu"; \
			exit 1; \
		fi; \
	fi

clean:
	rm -rf $(BUILD_DIR)
