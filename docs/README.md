# Docs Map

**VibeOS** — A service-oriented microkernel operating system for x86 (i386), with real hardware drivers, a lightweight GUI, and microkernel-based services.

## Project Status

Kept honest against executed evidence (COMPLETION_ACTION_PLAN P9). A ✅ means
a report/log in `build/` or a `make validate-*` target backs it; ⚠️ means code
exists but is unverified or partial; ❌ means the claim was false (facade or
dead code) with the gap tracked in the open-work backlog:
[`guidelines/DEFINITIVE_CLOSURE_PLAN.md`](guidelines/DEFINITIVE_CLOSURE_PLAN.md)
(the consolidated plan that absorbed every other plan — waves 0–9, anti-stub rule).

| Component | Status |
| --- | --- |
| Boot chain (MBR → VBR → Stage2 → Kernel) | ✅ `validate-phase6-boot-smoke` 6/6 (QEMU); real-BIOS chain audited (hw_validation.md) |
| Memory subsystem (free-list allocator, 4GB physmem, 4KB paging) | ✅ Heap header guards/canaries, reserved ranges |
| Storage: ATA, AHCI | ✅ `validate-phase6` ide + ahci scenarios; persistence `validate-storage-persist` |
| Storage: NVMe, USB BOT (runtime) | ⚠️ Code present, probed after USB init; no validation report — unverified |
| Network: virtio IPv4 client (DHCP/ARP/ICMP, reachable) | ✅ `validate-network-datapath` PASS: DHCP bound, gateway ICMP roundtrip, inbound ARP/ICMP responders, TX `drop=0` (pcap evidence) |
| Network: UDP/TCP client | ⚠️ UDP = DHCP only; TCP SYN/ACK path exists but unvalidated (P3) |
| Network: e1000, rtl8139 | ❌ Dead code — zero callers outside their own file (P3 camada 1) |
| Wi-Fi (Intel 7260 / iwlwifi) | ❌ Facade — fixed BDF 0:0:0, hardcoded scan list, fabricated connect (P3 camada 3) |
| Wi-Fi WPA2 crypto module (PBKDF2/PTK/4-way) | ⚠️ Real crypto but orphaned — no 802.11 auth/assoc or CCMP data path |
| Audio: Intel HDA, AC97, pcspkr (QEMU) | ✅ `validate-audio-*` in QEMU |
| Audio: real hardware | ❌ Open — hardware matrix not filled (P5, hardware-gated) |
| Input: PS/2 keyboard, mouse | ✅ Desktop session validated in QEMU |
| Video: LFB, VESA | ✅ QEMU validated |
| Video: native DRM (BGA, i915, radeon, nouveau) | ⚠️ i915 commit gated by `INTEL_I915_EXPERIMENTAL_COMMIT`; real-silicon campaigns open (P6) |
| Desktop GUI (microui, dynamic desktop, taskbar, applets) | ✅ QEMU validated |
| Microkernel services (IPC, restart, events, async) | ✅ Phases A-G |
| ABI compatibility (BSD/POSIX shapes, ELF32, sockets, termios) | ✅ `validate-abi-compat-surface` / `validate-abi-final` |
| USB substrate (UHCI/OHCI/EHCI/XHCI discovery, device descriptors) | ⚠️ UHCI discovery+init runs in QEMU; controller-count query reports 0 (inconsistent) |
| POSIX/Compat layer (BSD libc, pthreads, sockets) | ⚠️ Partial — ABI surface validated; route/netstat/ifconfig are snapshots (P3/P8) |
| POSIX Graphics X-Like compatibility | 🔄 In progress |
| SMP bring-up | 🔄 In progress — 2-CPU bring-up OK; real-time SMP2 has an open timing race (P4, non-gating for UP hardware boot) |
| Craft (voxel game) | 🔄 In progress |

## Layout

This directory is split into two kinds of documentation:

- technical reference docs in `docs/`
- planning, migration, and historical guidance docs in `docs/guidelines/`

Use this file as the entry point so we do not keep drifting copies of the same plan in both places.

## Start Here

If you want the code-guided architecture walkthrough, read these first:

- [overview.md](overview.md)
- [workflow.md](workflow.md)
- [mbr.md](mbr.md)
- [stage1.md](stage1.md)
- [stage2.md](stage2.md)
- [memory_map.md](memory_map.md)
- [kernel_init.md](kernel_init.md)
- [drivers.md](drivers.md)
- [runtime_and_services.md](runtime_and_services.md)
- [apps_and_modules.md](apps_and_modules.md)

## Active Plans In `docs/`

These are active project plans or validation-oriented documents that still make sense as top-level docs:

- [FINALIZATION_EXECUTION_PLAN.md](FINALIZATION_EXECUTION_PLAN.md)
- [guidelines/COMPLETION_ACTION_PLAN.md](guidelines/COMPLETION_ACTION_PLAN.md) — unified completion plan (audited 2026-10-11)
- [NETWORK_AUDIO_PANEL_PLAN.md](NETWORK_AUDIO_PANEL_PLAN.md)
- [VIDEO_BACKEND_REWRITE_PLAN.md](VIDEO_BACKEND_REWRITE_PLAN.md)
- [ICON_THEME_IMPLEMENTATION_PLAN.md](ICON_THEME_IMPLEMENTATION_PLAN.md)
- [CRAFT_OPTIMIZATION_PLAN.md](CRAFT_OPTIMIZATION_PLAN.md)
- [AUDIO_HARDWARE_VALIDATION_MATRIX.md](AUDIO_HARDWARE_VALIDATION_MATRIX.md)
- [KNOWN_BUGS.md](KNOWN_BUGS.md)

## Guidelines In `docs/guidelines/`

These are migration plans, compatibility plans, build notes, or historical implementation guides. They are still useful, but they are not the primary code-reference docs:

- [guidelines/README.md](guidelines/README.md)
- [guidelines/MICROKERNEL_MIGRATION.md](guidelines/MICROKERNEL_MIGRATION.md)
- [guidelines/QUICK_BUILD.md](guidelines/QUICK_BUILD.md)
- [guidelines/COMPAT_PLAN.md](guidelines/COMPAT_PLAN.md)
- [guidelines/COMPAT_PLAN2.md](guidelines/COMPAT_PLAN2.md)
- [guidelines/COMPAT_SYS_REUSE.md](guidelines/COMPAT_SYS_REUSE.md)
- [guidelines/MODULAR_APP_INTEGRATION_PLAN.md](guidelines/MODULAR_APP_INTEGRATION_PLAN.md)
- [guidelines/VIBELOADER_PLAN.md](guidelines/VIBELOADER_PLAN.md)
- [guidelines/smp.md](guidelines/smp.md)

## Canonical Location Rules

- If a document explains how the current code works, keep it in `docs/`.
- If a document is a long-lived plan, migration checklist, compatibility note, build note, or historical handoff, keep it in `docs/guidelines/`.
- If a document is an active execution plan tightly tied to current product work or validation campaigns, it can stay in `docs/`.
- Do not keep full duplicated copies across both locations.
- If an old path is still convenient for editors or bookmarks, keep only a short pointer file there.
