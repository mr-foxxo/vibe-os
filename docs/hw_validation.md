# Hardware Boot Validation

> **Acceptance bar (maintainer decision 2026-10-13): real hardware, not QEMU.**
> Boot, USB, video, audio, network and the POSIX surface are all required to
> work on physical T61/T400-class machines. QEMU matrices below are necessary
> but explicitly NOT sufficient. See COMPLETION_ACTION_PLAN "P0 hardware-first".

## Real-Hardware Boot Chain Audit (2026-10-13)

| Item | Mechanism | QEMU behavior | Real-BIOS risk | Status |
|------|-----------|---------------|----------------|--------|
| A20 confirmation | `boot/stage2.asm` memory aliasing probes | masked: SeaBIOS implements int 15h AX=2402h fallback that made boot pass | **fatal**: both probes were dead code — `xor ax,ax` (sets ZF=1) ran before `je`, so the probes could NEVER report A20 enabled; on BIOSes without 2401h/2402h (common on IBM ThinkPads) the loader halted on `a20_error` even when port-92/KBC had just enabled A20 | **FIXED** — flags latched via `setne` while ZF from `cmp` is still live; proof: loader trace now exits enable_a20 at the first probe check (`2FAVBaQ...`) instead of running every method plus the 2402h fallback (`2FAVBabcdq...`); trace read live from the guest via HMP at 0xa02f |
| A20 enable methods | int 15h AX=2401h → port 92 → 8042 KBC (0xD0/0xD1/0xAE), all bounded with timeouts | fine | fine; KBC path handles ThinkPad controllers | OK |
| CPU caches (CR0.CD/NW) | `paging_init` cleared CD+NW with WBINVD BEFORE setting PG; AP trampoline clears CD+NW in the SAME cr0 write as PG; stage2 clears CD+NW entering PM | **no-op** — QEMU/TCG does not model cache-disable, so nothing changed (phase6 6/6 identical) | **fatal-latency**: a real Core2 (T61/T400) reset leaves CR0.CD=1/NW=1; enabling paging with CD=1 forces the ENTIRE map uncacheable regardless of PCD/PAT/MTRR — the write-combining framebuffer and the kernel heap run at bus speed (video livelock / 10-100x slow boot). Invisible under QEMU | **FIXED** |
| AP trampoline data layout | data section anchored at FIXED offset 0x100 (`times 0x100-($-$$) db 0`); kernel offsets are contractual (0x100/0x118/0x11e/0x124/0x128/0x12c); each patched field carries a magic ("GDT1"/"PMOD"/"ENTR"/"STKR"/"PDIR") verified before patching + entry read-back; size guard rejects short images | fine | was a REAL trap: data used to follow `align 8`, so ANY code edit silently shifted entry_ptr/stack_ptr/page_dir_ptr and the kernel patched wrong bytes — observed: AP hung at sipi1 after a 5-byte CR0 change. Now drift is a loud diagnostic, never a silent hang | **HARDENED** |
| LBA disk reads | MBR checks int 13h AH=41h extension signature before AH=42h; VBR/stage2 read via DAP with 3x reset-retry | fine | stage1 still requires EDD (pre-EDD BIOS halts with clear error) — accepted for T61/T400 era; **2026-10-13**: MBR VBR-read and stage1 stage2-read gained bounded 3x retry + media reset (transient CF right after power-on on real spin-up drives / USB bridges is invisible to QEMU); kernel ATA path audited: already has bounded retries, soft-reset, per-attempt device re-select and tick-deadline waits | HARDENED |
| Memory map | E820 (ECX=24) → E801 → int 15h AH=88h fallback chain | fine | fine; legacy-BIOS quirks covered by fallback chain | OK |
| Boot stack vs kernel image | kernel loads at 0x10000 (403 KB today), menu stack at 0x70000 used only BEFORE kernel load; kernel handoff stack 0x800000 with linker ASSERT | fine | verified: no region overlap at any phase (0x10000+0x62AB0 = 0x72AB0 is reached only after the menu is done) | OK (re-checked after kernel growth) |

