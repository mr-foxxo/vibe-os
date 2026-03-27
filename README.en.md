# VIBE OS - A simple x86 operating system built with AI, vibe coding, many fox paws, and human hands

## Warning

This repository is an experimental x86 BIOS operating system project built with strong AI assistance. A lot of it is functional, but several areas are still incomplete, inconsistent, or in architectural transition.

Read this project as:

- a bootloader + kernel + modular runtime experiment
- a base for study, debugging, and refactoring
- code that does not yet represent a "finished" operating system

## Summary

The current state of the project is this:

- legacy **BIOS** boot in multiple stages
- **MBR -> VBR -> stage2 -> `KERNEL.BIN`**
- **FAT32** boot partition
- second **raw** partition for **AppFS**, persistence, and assets
- 32-bit kernel with:
  - CPU, GDT, IDT, PIC, and PIT initialization
  - video, keyboard, mouse, and storage drivers
  - simple scheduler
  - microkernel-style service layer
  - syscall table
- userland bootstrap through the built-in `init` service
- external app loading through **AppFS**
- modular shell and graphical path with `startx` and the desktop

In other words: the system is no longer a single "kernel + embedded userland" blob in the old README sense. The real flow today is:

`BIOS -> MBR -> stage1/VBR -> stage2 -> KERNEL.BIN -> kernel -> init -> AppFS -> userland.app/startx -> shell and apps`

## Detailed documentation note

The detailed files under `docs/` are currently **only available in English**.

If you want the most code-faithful technical view of the current system, start with:

- [docs/overview.md](docs/overview.md)
- [docs/workflow.md](docs/workflow.md)
- [docs/mbr.md](docs/mbr.md)
- [docs/stage1.md](docs/stage1.md)
- [docs/stage2.md](docs/stage2.md)
- [docs/memory_map.md](docs/memory_map.md)
- [docs/kernel_init.md](docs/kernel_init.md)
- [docs/drivers.md](docs/drivers.md)
- [docs/runtime_and_services.md](docs/runtime_and_services.md)
- [docs/apps_and_modules.md](docs/apps_and_modules.md)

## Quick index

