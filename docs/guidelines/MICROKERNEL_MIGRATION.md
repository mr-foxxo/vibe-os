# Microkernel Migration Plan

## Goal

Transform VibeOS from a monolithic kernel into a service-oriented microkernel architecture with:

- functional BIOS boot path with MBR/VBR compatibility
- real disk partitioning
- FAT32 support for boot and system volumes
- storage support for IDE, SATA, and AHCI
- USB boot compatibility
- smaller privileged kernel core
- drivers and high-level services moved out of the kernel core over time

## Current Reality

The repository is still a monolithic kernel with early drivers linked into the core image. The immediate work is to create a migration path that preserves bootability while we progressively split responsibilities.

This document is a live checklist. Items are only marked complete when code lands in the tree.

Important audit note: in the current tree, a checked item means the migration boundary or ABI milestone exists in code. It does not automatically mean the final end-state backend has already been extracted out of the kernel or that every compatibility fallback has disappeared.

## Phase 0: Foundation

- [x] Create migration document and tracked execution checklist
- [x] Add microkernel process/service/message scaffolding to the kernel tree
- [x] Add service registry bootstrap during kernel init
- [x] Define kernel-to-service ABI versioning
- [x] Define user-space driver/service launch protocol

## Phase 1: Kernel Core Minimization

- [x] Reduce kernel core responsibilities to:
- [x] scheduler
- [x] low-level memory management
- [x] IPC/message passing
- [x] interrupt routing
- [x] process/service supervision
- [x] move filesystem logic behind a service boundary
- [x] move high-level storage management behind a service boundary
- [x] move console/video policy behind a service boundary
- [x] move input policy behind a service boundary
- [x] move socket/network control plane behind a service boundary
- [x] move audio control/data plane behind a service boundary

## Phase 2: Boot Chain

- [x] restore a robust `MBR -> VBR/stage1 -> loader -> kernel` path
- [x] support active partition boot on BIOSes that reject superfloppy layouts
- [x] keep boot-time diagnostics available without text mode dependency
- [x] define boot info handoff contract for the microkernel loader
- [x] add a dedicated loader stage that can load kernel + initial services from FAT32

## Phase 3: Partitioning and Filesystems

- [x] add MBR partition parsing
- [x] add FAT32 reader for boot volume
- [x] add FAT32 writer support where safe
- [x] define system partition layout
- [x] define data/app partition layout
- [x] keep backward-compatibility tooling for current raw image format during migration

## Phase 4: Storage Drivers

- [x] keep legacy IDE path working
- [x] add AHCI controller detection
- [x] add AHCI read path
- [x] add AHCI write path
- [x] add SATA device enumeration through AHCI
- [x] unify storage devices behind one block-device abstraction
- [x] add USB mass-storage boot/loading strategy

## Phase 5: Initial User-Space Services

- [x] storage service
- [x] filesystem service
- [x] display service
- [x] input service
- [x] log/console service
- [x] network service
- [x] audio service
- [x] process launcher / init service

## Phase 6: Compatibility and Validation

- [x] QEMU regression target
- [x] Core 2 Duo regression target
- [x] Pentium / Atom regression target
- [x] BIOSes requiring active MBR partition
- [x] USB boot validation matrix
- [x] IDE/SATA/AHCI validation matrix

## Latest Completed Slice

- `MBR` now relocates itself safely, enables VESA early, selects the active partition, and chainloads a FAT32 `VBR`.
- The FAT32 `VBR` now loads a dedicated `stage2` loader from a reserved-sector slot that does not collide with FAT32 metadata (`FSInfo`, backup boot sector).
- `stage2` now locates `KERNEL.BIN` in the FAT32 root directory and loads it before handing off to the 32-bit kernel entry path.
- The in-kernel storage path now goes through a primary block-device abstraction, with the current ATA driver registered as one backend instead of exposing ATA-specific logic directly to the rest of the kernel.
- The disk image builder now emits a real two-partition image:
  - partition 1: bootable FAT32 volume with `KERNEL.BIN` visible for diagnostics
  - partition 2: raw data volume used by the current AppFS layout
