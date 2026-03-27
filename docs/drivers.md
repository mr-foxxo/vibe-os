# Drivers

This page documents the drivers that are actually present in `kernel/drivers/`.

## Driver overview

| Area | Files | Purpose |
| --- | --- | --- |
| Debug | `debug/serial.c` plus debug helpers | early logging / debug output |
| Interrupt | `interrupt/pic.c` | PIC remap, IRQ masking, EOI, irqsave helpers |
| Timer | `timer/pit.c` | PIT programming and tick counter |
| Input | `input/keyboard.c`, `input/mouse.c`, keymaps | PS/2 keyboard/mouse input |
| Video | `video/video.c`, `video/vesa.c`, `video/text.c`, `video/vga.c` | boot LFB binding, drawing, text fallback |
| Storage | `storage/storage.c`, `storage/ahci.c`, `storage/ata.c`, `storage/block_device.c` | native disk access after bootloader handoff |

## Debug

The debug path is used very early and is critical for bring-up.

Observed behavior:

- emits progress markers to port `0xE9`
- used heavily by the kernel bootstrap, service layer, storage probing, and scheduler tracing

The debug subsystem is also why the codebase contains many "trace budget" counters: the goal is to avoid flooding logs while still giving enough signal on early failures.

## Interrupt controller: PIC

Source: [`kernel/drivers/interrupt/pic.c`](../../kernel/drivers/interrupt/pic.c)

Current behavior:

- remaps the master/slave PIC to vectors `0x20` and `0x28`
- unmasks IRQs 0, 1, 2, and 12
- provides:
  - `kernel_irq_enable()`
  - `kernel_irq_save()`
  - `kernel_irq_restore()`
  - `kernel_pic_send_eoi()`

This is still a PIC-first system even though LAPIC detection exists elsewhere.

## Timer: PIT

Source: [`kernel/drivers/timer/pit.c`](../../kernel/drivers/timer/pit.c)

Behavior:

- programs channel 0 of the PIT
- defaults to 100 Hz
- maintains a global tick counter
- sends EOI on IRQ 0
- emits occasional debug traces

That tick counter drives scheduler accounting and some userland timing behavior.

## Keyboard: PS/2

Source: [`kernel/drivers/input/keyboard.c`](../../kernel/drivers/input/keyboard.c)

Behavior:

- initializes the PS/2 controller and keyboard device
- enables IRQ 1
- maintains a ring buffer of translated key events
- tracks shift, ctrl, and extended scancodes
- translates arrow and delete keys to extended logical keycodes
- supports runtime keyboard layout switching

Available layouts currently include:

- `us`
- `pt-br`
- `br-abnt2`
- `us-intl`
- `es`
- `fr`
- `de`

The keyboard path is intentionally direct because shell input latency matters. Not every input request is forced through a service worker.

## Mouse: PS/2

Source: [`kernel/drivers/input/mouse.c`](../../kernel/drivers/input/mouse.c)

Behavior:

- enables the auxiliary PS/2 port
- sends reset/default-data-reporting commands
- consumes three-byte PS/2 packets on IRQ 12
- maintains cursor position, deltas, and button state
- clamps the pointer to the active video mode bounds
- stores recent events in a queue

The initial pointer position is the screen center when a valid graphics mode exists.

## Video stack

### `vesa.c`

This file does not switch modes itself. It validates and imports the loader-published VESA mode from `BOOTINFO`.

### `video.c`

This is the real runtime video layer.

Responsibilities:

- bind to the boot linear framebuffer
- preserve and expose the active mode
- manage palette state
- manage backbuffering
- flip frames
- draw graphics primitives and text
- expose video capabilities to userland
- change modes when supported

Important constraint:

- the runtime still assumes 8-bit indexed color for the main graphics path

### `text.c`

This is the text console abstraction.

Behavior:

- uses VGA text memory when no graphics mode is active
- otherwise re-renders text cells into the graphics backbuffer
- tracks cursor position and scroll state

That dual behavior is why text output still works after a graphical boot.

## Storage stack

### Storage orchestrator

Source: [`kernel/drivers/storage/storage.c`](../../kernel/drivers/storage/storage.c)

Behavior:

- resets the block-device layer
- tries AHCI first
- falls back to ATA

### ATA backend

Source: [`kernel/drivers/storage/ata.c`](../../kernel/drivers/storage/ata.c)

Behavior:

- legacy primary-bus ATA PIO path
- IDENTIFY-based probe
- sector read/write through port I/O
- spinlock-protected serialized access
- write verification after writes
- respects the data partition geometry published by `BOOTINFO`

This backend is important because the bootloader itself used BIOS disk I/O, but once the kernel is running, BIOS calls are no longer the normal storage path.

### AHCI backend

Source: [`kernel/drivers/storage/ahci.c`](../../kernel/drivers/storage/ahci.c)

Behavior:

- scans PCI config space for a SATA AHCI controller
- enables memory-space and bus-master bits
- maps the AHCI BAR
- sets up command lists, FIS receive areas, and command tables
- issues ATA identify/read/write commands through AHCI DMA structures
- exposes the discovered disk as the primary block device

### Block-device layer

The block-device layer abstracts the active backend so higher layers can request logical sectors from the data partition rather than raw controller-specific operations.

## Current limitations

- no native USB mass-storage driver
- no NVMe driver
- no general PCI device framework beyond ad hoc probing in AHCI
- no advanced input stack beyond PS/2
- video path is centered around boot LFB preservation and 8-bit modes

These limits matter because they shape what the bootloader must still do before the kernel takes over.