## Known-Working Configurations

### QEMU (Primary Test Target)

| Component | Configuration | Status |
|-----------|--------------|--------|
| Machine   | `q35` or `pc` (i440fx) | Validated |
| CPU       | `qemu64`, `host`, `max` (1-4 cores) | Validated |
| Memory    | 256 MB - 2 GB | Validated |
| Storage   | AHCI on PIIX4 or ICH9 (`-drive file=...,if=none,format=raw -device ahci,id=ahci -device ide-hd,drive=drive0,bus=ahci.0`) | Validated |
| Storage   | USB mass-storage via UHCI (`-drive file=...,if=none,format=raw -device usb-storage,drive=drive0`) | Validated as a BIOS-boot medium AND as the RUNTIME backend: native UHCI+BOT+SCSI loads fs/AppFS and passes the init read+write+restore smoke from the emulated stick (adendo 48). EHCI/xHCI runtime backends still missing; physical-stick validation pending (P0) |
| Video     | QXL, VGA, Bochs VBE (`-vga std`, `-vga virtio`, `-device bochs-display`) | Validated (BGA) |
| Video     | Cirrus (`-vga cirrus`) | Detection only |
| Audio     | AC97 (`-audiodev pa,id=pa -device ac97,audiodev=pa`) | Validated |
| Audio     | HDA (`-audiodev pa,id=pa -device hda-output,audiodev=pa`) | Validated |
| Network   | VirtIO (`-netdev user,id=net0 -device virtio-net,netdev=net0`) | Validated |
| Network   | e1000 (`-netdev user,id=net0 -device e1000,netdev=net0`) | NOT validated - driver exists but has zero callers; never wired into the live network stack (COMPLETION_ACTION_PLAN P3) |
| Network   | RTL8139 (`-netdev user,id=net0 -device rtl8139,netdev=net0`) | NOT validated - driver exists but has zero callers; never wired into the live network stack (COMPLETION_ACTION_PLAN P3) |
| USB       | UHCI (`-device piix3-usb-uhci`) | Validated |
| USB       | OHCI (`-device pci-ohci`)  | Validated |
| USB       | XHCI (`-device qemu-xhci`)  | Functional (control) |
| Input     | PS/2 keyboard + mouse | Validated |
| Input     | USB HID keyboard + mouse | Validated |

### QEMU Quick Start

```sh
make run                    # default AHCI + VGA + VirtIO
make run USB=1              # enable USB controller
make run AUDIO=ac97         # AC97 audio
make run AUDIO=hda          # HDA audio
make run NET=e1000          # e1000 NIC
make run SMP=2              # 2 CPU cores
```

### QEMU Boot Validation

Run the automated boot chain validation:

```sh
python3 tools/validate-boot-chain.py --image build/boot.img --report /tmp/boot-report.md
```

### Known QEMU Limitations

- XHCI USB 3.0: control transfers work, mass-storage BOT is functional but slower than UHCI/OHCI
- Cirrus VGA: detection-only, no native modeset
- QXL: detection-only (no native DRM backend yet)
- SMP: AP bring-up (INIT/SIPI) works, but the per-CPU scheduler is gated behind BOOTINFO_FLAG_EXPERIMENTAL_SMP (stage2 key M) and has an open SMP memory-corruption issue (COMPLETION_ACTION_PLAN P4); default boot is BSP-only
- RTL8139 / e1000: drivers are dead code today (no init call sites, no stack attachment); the only live datapath is the private virtio-net in kernel/microkernel/network.c

## Real Hardware Profiles

> STATUS EVIDENCE (2026-10-12 audit): the rows below are UNVERIFIED in this repository.
> No serial logs, validation reports or filled matrix rows back them, and README.md
> still lists real-hardware audio/video/SMP/network as open. Treat every
> "Validated" here as a claim that needs re-proof: attach the boot serial log /
> validate-* report per row (AUDIO_HARDWARE_VALIDATION_MATRIX.md pattern)
> before relying on it. Wi-Fi is worse than unverified: scan/connect are UI
> placeholders with hardcoded SSIDs and no 802.11 data plane at all.

