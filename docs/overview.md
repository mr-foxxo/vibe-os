# Vibe OS Detailed Map

This directory is the code-guided reference for how Vibe OS boots, initializes the kernel, exposes services, and finally reaches runnable applications.

The current architecture is not a classic monolithic "load kernel, then exec ELF binaries from a mature VFS" design. The real flow is:

1. BIOS loads the MBR.
2. The MBR loads the active partition VBR.
3. Stage 1 loads `STAGE2.BIN` from the boot partition reserved sectors.
4. Stage 2 parses FAT32 metadata from the boot partition, discovers video and memory, shows the boot menu, and loads `KERNEL.BIN`.
5. The kernel performs early platform bring-up, reserves memory, initializes drivers and microkernel services, and launches a built-in `init` service.
6. `init` mounts the lightweight userland filesystem/runtime, probes storage/AppFS, and tries to launch the external `userland.app`.
7. `userland.app` starts the shell and, in normal desktop boot mode, auto-runs `startx`.
8. `startx` and the desktop-related apps run from the external AppFS catalog.

## Reading order

- [`workflow.md`](workflow.md): the complete end-to-end execution path from BIOS reset vector assumptions to runnable apps.
- [`mbr.md`](mbr.md): stage 0, partition discovery, and initial `BOOTINFO` publication.
- [`stage1.md`](stage1.md): VBR behavior and stage 2 loading contract.
- [`stage2.md`](stage2.md): FAT32 parsing, BIOS discovery, menu logic, boot mode flags, and kernel handoff.
- [`memory_map.md`](memory_map.md): low-memory layout, bootloader scratch regions, kernel reservations, disk layout, and AppFS arenas.
- [`kernel_init.md`](kernel_init.md): what `kernel_entry()` really initializes and in which order.
- [`drivers.md`](drivers.md): storage, video, input, timer, interrupt, and debug driver behavior.
- [`runtime_and_services.md`](runtime_and_services.md): scheduler, process model, microkernel services, syscalls, init, AppFS loading, and shell/Desktop bootstrap.
- [`apps_and_modules.md`](apps_and_modules.md): built-in bootstrap code, modular apps, language runtimes, desktop apps, games, and compatibility ports.

## Key facts to keep in mind

- The boot chain is BIOS-only. Early disk and video access still depend on BIOS interrupts.
- `BOOTINFO` at `0x8D00` is the main contract between boot stages and the kernel.
- The boot partition is FAT32 and contains visible diagnostic artifacts like `KERNEL.BIN` and `STAGE2.BIN`.
- The data partition is a raw layout that starts with an AppFS directory and app area, then persistence sectors, then bundled raw assets.
- The kernel does not directly boot the full desktop. It boots a tiny built-in `init`, and `init` launches external apps from AppFS.
- The current in-kernel VFS is still minimal (`ramfs` only). Real application loading is still routed through AppFS and the userland loader.
