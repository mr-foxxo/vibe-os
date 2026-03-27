# Stage 0: MBR

Source: [`boot/mbr.asm`](../../boot/mbr.asm)

## Role

The MBR is the BIOS entrypoint and the only code the firmware loads directly. Its job is intentionally small:

- relocate itself away from `0x7C00`
- initialize shared boot metadata
- find the active partition
- load exactly one sector from that partition, the VBR

It does not understand FAT32 files. It only understands the partition table and BIOS sector reads.

## Load location and relocation

- BIOS loads the MBR to `0x7C00`.
- The MBR sets `ds = es = ss = 0` and `sp = 0x7C00`.
- It copies 512 bytes from `0x7C00` to `0x7000`.
- It jumps to the relocated copy.

This frees `0x7C00` so the active partition boot sector can later occupy the conventional BIOS boot address.

## BOOTINFO contract

The MBR initializes `BOOTINFO` at `0x8D00`.

Published values:

- `magic = 0x544F4256`
- `version = 2`
- `flags = BOOTINFO_FLAG_PARTITIONS_VALID`
- boot partition start LBA and size
- data partition start LBA and size

At this point the only valid information is partition geometry. VESA and memory information are added later by stage 2.

## Partition behavior

The partition table is embedded directly in the assembled MBR image:

- entry 0: active FAT32 boot partition
- entry 1: raw data/software partition
- entries 2 and 3: zeroed

The loader scans the four entries for a boot flag of `0x80`. If none is found, it falls back to the first entry.

## Disk I/O path

The MBR uses BIOS Extended Disk Drive reads:

- `int 13h, ah=41h` to validate EDD support
- `int 13h, ah=42h` to read sectors through a Disk Address Packet

The VBR is first read to `0x0600`, then copied to `0x7C00`, then executed.

## Important limits

- No filesystem parsing happens here.
- No protected-mode setup happens here.
- No attempt is made to load stage 2 directly from a filename.
- The MBR assumes the partition table embedded in the image builder is correct.

## Why this matters

The MBR is the point where the image layout produced by the build system becomes the runtime contract. If the partition geometry encoded at build time is wrong, every later stage inherits the wrong disk map.