### x86 Laptop (Intel)

| Component | Model | Status |
|-----------|-------|--------|
| CPU       | Core i5-3320M (Ivy Bridge) | Validated |
| Chipset   | QM77 (Panther Point) | Validated |
| Storage   | AHCI SATA SSD | Validated |
| Video     | Intel HD 4000 (i915) | Validated (native modeset) |
| Audio     | HDA Realtek ALC269 | Validated |
| Network   | Intel 82579LM (e1000) | Validated |
| Network   | Intel Centrino 7260 (Wi-Fi) | NOT functional - scan list is hardcoded and "connect" fabricates an IP; no 802.11 auth/assoc/CCMP path exists (COMPLETION_ACTION_PLAN P3) |
| USB       | EHCI + XHCI | Validated (keyboard, storage) |
| Input     | PS/2 Synaptics touchpad | Validated |

### x86 Desktop (AMD)

| Component | Model | Status |
|-----------|-------|--------|
| CPU       | AMD FX-8350 (Vishera) | Validated |
| Chipset   | AMD 970 + SB950 | Validated |
| Storage   | AHCI SATA HDD | Validated |
| Video     | Radeon HD 7850 (Pitcairn) | Validated (native modeset) |
| Audio     | HDA Realtek ALC892 | Validated |
| Network   | Realtek RTL8111 (RTL8139) | Validated |
| USB       | OHCI + EHCI | Validated |

### Real Hardware Limitations

- NVIDIA GPU: nouveau driver is probe-only; no scanout/modeset support yet
- Suspend/resume: not implemented
- Hotplug (USB, PCIe): not implemented; all devices must be present at boot
- Audio capture: not validated on real hardware
- Wi-Fi: no real wireless support at all (placeholder UI only); see COMPLETION_ACTION_PLAN P3

## T61/T400 Physical Campaign Runbook (P0, added 2026-10-19)

The machine itself is the only missing dependency for P0. This section is the
end-to-end procedure; nothing here needs QEMU beyond the artifact check.

### 1. Artifact

```
make          # full green build (validated in-tree by phase6 matrices)
make imb      # -> build/vibe-os-usb.img (raw, FIXED 256 MiB geometry:
              #    MBR + FAT32 boot part LBA 2048 x131072 + raw AppFS LBA 133120 x391168)
```

Honest limitation: on a 32 GiB stick the remaining ~29 GiB stay unallocated;
full-device geometry needs a rebuild with larger total sectors or the
`tools/patch_boot_sectors.py` low-wear flow (see AGENTS.md).

### 2. Prove the exact artifact boots (QEMU, necessary gate)

```
python3 tools/validate_phase6.py --image build/vibe-os-usb.img \
    --report build/phase6-validation-usb-artifact.md \
    --qemu qemu-system-i386 --memory-mb 2048 --timeout 60
```

The `usb-bios-boot` scenario boots this raw image through BIOS USB emulation;
all six scenarios must pass before the image is worth flashing.

### 3. Flash the stick (tvzinha recipe)

The dev host runs stale kernel 6.18.52 whose modules tree was removed by an
in-place upgrade without reboot: `usb-storage` will not load host-side and
there is no `/dev/kvm` (TCG only). Flashing therefore goes through QEMU USB
passthrough with the host's installed 6.18.55 kernel as the guest:

```
sudo -E bash tools/usb_flash_guest/flash_stick.sh build/vibe-os-usb.img
```

The runner FAILS CLOSED unless exactly one Alcor `058f:6387` stick is on the
bus; the guest verifies `/sys/block/sda/device` resolves through a `usb`
path before any write, does `dd bs=1M conv=fsync`, byte-compares the MBR and
prints `RESULT WRITE_OK`. Expect ~4 minutes for 256 MiB under TCG. Once the
host is rebooted into 6.18.55, plain `dd` works and this script is obsolete.
Known gotcha: `build/usb-guest/` is a stale root-owned tree; `make full`
fails on its clean step — ask the maintainer to `sudo rm -rf` it once.

### 4. Boot the T61

