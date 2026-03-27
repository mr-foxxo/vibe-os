# Stage 1: FAT32 VBR Loader

Source: [`boot/stage1.asm`](../../boot/stage1.asm)

## Role

Stage 1 is the boot sector of the active FAT32 boot partition. Its job is even narrower than stage 2:

- preserve a persistent boot trace
- compute where stage 2 lives on disk
- read stage 2 from reserved sectors
- far-jump into it

It still does not load files by name.

## Why stage 1 exists

The MBR can only load one sector. Stage 1 exists so the project can keep:

- a conventional BIOS+MBR boot path
- a FAT32 boot partition with a valid BPB/EBPB
- a larger stage 2 loader stored in the partition reserved sectors

## Input assumptions

When stage 1 starts:

- it is at `0x7C00`
- `dl` still contains the BIOS boot drive
- the FAT32 BPB fields are part of the sector image at `0x7C00`
- the hidden-sector field points to the partition start LBA on disk

## Stage 2 loading formula

Stage 1 computes the absolute disk location of stage 2 as:

`absolute_lba = hidden_sectors_from_bpb + STAGE2_START_LBA`

Where:

- `hidden_sectors_from_bpb` is read from offset `0x7C1C`
- `STAGE2_START_LBA` is a reserved-sector-relative constant assembled into the boot sector

The loaded image is placed at:

- segment `0x0900`
- offset `0x0000`
- physical address `0x9000`

## Persistent boot trace

Stage 1 writes breadcrumbs into low memory at `0x1000`.

Observed codes:

- `S`: stage 1 start
- `R`: reading stage 2
- `L`: stage 2 loaded
- `E`: stage 1 disk error

The same memory region is later appended by stage 2 and the kernel. If the previous boot died before the kernel marked the boot stable, stage 1 prints the previous trace before continuing.

This trace is one of the most useful debugging tools for real BIOS hardware, because it survives resets as long as low memory is not overwritten first.

## Error path

If the BIOS extended read fails:

- stage 1 switches to text mode
- prints `STAGE1 DISK ERROR`
- prints the previous boot trace and last marker
- halts forever

## What stage 1 does not do

- it does not parse FAT chains
- it does not read `STAGE2.BIN` by filename
- it does not switch to protected mode
- it does not understand the kernel image

Those responsibilities belong to stage 2.
