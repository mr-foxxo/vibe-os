# End-to-End Boot Workflow

This is the chronological map of the current system, based on the actual code paths in `boot/`, `kernel/`, `userland/`, `lang/`, and the image-building tools.

## 1. BIOS and initial sector load

- The machine starts in 16-bit real mode.
- BIOS loads sector 0 of the boot disk to physical address `0x7C00`.
- Control enters [`boot/mbr.asm`](../../boot/mbr.asm).

## 2. Stage 0: MBR

- The MBR disables interrupts and sets flat real-mode segments with a stack at `0x7C00`.
- It relocates its own 512-byte image from `0x7C00` to `0x7000` so that `0x7C00` can be reused by the next stage.
- It saves `dl` as the BIOS boot drive.
- It clears and initializes `BOOTINFO` at `0x8D00`.
- It publishes partition geometry into `BOOTINFO`:
  - boot partition LBA and sector count
  - data partition LBA and sector count
- It scans the embedded partition table for the active partition.
- It verifies EDD support with `int 13h, ah=41h`.
- It uses `int 13h, ah=42h` with a Disk Address Packet to read the active partition VBR into `0x0600`, copies it to `0x7C00`, and jumps there.

## 3. Stage 1: active partition VBR

- Control enters [`boot/stage1.asm`](../../boot/stage1.asm), still in 16-bit real mode at `0x7C00`.
- Stage 1 reinitializes the segments and stack.
- It records a persistent low-memory debug trace at `0x1000`.
- It computes the absolute disk LBA of stage 2 using:
  - the FAT32 BPB hidden-sector field inside the VBR at `0x7C1C`
  - the reserved-sector-relative constant `STAGE2_START_LBA`
- It reads `STAGE2_SECTORS` sectors to segment `0x0900` (`0x9000` physical).
- If the BIOS read fails, it prints `STAGE1 DISK ERROR` and dumps the previous boot trace.
- On success it jumps to `0x0900:0x0000`.

## 4. Stage 2 entry and FAT32 metadata parsing

- Control enters [`boot/stage2.asm`](../../boot/stage2.asm) at `0x9000`, still in real mode.
- Stage 2 resets text mode first (`int 10h, ax=0003h`) so early failures are visible.
- It clears and reinitializes `BOOTINFO`, preserving the contract layout expected by the kernel.
- It parses the FAT32 BPB that still lives in the VBR image at `0x7C00`.
- From the BPB it derives:
  - reserved sector count
  - FAT count
  - FAT size
  - root cluster
  - hidden sectors
  - sectors per cluster
  - FAT start LBA
  - data start LBA

## 5. Stage 2 optional asset loading and platform discovery

- Stage 2 optionally loads `VIBEBG.BIN` from the FAT32 root directory into `0x80000`.
- It enumerates VESA modes through `int 10h, ax=4F00h/4F01h`.
- It filters modes aggressively:
  - 8 bpp only
  - linear framebuffer required
  - width and height within the allowed range
  - valid pitch and framebuffer base
- It builds a video mode catalog inside `BOOTINFO`.
- It probes usable RAM with `int 15h, eax=E820h` and publishes the largest >= 1 MiB region into `BOOTINFO`.
- If E820 fails, it falls back to `int 15h, ax=E801h` and then `ah=88h`.
- It enables A20 and verifies it with multiple tests and BIOS-state fallbacks.

## 6. Stage 2 protected-mode loader UI

- Stage 2 loads a GDT and flips CR0.PE to enter 32-bit protected mode.
- In protected mode it draws the Vibeloader menu, using the VESA linear framebuffer when possible.
- The menu currently exposes three logical boot choices:
  - `VIBEOS`
  - `SAFE MODE`
  - `RESCUE SHELL`
- The menu also lets the user cycle through discovered video modes.
- The chosen boot mode is encoded back into `BOOTINFO.flags`:
  - `BOOTINFO_FLAG_BOOT_TO_DESKTOP`
  - `BOOTINFO_FLAG_BOOT_SAFE_MODE`
  - `BOOTINFO_FLAG_BOOT_RESCUE_SHELL`