- [Current architecture](#current-architecture)
- [Directory map](#directory-map)
- [Build](#build)
- [Running in QEMU](#running-in-qemu)
- [Generated artifacts](#generated-artifacts)
- [Documentation](#documentation)
- [Current project status](#current-project-status)
- [Project future](#project-future)
- [Portuguese version](README.md)

## Current architecture

### Boot

- [`boot/mbr.asm`](boot/mbr.asm)
  - MBR relocation
  - `BOOTINFO` initialization
  - active partition discovery
  - VBR loading
- [`boot/stage1.asm`](boot/stage1.asm)
  - stage 2 loading from FAT32 reserved sectors
  - persistent low-memory boot trace
- [`boot/stage2.asm`](boot/stage2.asm)
  - minimal FAT32 parsing
  - loading of `VIBEBG.BIN` and `KERNEL.BIN`
  - VESA enumeration
  - memory detection
  - A20 activation
  - boot menu
  - return to real mode for BIOS usage and final kernel handoff

### Kernel

The current kernel enters through [`kernel/entry.c`](kernel/entry.c) and performs:

- basic CPU/GDT/IDT/PIC/PIT bring-up
- video and text console initialization
- PS/2 keyboard and PS/2 mouse initialization
- native storage discovery:
  - AHCI first
  - ATA second
- memory, heap, and external app arena initialization
- scheduler startup
- service registration and bootstrap
- syscall setup
- built-in `init` launch

### Runtime and apps

The current app execution path does not depend on a complete kernel VFS.

The flow is:

- the kernel starts the built-in `init` service
- `init` prepares the userland console and filesystem
- `init` tries to launch `userland.app` through AppFS
- `userland.app` starts the shell
- in normal desktop boot mode, `userland.app` auto-runs `startx`
- `startx` and the desktop launch modular graphical apps

### Runtime storage

Today there are two storage "worlds":

1. FAT32 boot partition
   - used by the BIOS loader
   - contains `KERNEL.BIN`, `STAGE2.BIN`, manifests, and boot assets
2. raw data partition
   - AppFS
   - persistence area
   - raw assets such as wallpaper and game data

## Directory map

```text
.
├── boot/                # MBR, VBR/stage1, and stage2 assembly
├── kernel/              # kernel C
├── kernel_asm/          # kernel assembly
├── headers/             # centralized headers
├── userland/            # bootstrap, modules, and native apps
├── lang/                # runtime/AppFS SDK and language apps
├── applications/        # auxiliary apps/ports used in the build
├── compat/              # compatibility/ports tree
├── tools/               # image packing, AppFS, and validation tools
├── linker/              # linker scripts
├── assets/              # boot and runtime images
└── docs/
    ├── *.md             # detailed technical documentation
    └── guidelines/      # plans, guidelines, and agent-oriented docs
```

## Build

### Requirements

- `nasm`
- `make`
- `python3`
- recommended i386 ELF toolchain:
  - `i686-elf-gcc`
  - `i686-elf-ld`
  - `i686-elf-objcopy`
- `qemu-system-i386` or `qemu-system-x86_64`
- FAT tools to format/copy files into the boot partition:
  - `mkfs.fat` or equivalent
  - `mcopy`
  - `mmd`

### Linux

Minimal Debian/Ubuntu example:

```bash
sudo apt update
sudo apt install -y build-essential make python3 nasm qemu-system-x86 mtools dosfstools binutils gcc-multilib
```

### macOS

Homebrew example:

```bash
brew install nasm qemu mtools dosfstools i686-elf-gcc
```

### Main command

```bash
make
```

Useful targets:

- `make` or `make all`: full image build
- `make full`: clean and rebuild everything
- `make img`: generate the bootable image
- `make imb`: generate the final image for writing/external use
- `make legacy-data-img`: generate only `build/data-partition.img`
- `make clean`: remove artifacts

## Running in QEMU

Normal mode:

```bash
make run
```

Headless mode with serial output in the terminal:

```bash
make run-headless-debug
```

Other useful debug targets already defined in the `Makefile`:

- `make run-debug`
- `make run-headless-core2duo-debug`
- `make run-headless-pentium-debug`
- `make run-headless-atom-debug`
- `make run-headless-ahci-debug`
- `make run-headless-usb-debug`

## Generated artifacts

The most important outputs today are:

- `build/mbr.bin`
- `build/boot.bin`
- `build/stage2.bin`
- `build/kernel.bin`
- `build/data-partition.img`
- `build/boot.img`
- `build/generated/app_catalog.h`
- `build/lang/userland.app`

The build also generates layout/policy manifests for the FAT32 boot partition.

## Documentation

### Main technical documentation

- [docs/overview.md](docs/overview.md): general map
- [docs/workflow.md](docs/workflow.md): full flow from boot to apps
- [docs/memory_map.md](docs/memory_map.md): memory and disk layout
- [docs/kernel_init.md](docs/kernel_init.md): real kernel initialization sequence
- [docs/drivers.md](docs/drivers.md): current drivers
- [docs/runtime_and_services.md](docs/runtime_and_services.md): scheduler, services, init, syscalls, and AppFS
- [docs/apps_and_modules.md](docs/apps_and_modules.md): app, module, and runtime catalog

### Guidelines and plans

They are useful as historical context and planning material, but they should not be read as the definitive description of the current code state. These files are available under [docs/guidelines/](docs/guidelines/).

## Current project status

What already exists in a material sense:

- multi-stage BIOS boot
- FAT32 boot partition working in the pipeline
- `stage2` loading `KERNEL.BIN`
- `BOOTINFO` shared between loader and kernel
- VESA framebuffer preserved when valid
- basic video, keyboard, mouse, timer, and storage drivers
- simple scheduler and task/process model
- microkernel-style service layer
- enough syscalls for shell, graphical runtime, and AppFS
- built-in `init` plus external `userland.app`
- desktop and a reasonable set of graphical apps
- large catalog of external apps, utilities, and ports

What remains limited or transitional:

- the kernel VFS is still minimal
- the main app execution path still goes through AppFS, not a general ELF/VFS loader
- process isolation and a full "ring3 complete" model should not be assumed
- several compatibility/port areas are still adapted pragmatically
- the repository still contains a lot of transitional code and historical documentation

## Project future

The project is currently being developed by me and dear [Mel Santos](https://github.com/melmonfre). The goal is to push it as far as possible until we have a complete version of a small modular operating system. In addition, since Mr. Raposo is behind the project, there is a plan to build a small SLM (Small Language Model) running directly in the kernel using a simple GPT-like architecture, plus as many improvement techniques as we can reasonably implement. The idea is to expose that intelligence inside the shell.
