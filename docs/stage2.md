# Stage 2: BIOS Loader, FAT32 Reader, Menu, and Kernel Handoff

Source: [`boot/stage2.asm`](../../boot/stage2.asm)

## Role

Stage 2 is the real bootloader. It is the first stage that understands enough of the boot partition to behave like an OS loader instead of a pure sector chain.

Its responsibilities are:

- rebuild `BOOTINFO`
- parse the FAT32 BPB
- read files from the FAT32 root directory
- discover video modes and usable memory
- enable A20
- present the boot menu
- apply boot mode and video selections
- load `KERNEL.BIN`
- switch to protected mode and hand off to the kernel

## Real-mode entry work

Immediately after entry, stage 2:

- resets the screen to BIOS text mode for visible early diagnostics
- clears and initializes `BOOTINFO`
- resets the persistent boot trace helpers
- parses the FAT32 BPB left in memory by the VBR

The parsed FAT32 fields drive all later file lookups.

## FAT32 support scope

Stage 2 contains a minimal FAT32 reader, enough for boot-time lookup of root-directory files.

Implemented behavior:

- convert cluster numbers to LBAs
- read individual sectors via BIOS EDD
- walk root directory entries
- skip deleted entries and long-filename entries
- follow FAT cluster chains
- load files linearly into memory

The loader searches for 8.3 names:

- `KERNEL  BIN`
- `VIBEBG  BIN`

This is intentionally narrow. It is not a general FAT32 runtime.

## Optional background asset

If `VIBEBG.BIN` exists and has the exact expected size, stage 2 loads it into memory and uses it when drawing the menu background.

If it is missing or invalid, the boot continues without it.

## Video discovery and selection

Stage 2 enumerates VESA modes and builds a mode catalog inside `BOOTINFO`.

Filtering rules include:

- linear framebuffer required
- 8 bpp only
- valid framebuffer base address
- valid pitch
- minimum 640x480
- upper resolution bounds enforced

It stores:

- active boot mode
- fallback mode index
- selected mode index
- up to 16 discovered modes

If the current BIOS-provided mode is already valid, stage 2 preserves it. Otherwise it picks the selected or fallback catalog entry and updates `BOOTINFO` accordingly.

## Memory discovery

Stage 2 primarily uses `int 15h, eax=E820h` and records only the largest usable region above 1 MiB.

Published memory fields:

- `largest_base`
- `largest_size`
- `largest_end`

If E820 is unavailable, it falls back to `E801h` or `88h`.

This is enough for the current kernel bootstrap, which only needs a conservative usable range, not a full physical memory map.

## A20 strategy

The loader uses several methods to enable and validate A20:

- BIOS `int 15h`
- system control port `0x92`
- 8042 keyboard controller path
- multiple memory alias tests
- BIOS-state-based fallback acceptance

This exists because real BIOS hardware has been less reliable than emulators on the A20 path.

## Protected-mode UI

After GDT setup and CR0.PE enable, stage 2 enters 32-bit protected mode and runs the Vibeloader UI.

Current boot modes:

- `VIBEOS`: normal boot, sets `BOOTINFO_FLAG_BOOT_TO_DESKTOP`
- `SAFE MODE`: sets `BOOTINFO_FLAG_BOOT_SAFE_MODE`
- `RESCUE SHELL`: sets `BOOTINFO_FLAG_BOOT_RESCUE_SHELL`

The menu also allows left/right cycling through discovered video modes.

The selected state is persisted in `BOOTINFO`, not in a separate loader-private structure, so the kernel and userland can observe the final choice.

## Return to real mode for kernel load

Stage 2 cannot finish boot entirely in protected mode because:

- disk file loading still uses BIOS `int 13h`
- VESA mode changes still use BIOS `int 10h`

So after selection it returns to real mode, then:

1. reapplies the selected VESA mode if needed
2. reloads the FAT32 file path logic
3. finds `KERNEL.BIN`
4. loads it to segment `0x1000`
5. revalidates A20
6. re-enters protected mode

The final jump is to the protected-mode resume label that transfers control into the kernel image at physical `0x10000`.

## Debug behavior

Stage 2 is instrumented heavily for real-hardware debugging:

- persistent boot trace appended to the stage 1 trace region
- `0xE9` debug characters for emulator traces
- text-mode fallbacks for menu/video failures
- debug HUD values in the menu itself

This is important context when reading the source: many branches exist to diagnose BIOS behavior, not just to provide clean release-mode logic.

## Contract with the kernel

By the time the kernel starts, stage 2 is responsible for having published:

- valid `BOOTINFO.magic` and `BOOTINFO.version`
- partition geometry
- optional valid VESA mode and framebuffer information
- boot mode flags
- selected video mode catalog state
- a usable memory range summary

That `BOOTINFO` block is the kernel's main source of bootloader intent.