- The selected video mode is also written back to the `BOOTINFO` video catalog and VESA fields.

## 7. Real-mode return for kernel loading

- After menu confirmation, stage 2 returns from protected mode to real mode.
- It reapplies the selected VESA mode through BIOS if the selected mode differs from the current active one.
- It searches the FAT32 root directory for `KERNEL.BIN`.
- It follows the file cluster chain via FAT lookups and reads the file to segment `0x1000` (`0x10000` physical).
- It re-enables and revalidates A20.
- It reloads the GDT, re-enters protected mode, and jumps to the kernel entry path.

## 8. Kernel early entry

- Control reaches [`kernel/entry.c`](../../kernel/entry.c) at the `.entry` section entrypoint.
- The kernel zeroes `.bss`.
- It emits very early progress breadcrumbs to:
  - I/O port `0x80`
  - I/O port `0xE9`
  - the framebuffer, when boot VESA mode is valid
  - the persistent boot trace area at `0x1000`
- It initializes:
  - debug subsystem
  - microkernel launch bookkeeping
  - microkernel service registry
  - HAL
  - CPU/GDT state
  - video binding to the boot framebuffer
  - text console

## 9. Kernel platform bring-up

- The kernel sets up the IDT and remaps the PIC.
- It programs the PIT for 100 Hz ticks.
- It initializes the PS/2 keyboard and PS/2 mouse.
- It unmasks IRQ lines 0, 1, 2, and 12 and enables interrupts.
- It initializes the memory subsystem.
- It reserves:
  - the kernel/core low-memory area
  - the boot framebuffer region, if present
  - a dedicated backing area for three 16 MiB external app arenas, when possible
- It creates the kernel heap from the largest remaining free region.

## 10. Kernel storage, scheduler, and service registration

- The kernel probes native storage:
  - AHCI first
  - ATA PIO fallback second
- It initializes the scheduler and the simple driver manager.
- It initializes the current VFS layer, which is only a seeded `ramfs`.
- It registers or launches the microkernel-style services:
  - storage
  - filesystem
  - video
  - input
  - console
  - network
  - audio
- It installs the syscall table.

## 11. Built-in init launch

- `userland_run()` in [`kernel/userland_loader.c`](../../kernel/userland_loader.c) creates a bootstrap launch descriptor for the built-in `init` service.
- That service entry is `userland_entry()` from [`userland/bootstrap_init.c`](../../userland/bootstrap_init.c).
- The scheduler is entered.

## 12. Built-in init service work

- `init` starts the console and the userland filesystem layer.
- It performs a storage smoke test against a reserved persistence sector.
- It prints the bootstrap banner and reports whether safe mode or rescue shell was requested.
- It tries to launch external AppFS apps through `lang_try_run()`:
  - first `userland`
  - if normal desktop boot was requested and `userland` fails, then `startx`
- If rescue shell was requested, it skips the external `userland` app.
- If the external app path fails, it falls back to the built-in shell path.

## 13. External app runtime and shell

- The userland loader/runtime in `lang/` reads the AppFS directory from the raw data partition.
- It maps apps into one of the reserved virtual arenas:
  - `0x10000000` main arena
  - `0x14000000` desktop arena
  - `0x18000000` boot arena
- `userland.app` starts the modular shell.
- In normal desktop mode, `userland.app` auto-runs `startx` before dropping to the shell.

## 14. Desktop and application execution

- `startx` launches the external desktop app/runtime.
- The desktop owns the window system and launches app modules such as:
  - terminal
  - clock
  - filemanager
  - editor
  - task manager
  - calculator
  - image viewer
  - sketchpad
  - multiple games
- Additional command-line applications, language runtimes, BSD game ports, and POSIX-style utilities are exposed through the shell-visible AppFS catalog.

## Practical summary

The shortest accurate sentence for the current architecture is:

`BIOS -> MBR -> FAT32 VBR -> stage2 -> KERNEL.BIN -> kernel_entry -> built-in init -> AppFS loader -> userland.app/startx -> shell and desktop apps`