- The kernel storage layer now parses the MBR, discovers the data partition, and exposes it as a logical block volume.
- AppFS, persistence, DOOM assets, and Craft textures are now addressed relative to the data partition instead of absolute disk LBAs.
- A shared `bootinfo` contract now lives in the tree and is populated by the `MBR` for VESA + partition metadata, replacing scattered magic-address parsing in the kernel.
- A new microkernel launch contract now describes bootstrap services/drivers explicitly, captures boot partition metadata for launched tasks, and routes the built-in `init` userland payload through the same scheduler/service bootstrap path intended for later extracted services.
- The launch contract now honors per-service stack sizing and exposes the active launch context to userland through a dedicated syscall, so extracted services can introspect how they were started without scraping boot-time globals.
- Storage syscalls now enter through a microkernel storage-service dispatch layer that resolves the storage service via the service registry, builds request/reply message envelopes, and moves bulk data through kernel-managed transfer buffers instead of raw payload pointers.
- Filesystem syscalls now resolve through a local filesystem service handler with transfer-buffer-backed request/reply dispatch for `open/read/write/close/lseek/stat/fstat`, so the whole current VFS ABI now crosses an explicit service boundary even though the handler still runs in-kernel.
- Video syscalls now resolve through a local video service handler, including text blits, palette transfers, and mode/capability queries, so `syscall.c` no longer talks to the graphics backend directly for the current userland ABI.
- Input and console syscalls now also traverse local service handlers, so keyboard/mouse polling, keymap control, debug output, and text console operations no longer call concrete drivers directly from the syscall table.
- `network` and `audio` now exist as first-class microkernel services with explicit request/reply ABIs and local stub handlers, and VibeOS now exposes BSD-shaped `sys/socket.h`, `sys/uio.h`, and `sys/audioio.h` compatibility headers derived from `compat/sys` so extracted services can preserve familiar contracts without trying to port the BSD kernel wholesale.
- The microkernel service registry now supports process-backed workers over an in-kernel request/reply IPC path, and the current `network`/`audio` stubs run as real scheduled service tasks instead of only inline local handlers.
- `storage`, `filesystem`, `video`, `input`, and `console` now also run as scheduled service workers over that same IPC path, so the main syscall surface no longer resolves through inline handlers for the current service set.
- Transfer buffers now carry explicit owner PID metadata plus per-service grants, so process-backed services only touch request payload memory after the caller shares the specific buffer with read/write permissions.
- The service registry now retains worker launch metadata and can relaunch an offline built-in worker on demand before dispatching a request, adding a first supervision layer without changing the current boot ABI.
- QEMU boot validation now reaches the built-in `init` banner path again after fixing resumed-task register restore in `context_switch`, deferring heavy bootstrap asset discovery / initial FS sync work out of the first boot path, and using direct bootstrap fast paths for serial debug plus legacy text-console syscalls while richer console service plumbing stays available for non-bootstrap request/reply traffic.
- The FAT32 `stage2` loader now retries BIOS extended reads during `KERNEL.BIN` loading, and the current image builder keeps the boot kernel file physically contiguous so the simple linear stage2 path stays reliable while a fully general FAT32 chain loader is still pending.
- Video capability queries now return both the existing mode bitmask and the concrete mode list in the ABI, so userland can enumerate supported resolutions without hardcoding them.
- The kernel now carries only a minimal built-in `init` bootstrap entry plus a tiny AppFS launcher runtime instead of the full embedded userland tree, keeping the BIOS-loaded `kernel.bin` under the low-memory ceiling while apps stay split into independent binaries loaded one at a time.
- Headless QEMU boot now reaches a real external AppFS userland jump again: the bootstrap `init` path loads `hello.app`, enters `vibe_app_entry`, runs `vibe_app_main`, and returns to `init` over the current loader ABI instead of stopping before the first external app call.
- The AppFS external-app arena now lives below the low-memory kernel-heap fallback, and the FS persistence image moved off the bootstrap stack into static scratch storage, removing the memory overlap / stack-clobber failures that were corrupting `scheduler_current()` during early userland bring-up.
- Built-in services now degrade more intelligently when the worker transport fails: request dispatch can fall back to the in-kernel local handler for built-in services, and input syscalls also fall back to direct keyboard/mouse driver reads so the post-boot interactive path keeps working while the process-backed worker path is still being stabilized.
- Built-in service supervision now also treats transport degradation as a restart signal: the registry can terminate and relaunch the stale worker from saved launch metadata, clear the degraded state after a healthy reply, and keep the current built-in compatibility fallback as a safety net instead of the permanent end state.
- Headless QEMU validation now survives past the first post-boot input poll without reply-failure spam: the current path still shows one degraded input request, then relaunches the input worker and stays stable through a longer soak after the external `hello.app` jump returns.
- The image pipeline now preserves the current raw AppFS/persistence/asset layout as a first-class standalone artifact again: `make legacy-data-img` emits a raw `build/data-partition.img`, and the partitioned boot image now consumes that same blob instead of rebuilding the data layout through a separate ad-hoc path.
- FAT32 boot-volume writing is now generalized beyond a single diagnostic kernel copy: the image builder can safely populate the boot partition through `mtools`, and the default image now exports `KERNEL.BIN`, `STAGE2.BIN`, `LAYOUT.TXT`, and `DATAINFO.TXT` into the visible FAT32 volume for inspection and migration debugging.
- `userland.c` now also builds as an external `userland.app`, and the built-in `init` bootstrap autostarts that AppFS payload on boot before falling back to the tiny rescue shell if the app is missing or returns.
- `mkdir` is now shipped as an independent AppFS app adapted from `compat/bin/mkdir`, with the shell preferring the external app path before using the old builtin fallback.
- Headless QEMU validation on the current Phase 1 path now reaches the external boot shell too: the serial log shows `init -> lang_try_run(userland) -> app: runtime init ok -> userland.app: shell start`, confirming that the minimized kernel boots into the AppFS userland payload instead of relying on a monolithic in-kernel shell image.
- Residual risk during that headless path: the first interactive input request can still log one `service: reply send failed ... type=66` while the input worker transport degrades and recovers through the existing local fallback/supervision path; Phase 1 is considered complete because the service boundary, supervision path, and boot-to-userland handoff are all active, but richer extraction/cleanup remains future work.
- The SMP scheduler now tracks per-task CPU preference/history, distributes new tasks toward the least-loaded online core, and filters runnable selection so the same process is not dispatched simultaneously on multiple CPUs during multiprocessor bring-up.
- The LAPIC SMP bring-up path now avoids waiting on PIT ticks with interrupts masked during `INIT/SIPI`, and QEMU `-smp 4` currently reaches `smp: online cpus=4/4` before returning to the `init` bootstrap path.
- QEMU validation currently reaches the kernel and userland path with early debug markers:
  - `M a b d V J S L 2 F K A P 10 11 12 13 14 15 16 17`