- Default posture: boot USB (legacy BIOS), stage2 menu appears. **The
  physical acceptance target #1 is the DEFAULT menu (single CPU)** — SMP is
  opt-in with `M` (BOOTINFO_FLAG_EXPERIMENTAL_SMP) and stays experimental.
- Evidence channels: the kernel debug console writes to VGA text AND COM1
  (0x3F8, 115200 8N1). For logs, either record the screen or attach a
  DB9-USB serial adapter; every line below appears on both.
- **On a hang: press Ctrl+Alt+Del (warm reset), NOT the power button.**
  The bootdebug window at physical 0x1000 survives a warm reset and the
  next kernel boot reports it as `PREV-CRASH`; a cold power cut destroys
  the evidence with the RAM.

### 5. Bootdebug trace codes (the campaign's crash evidence)

The window records a key per phase; on the next warm boot the kernel prints
(serial + VGA):

```
bootdebug: PREV-CRASH len=<n> last=<code> trace=<keys>
```

| Code | Meaning |
|------|---------|
| `S R L` + method chars (`2 F A V B a Q`...) | stage2 init, A20 enable: methods probed/OK (see loader audit above) |
| `M 1 2 C m 3..7` | media reset, disk read attempts/retries, menu, stage2 load steps |
| `n o p q r s t u` | stage2 pre-kernel actions (EDD, handoff) |
| `0..9 A-F` | kernel early stages (BOOTINFO, paging, CPU, video, IDT, timer, input, storage...) |
| `B` `R` | smp bring-up start / smp_init returned |
| `S` `T` | smp_init / trampoline prepared |
| `P` `I` `1` `2` | per-AP: boot-lock, attempt, INIT sent, SIPI sent |
| `a` `l` `g` `i` `m` | AP side: ap_entry, LAPIC, GDT, IDT, AP marked online |
| `O` `F` `D` | AP online confirmed / AP FAILED after 3 retries / bring-up done |

Interpretation: a trace ending at `2` (SIPI sent) without `a` means the AP
never took the trampoline (real-hardware trampoline/SIPI problem — exactly
what this campaign tracks). `m` without `O` = AP entered but stalled before
being counted. `db:` lines: on real silicon the class-#5 watchpoint tool
auto-arms (`db: armed ...`) — QEMU prints `db: unsupported (probe silent)`;
if the T61 shows `db: CTX-WRITE eip=%x cs=%x`, that eip/cs IS the transient
record-field writer the QEMU campaign could never name — capture that line.

### 6. Pass/fail checklist per surface (fill with logs/photos)

| # | Surface | Pass marker | Fail evidence to collect |
|---|---------|-------------|--------------------------|
| 1 | Boot to desktop (UP default) | `desktop: session start`/shell ready | last screen text + PREV-CRASH on warm reboot |
| 2 | Storage write path | `init: storage smoke ok (read+write+restore)` | `storage smoke read/write failed` + trace tail |
| 3 | Cross-boot durability | boot 2: `persistent image valid` WITHOUT rebuild | both screenshots |
| 4 | A20 on IBM BIOS | no `a20_error` halt; loader exits first probe (`2FAVBaQ`) | screen text if halted |
| 5 | SMP (opt-in `M`) | `smp: done, 2 cpus online` | trace tail per table above |
| 6 | Class-#5 writer | (expected on smp only) `db: armed` + `db: CTX-WRITE eip= cs=` if it fires | the exact db: line |
| 7 | Video i915 (gated experimental) | handoff line `video: handoff stage=... backend=drm` | `video:` lines, mode tried |
| 8 | Audio HDA | `hda:` init lines without hang | last audio line + reboot trace |
| 9 | USB runtime | honest failure expected: no native mass-storage backend yet (ENOTTY path) | diagnostic line |
| 10 | Network | honest failure expected: T61's 82567/e1000 has no wired backend (P3 [!]) | diagnostic line |

Rows 9/10 are EXPECTED to report "not supported" — that is the honest
contract (`ENOTTY`/`ENOSYS`, never fake state); they become failures only if
the kernel claims success falsely.
