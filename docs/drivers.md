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
| Storage | `storage/storage.c`, `storage/ahci.c`, `storage/ata.c`, `storage/block_device.c`, `storage/nvme.c`, `storage/usb_storage.c` | native disk access, NVMe, USB mass-storage |
| Network | `network/virtio_net.c`, `network/e1000.c`, `network/rtl8139.c`, `network/wifi_data_plane.c`, `network/wifi_wpa2.c`, `network/wifi_scanning.c` | VirtIO, Intel PRO/1000, Realtek, Intel 7260 Wi-Fi with WPA2 |
| Audio | `audio/hda.c` | Intel HDA with CORB/RIRB, DMA BDL playback |

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
- ioctl interface: `kernel_video_ioctl()` with `DRV_IOCTL_GET_STATUS`, `VID_IOCTL_GET_MODE`, `VID_IOCTL_GET_BACKEND`, `VID_IOCTL_GET_FRAMEBUFFER`
- registered with driver_manager as `"video"/"display"`

Important constraint:

- the runtime still assumes 8-bit indexed color for the main graphics path

### DRM backends (native GPU)

Sources:
- [`kernel/drivers/video/drm/drm.c`](../../kernel/drivers/video/drm/drm.c) — DRM orchestrator
- [`kernel/drivers/video/drm/bga.c`](../../kernel/drivers/video/drm/bga.c) — Bochs/QEMU/VirtualBox VGA
- [`kernel/drivers/video/drm/i915.c`](../../kernel/drivers/video/drm/i915.c) — Intel gen5+ integrated graphics
- [`kernel/drivers/video/drm/radeon.c`](../../kernel/drivers/video/drm/radeon.c) — AMD Radeon r6xx+ (AVIVO)
- [`kernel/drivers/video/drm/nouveau.c`](../../kernel/drivers/video/drm/nouveau.c) — NVIDIA NV40+ (experimental)

The DRM layer provides native GPU modesetting with automatic fallback:

- **BGA**: Bochs VBE/VirtualBox VGA — proven on QEMU, VirtualBox, and real hardware
- **i915**: Intel gen5+ (Iron Lake through Haswell) — full MMIO modeset with commit/restore, primary plane programming, PLL configuration
- **Radeon**: AMD r6xx family (RS690, RS780, RV610–RV770, Evergreen) — AVIVO display controller, D1 scanout engine, PLL compute, verified stage/commit/restore
- **Nouveau**: NVIDIA NV40+ — CRTC programming, timing tables, restore support, currently gated behind explicit enable flag

Each backend implements the `kernel_drm_backend_ops` interface: `probe`, `set_mode`, `revert_last_modeset`, `forget_last_modeset`, and `prepare_for_bios_modeset`. Probed candidates are enumerated via PCI and logged at boot.

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

## Network drivers

### virtio_net

Source: [`kernel/drivers/network/virtio_net.c`](../../kernel/drivers/network/virtio_net.c)

Full VirtIO network driver with:
- legacy and modern virtqueue ring support
- TX/RX descriptor rings
- link state detection
- MAC address discovery
- interrupt-driven operation

### e1000 (Intel PRO/1000)

Source: [`kernel/drivers/network/e1000.c`](../../kernel/drivers/network/e1000.c)

Intel PRO/1000 family driver supporting:
- 82540EM, 82574L, 82571, 82572 variants
- PCI BAR mapping and bus-master enable
- descriptor ring TX/RX
- link state and speed detection
- interrupt-driven packet delivery

### rtl8139 (Realtek)

Source: [`kernel/drivers/network/rtl8139.c`](../../kernel/drivers/network/rtl8139.c)

Realtek RTL8139/RTL8169 driver with:
- PCI probe and I/O BAR access
- early chip reset
- RX ring and TX descriptor setup
- link polling and interrupt operation

### Wi-Fi stack

Source files:
- [`kernel/drivers/network/wifi_data_plane.c`](../../kernel/drivers/network/wifi_data_plane.c)
- [`kernel/drivers/network/wifi_wpa2.c`](../../kernel/drivers/network/wifi_wpa2.c)
- [`kernel/drivers/network/wifi_scanning.c`](../../kernel/drivers/network/wifi_scanning.c)
- [`kernel/drivers/network/wifi_stub.c`](../../kernel/drivers/network/wifi_stub.c)

Intel 7260 wireless driver with:
- real BAR0 access, MAC address discovery, firmware loading, IRQ handling
- PBKDF2-SHA1 with 4096 iterations and HMAC-SHA1 for WPA2
- 4-way EAPOL-Key handshake for WPA2-PSK
- 802.11 probe request transmission
- beacon frame parsing
- channel hopping across 1-11
- enhanced stub with realistic AP profiles

## Storage drivers

### NVMe

Source: [`kernel/drivers/storage/nvme.c`](../../kernel/drivers/storage/nvme.c)

NVMe driver with:
- PCI controller discovery and BAR mapping
- admin queue creation and submission
- I/O queue creation with PRP (Physical Region Page) support
- identify controller/namespace commands
- sector read/write through NVMe submission/completion queues
- IRQ or polling operation

### USB Storage (BOT)

Source: [`kernel/drivers/storage/usb_storage.c`](../../kernel/drivers/storage/usb_storage.c)

USB Bulk-Only Transport driver with:
- SCSI command set (READ10, WRITE10, INQUIRY, READ CAPACITY)
- CBW/CSW protocol handling
- USB UHCI/OHCI transport integration
- sector read/write abstraction

## Audio driver

### Intel HDA

Source: [`kernel/drivers/audio/hda.c`](../../kernel/drivers/audio/hda.c)

Intel High Definition Audio driver with:
- PCI controller discovery and MMIO BAR mapping
- CORB (Command Output Ring Buffer) and RIRB (Response Input Ring Buffer) engine
- codec/probe with widget topology enumeration
- playback stream with BDL (Buffer Descriptor List) DMA
- IRQ handling for stream completion and codec responses
- Multiple backend support: `compat-azalia` (HDA), `compat-auich` (AC97), `compat-uaudio` (USB), `pcspkr` (fallback buzzer)

### Real Hardware Status

**Resolved:**
- NVIDIA GPU: ✅ Nouveau modeset implemented (NV40+), CRTC timing programming, framebuffer scanout
- Wi-Fi: ⚠️ NOT connectable. `wifi_pci.c` detects Intel 7260 / RTL8188/8192 / AR93xx / BCM4360, and `wifi_scanning.c`/`wifi_wpa2.c` are genuine module code (probe frames; PBKDF2-SHA1 + 4-way EAPOL crypto) but they are ORPHANED: no 802.11 data plane binds them, the service scan returns an empty list and connect refuses with ENOSYS (COMPLETION_ACTION_PLAN line 84). The Intel 7260 register driver in `wifi_data_plane.c` is experimental-gated OFF by default so a production boot never pokes an unattached chip (line 85). Do not claim Wi-Fi connectivity until lines 86-87 (auth/assoc/EAPOL frame path + CCMP) land.
- Audio: ✅ HDA Intel (ICH6-ICL/TGL, NVIDIA HDMI), AC97 (Intel ICH, SiS, NVIDIA, AMD, ATI, ALI, VIA), USB Audio Class 1.0
- Storage: ✅ ATA PIO, AHCI, NVMe, USB mass storage (BOT/SCSI)

**Still in progress:**
- Suspend/resume: not implemented (requires ACPI S3/S4)
- Hotplug (USB, PCIe): not implemented; all devices must be present at boot
- Audio capture: not validated on real hardware (HDA/AC97 capture path exists)