- Phase 4 storage driver work is now wired through a single backend-selection point: the kernel probes PCI for AHCI first, enumerates implemented SATA ports, identifies a SATA disk through the AHCI path, and falls back to the legacy ATA PIO backend when no AHCI controller/device is present.
- IDE and AHCI storage now both pass the same headless bootstrap smoke test: the serial log shows `init: storage smoke begin`, a read request (`type=16`), a write request (`type=17`), and `init: storage smoke ok` before userland handoff, while the `q35` + `ahci` scenario also reports `ahci: controller 0:3.0 pi=3f`, `ahci: sata port=0 total=524288 start=133120 sectors=391168`, and `storage: using ahci backend`.
- The boot FAT32 volume now also exports `BOOTPOLICY.TXT`, documenting the current USB mass-storage loading strategy for the migration: BIOS `INT 13h` remains the transport for USB boot media through `MBR -> VBR -> stage2 -> KERNEL.BIN/STAGE2.BIN`, while the runtime storage layer probes `AHCI` first and then falls back to legacy `ATA`; native USB mass-storage drivers and hardware validation remain Phase 6 work.
- Phase 5 initial services now run as distinct user-space service hosts instead of the old in-kernel worker loop: `storage`, `filesystem`, `video`, `input`, `console`, `network`, and `audio` each launch through the microkernel bootstrap path as their own scheduled service process, and headless QEMU boot shows `service-host: online <name>` for all seven before `init` starts.
- The current extraction shape uses a deliberate compatibility bridge: each user-space service receives IPC in userland, then invokes the preserved per-domain kernel local handler through a dedicated backend-shim syscall, so transport ownership and lifecycle now live in user space while the older domain logic remains available for supervision fallback and incremental future extraction.
- Phase 6 validation is now codified in-tree instead of living only in ad-hoc shell history: `make validate-phase6` runs a headless QEMU matrix across default IDE, `core2duo`, `pentium`, `n270`, `q35`+`ahci`, and USB mass-storage BIOS boot, then writes `build/phase6-validation.md` with the observed markers and MBR metadata.
- The legacy ATA path is now hardened for compatibility validation: the backend performs 400 ns alternate-status delays, serializes sector transactions with `irqsave` locking, and the bootstrap smoke test now uses a non-AppFS persistence sector so IDE validation no longer trashes the external app catalog while probing read/write health.
- The AppFS loader now retries directory and app-image reads before giving up, and the ATA partition resolver now prefers the bootloader-published data-partition geometry from `BOOTINFO` before falling back to runtime MBR parsing. With that fix, the current IDE/Core2/Pentium/Atom and `q35` + `ahci` QEMU matrix all reach the external `userland.app` shell again; the built-in rescue shell remains a safety net, not the expected steady-state path.
- The image validation path now also asserts the active MBR partition bit directly from `boot.img`, so the builder will fail Phase 6 regression if the BIOS-facing `MBR -> active FAT32 partition -> VBR -> stage2` chain drifts back toward a superfloppy layout.
- The USB validation matrix now records the current migration boundary explicitly: under QEMU `usb-storage`, SeaBIOS boots the image through the same active-partition chain and reaches the built-in bootstrap shell, while runtime block-device access still reports `storage: no block device backend available` until a native USB mass-storage service exists.
- The ported-userland build path is now stable under the official regression target too: `Build.ported.mk` exposes a single deterministic `ported-all` goal, `make validate-phase6` completes successfully again, and the generated `build/phase6-validation.md` reflects the same boot markers used by the migration plan.

