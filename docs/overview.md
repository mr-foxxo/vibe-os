# Vibe OS Detailed Map

For the docs directory layout and canonical file locations, see [README.md](README.md).

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
- [`drivers.md`](drivers.md): storage, video, input, timer, interrupt, debug, network, and audio driver behavior.
- [`runtime_and_services.md`](runtime_and_services.md): scheduler, process model, microkernel services, syscalls, init, AppFS loading, and shell/Desktop bootstrap.
- [`apps_and_modules.md`](apps_and_modules.md): built-in bootstrap code, modular apps, language runtimes, desktop apps, games, and compatibility ports.

## New Drivers

The kernel now includes a comprehensive set of hardware drivers:

- **Network**: `virtio_net.c` (VirtIO), `e1000.c` (Intel PRO/1000 family), `rtl8139.c` (Realtek), `wifi_data_plane.c` (Intel 7260 with real BAR0/MAC/firmware/IRQ), `wifi_wpa2.c` (PBKDF2-SHA1, HMAC-SHA1, 4-way EAPOL handshake), `wifi_scanning.c` (802.11 probe requests, beacon parsing, channel hopping)
- **Storage**: `nvme.c` (NVMe with admin/I/O queues, PRP), `usb_storage.c` (USB BOT with SCSI command set)
- **Audio**: `hda.c` (Intel HDA with CORB/RIRB, DMA BDL playback)
- **Video DRM**: `drm.c` (orchestrator), `bga.c` (Bochs/QEMU/VirtualBox), `i915.c` (Intel gen5+), `radeon.c` (AMD r6xx+), `nouveau.c` (NVIDIA NV40+) — native GPU modeset with commit/restore and automatic fallback

## Memory Subsystem Rewrite

- `heap.c`: bump allocator replaced with free-list allocator (best-fit, coalescing, `kernel_realloc`)
- `physmem.c`: supports up to 4GB with dynamic bitmap, `alloc_phys_pages()`/`free_phys_pages()`
- `paging.c`: 4KB page support with `paging_map_page()`/`paging_unmap_page()`, page table allocation
- `memory_init.c`: improved bootinfo detection

## GUI Rewrite

The desktop GUI has been completely rewritten:

- **microui.h/c**: 840-line embedded immediate-mode UI library integrated
- **mu_renderer.h/c**: VibeOS-specific renderer backend
- **desktop_new.c**: Dynamic desktop (492 lines vs 7000+ legacy), menu from app registry, taskbar, sound/network applets

## Microkernel Improvements

- **IPC**: queues per process, blocking receive with waitable
- **Service restart**: retry with backoff, event subscriptions (online/offline/degraded/restarted)
- **Async messaging**: transfer buffers for efficient data passing
- **All services migrated**: Video (service-owned with worker thread), Audio (dataplane executor), Input (event queue), Network (socket queues, NIC drivers) — all with local fallback for service worker offline

## Known Bugs Fixed

- Audio: playback_buffer drain in `mk_audio_service_pump_async()`
- Network: DHCP parser size check relaxed
- Doom: WAD path resolution with 6 alias names

## Key facts to keep in mind

- The boot chain is BIOS-only. Early disk and video access still depend on BIOS interrupts.
- `BOOTINFO` at `0x8D00` is the main contract between boot stages and the kernel.
- The boot partition is FAT32 and contains visible diagnostic artifacts like `KERNEL.BIN` and `STAGE2.BIN`.
- The data partition is a raw layout that starts with an AppFS directory and app area, then persistence sectors, then bundled raw assets.
- The kernel does not directly boot the full desktop. It boots a tiny built-in `init`, and `init` launches external apps from AppFS.
- The current in-kernel VFS is still minimal (`ramfs` only). Real application loading is still routed through AppFS and the userland loader.
