# Memory and Disk Layout

This document combines the low-memory boot map, the kernel's early reservations, and the on-disk layout that ties the BIOS loader to the AppFS/runtime world.

## 1. Low-memory boot layout

These addresses matter because every boot stage shares the first megabyte.

| Address range | Purpose |
| --- | --- |
| `0x0000-0x03FF` | BIOS IVT |
| `0x0400-0x04FF` | BIOS Data Area |
| `0x0500-0x05FF` | legacy VESA / A20 scratch |
| `0x0600-0x08FF` | BIOS disk scratch buffer |
| `0x1000-0x10FF+` | persistent boot trace buffer |
| `0x7000-0x71FF` | relocated MBR |
| `0x7C00-0x7DFF` | VBR / stage 1 load address |
| `0x8D00-0x8D83+` | `BOOTINFO` |
| `0x9000-...` | stage 2 image |
| `0xA00-0xDFF` | VBE info / mode info / scratch |
| `0xC000` | real-mode stack top used by stage 2 |
| `0x10000` | kernel physical load base |
| `0x6F000` | protected-mode fault stack in stage 2 |
| `0x70000` | protected-mode stage 2 stack |
| `0x80000` | optional boot background staging area |

## 2. BOOTINFO structure

`BOOTINFO` lives at `0x8D00` and has a fixed packed layout described in [`headers/kernel/bootinfo.h`](../../headers/kernel/bootinfo.h).

Major regions:

- header:
  - magic
  - version
  - flags
- VESA block:
  - mode number
  - framebuffer address
  - pitch
  - width
  - height
  - bpp
- memory block:
  - largest usable base
  - largest usable size
  - largest usable end
- disk block:
  - boot partition LBA/count
  - data partition LBA/count
- video catalog:
  - discovered VESA modes
  - active/fallback/selected indices

## 3. Kernel early memory policy

The kernel does not consume a full BIOS memory map yet. Instead it uses the largest usable range from `BOOTINFO` and carves reservations out of it.

Important reservations made in `kernel_entry()`:

- kernel/core low memory up through:
  - kernel image end
  - userland stack reserve
  - heap guard bytes
- boot framebuffer range, when the stage 2 VESA mode is valid
- three dedicated 16 MiB app backing arenas, when a large enough free span exists

The remaining largest free region becomes the kernel heap.

## 4. External app virtual arenas

The modular app runtime expects three fixed virtual arenas:

| Virtual address | Purpose |
| --- | --- |
| `0x10000000` | main app arena |
| `0x14000000` | desktop app arena |
| `0x18000000` | boot app arena |

Each arena is `16 MiB`.

These are not arbitrary constants in docs only. They are part of the AppFS runtime ABI in [`lang/include/vibe_app.h`](../../lang/include/vibe_app.h).

## 5. Disk layout produced by the build

The image builder creates an MBR-partitioned disk with two partitions:

### Partition 1: boot FAT32

- starts at LBA `2048`
- contains:
  - VBR
  - reserved sectors holding stage 2 and the kernel copy used by the loader
  - visible FAT32 files such as:
    - `/STAGE2.BIN`
    - `/KERNEL.BIN`
    - layout/policy diagnostics

Why both reserved sectors and visible files exist:

- stage 1 loads stage 2 from reserved sectors
- stage 2 loads the kernel by FAT32 filename
- visible files make the boot volume inspectable and debuggable

### Partition 2: raw data partition

The second partition is not FAT32. It is a raw layout consumed by the current runtime:

1. AppFS directory
2. AppFS app area
3. persistence area
4. raw bundled assets

Important constants:

- AppFS directory LBA: `0`
- AppFS directory sectors: `16`
- App area sectors: `131072`
- persistence starts immediately after the app area

The kernel obtains the base of this partition from `BOOTINFO.disk`.

## 6. AppFS directory layout

AppFS version 1 starts with a packed directory blob:

- magic
- version
- entry count
- checksum
- up to 96 entries

Each entry stores:

- app name
- start LBA relative to the data partition
- sector count
- exact image size
- flags

The directory format is defined in [`lang/include/vibe_appfs.h`](../../lang/include/vibe_appfs.h) and built by [`tools/build_appfs.py`](../../tools/build_appfs.py).

## 7. Persistence and asset regions

The userland FS layer uses sectors after the AppFS app area for persistence.

Boot/runtime assets are also mapped at fixed LBAs relative to the data partition, including paths used by:

- DOOM asset registration
- Craft textures and fonts
- wallpaper registration

Those assets are discovered lazily by the userland FS layer when the relevant paths are accessed.

## Practical summary

The current system depends on two separate storage worlds:

- a BIOS-readable FAT32 boot world for stage 2 and `KERNEL.BIN`
- a raw data partition world for AppFS, persistence, and bundled runtime assets

That split is fundamental to understanding why boot works one way and app execution works another.