## Audited Remaining Gaps

These are the items that still prevent the migration from being considered fully finished end-to-end, even though the phase checklist above is green:

- Native USB mass-storage runtime support is still missing. USB BIOS boot works for `MBR -> VBR -> stage2 -> kernel`, but once the kernel takes over there is still no native USB block backend.
- The extracted service model still uses a compatibility bridge. `storage`, `filesystem`, `video`, `input`, `console`, `network`, and `audio` run as user-space service hosts, but the preserved backend logic is still invoked through the kernel-side local handlers via the backend-shim syscall.
- `network` and `audio` are not feature-complete services yet. Their current implementations provide ABI shape, supervision, and service lifecycle, but not a real NIC packet path, socket stack, audio DMA pipeline, or mixer/playback backend.
- The current `stage2` path is still a pragmatic FAT32 loader, not a fully general filesystem loader. It is reliable for the current image layout, but it still relies on the current contiguous/linear loading strategy documented above.

## Document Reality Audit

The statements below classify how much of this document is literally true in the current tree.

### Verified As Implemented In The Current Tree

- Phase 2 boot-chain claims are materially true: `MBR -> active FAT32 partition -> VBR -> stage2 -> KERNEL.BIN` exists and is validated in QEMU.
- Phase 3 disk-layout claims are materially true: the image is partitioned, FAT32 is used for the boot volume, and the raw data/AppFS partition is built and consumed consistently.
- Phase 4 ATA/AHCI claims are materially true: both backends exist, are selected behind one block-device abstraction, and pass the current headless smoke/boot matrix.
- The `init -> userland.app` handoff is materially true on the current IDE/AHCI QEMU paths.
- Phase 6 validation infrastructure is materially true: `make validate-phase6` produces `build/phase6-validation.md` and the matrix currently passes.

### True Only As A Migration Boundary, Not As Final Extraction

- Phase 1 service-boundary claims are only partially true in the strong microkernel sense: the syscall/IPC/service boundaries exist, but much of the concrete backend logic for `storage`, `filesystem`, `video`, `input`, and `console` still lives in kernel-side local handlers.
- Phase 5 initial user-space service claims are true for transport, lifecycle, and supervision, but not yet for backend ownership. The service hosts are real user-space tasks, but they still call back into preserved kernel handlers through the backend-shim syscall.
- USB compatibility is true only for BIOS boot/loading strategy. Native runtime USB block I/O is still missing.

### Not Yet True If Read Literally As End-State Claims

- VibeOS is not yet a fully extracted service-oriented microkernel in the strict sense. The repository is currently a hybrid system with real service boundaries plus compatibility bridges back into in-kernel handlers.
- `network` is not yet a real networking stack/service. It is currently a query/capability stub with request ABI scaffolding.
- `audio` is not yet a real playback/capture service. It is currently a query/control stub with no real DMA/ring/mixer backend.
- The boot loader is not yet a fully general FAT32 loader. It is a robust current-path loader tuned to the current image layout.

## Stub / Bridge Inventory

This is the current list of migration-relevant stubs, bridges, and fallbacks that still need proper implementation.

### User-Space Services That Still Call Back Into Kernel Handlers

- `storage` service host:
  current user-space process exists, but the request body is still ultimately handled by the kernel-side local storage handler through `sys_service_backend()`.
- `filesystem` service host:
  current user-space process exists, but file operations still terminate in the preserved kernel-side local handler.
- `video` service host:
  current user-space process exists, but rendering/mode backend logic still lives behind the kernel-side local handler.
- `input` service host:
  current user-space process exists, but keyboard/mouse handling still depends on the kernel-side local handler and also has a direct-driver fallback path.
- `console` service host:
  current user-space process exists, but the console backend still resolves to the preserved kernel-side local handler.
- `network` service host:
  current user-space process exists, but its backend is still a stub local handler.
- `audio` service host:
  current user-space process exists, but its backend is still a stub local handler.

Relevant code:

- `userland/bootstrap_service.c`
- `kernel/microkernel/service.c`

### Kernel-Side Service Stubs

- `kernel/microkernel/network.c`
  - marks the service as `QUERY_ONLY`
  - reports socket families/types as capability metadata only
  - returns failure for real socket/bind/connect/send/recv/setsockopt/getsockopt operations
- `kernel/microkernel/audio.c`
  - reports a device called `stub`
  - marks the service as `QUERY_ONLY`
  - has parameter/status bookkeeping only
  - returns failure for real audio read/write and mixer operations

### Direct Fallback Paths Still In Use

- `kernel/microkernel/input.c`
  - still falls back to direct keyboard/mouse driver reads when service transport is degraded
- `kernel/microkernel/service.c`
  - still falls back from process transport to local handlers when transport is degraded
- `userland/bootstrap_init.c`
  - still retains the built-in shell fallback if `userland.app` is missing or returns

### Shell / Userland Stubs Still Present

- `userland/modules/busybox.c`
  - weak fallback implementations still exist for:
    - CPU/APIC topology helpers
    - heap/RAM reporting helpers
    - `vibe_lua_main`
    - `sectorc_main`
    - desktop/editor launch entrypoints
  - `startx`, `edit`, and `nano` are still unavailable in the boot app unless an external app is found
  - `lua` and `sectorc` still report `indisponivel` when neither an external app nor a linked runtime is present
  - built-in `uname` still returns a fixed string (`VIBE-OS`) rather than a full compat-style implementation

### VFS Execution Discovery Stubs

- `userland/modules/fs.c`
  - bootstraps empty placeholder files in `/bin`, `/usr/bin`, and `/compat/bin` for command discovery
- `userland/modules/lang_loader.c`
  - still treats those placeholder paths as runtime stubs so the shell can discover external apps before native VFS execution exists

This is acceptable as a migration bridge, but it is not the final shape for native executable discovery/execution.

### Core Kernel No-Op Stubs Still In Tree

- `kernel/memory/paging.c`
  - `paging_init()` is still a no-op placeholder
- `kernel/memory/memory_init.c`
  - `memory_subsystem_init()` is still only a thin stub wrapper around lower-level init calls
- `kernel/hal.c`
  - `hal_init()` is still empty

These are not the main blockers for the current microkernel migration path, but they are real stubs and the document should not pretend otherwise.

## Stub Implementation Priority

Recommended order for replacing the current migration stubs with proper implementations:

1. Native USB mass-storage runtime backend
2. Remove the backend-shim dependency for `storage`
3. Remove the backend-shim dependency for `filesystem`
4. Remove the backend-shim dependency for `video`, `input`, and `console`
5. Implement a real extracted `network` backend (preferably a narrow QEMU-friendly NIC first)
6. Implement a real extracted `audio` backend (DMA/ring/mixer path)
7. Replace VFS executable placeholder files with native executable metadata/lookup
8. Replace the built-in `uname` stub and boot-app-only `startx`/editor fallback gaps with proper external apps or linked runtimes
9. Generalize the FAT32 `stage2` loader beyond the current contiguous/linear strategy
10. Replace remaining kernel no-op stubs such as paging/HAL initialization as part of core cleanup

## First Implementation Slice

The first slice implemented in-tree is intentionally modest:

1. Introduce structured message envelopes.
2. Introduce a service registry owned by the kernel.
3. Extend `process_t` so services can be represented explicitly.
4. Keep the existing monolithic behavior while creating the interfaces needed for extraction.

This does not finish the migration, but it starts replacing ad-hoc coupling with explicit microkernel-oriented primitives.
