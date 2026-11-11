# Microkernel Migration Plan

## Current Architecture Snapshot

The repository is already in the hybrid microkernel migration stage: the kernel boots, launches service hosts, and runs the desktop/userland stack through explicit launch and supervision paths, but several domains still keep compatibility shims or kernel-owned backend state.

This document now keeps the whole active migration picture near the top. The completed baseline checklist that was previously here lives at the end of the file so open architecture work is easier to scan first.

### Target End-State

- functional BIOS boot path with MBR/VBR compatibility
- real disk partitioning
- FAT32 support for boot and system volumes
- storage support for IDE, SATA, and AHCI
- USB boot compatibility
- smaller privileged kernel core
- drivers and high-level services moved out of the kernel core over time

### Current Reality

- the repository still carries a hybrid kernel with preserved compatibility paths while services are extracted incrementally
- checked items in this document mean the migration boundary or ABI milestone exists in-tree, not that every final backend has already moved out of the kernel
- this migration slice still does not need to block the other planning documents; reconcile those separately after the architectural cleanup in this file

## Current Open Work

This section stays near the top on purpose: open migration work first, completed work at the end of the file.

- Phase D is now delivered in-tree as of 2026-03-30: `vidmodes-shell`, `video-restart-desktop`, and `video-restart-mouse-desktop` all pass against `build/boot.img`, so the desktop/present split is no longer the blocking video item.
- Phase E is now delivered in-tree as of 2026-03-30: `storage` / `filesystem` steady-state requests now run through dedicated user-space loops, persistence writeback advances incrementally under `fs_tick()`, and native executable lookup no longer depends on placeholder files in `/bin` / `/usr/bin` / `/compat/bin`; the rescue-shell Phase E matrix is green, while desktop terminal launch still shares the known `videosvc` reset instability tracked outside this phase.
- The Phase G transport/containment slice is now delivered in-tree as of 2026-03-30: steady-state service execution no longer goes through the generic backend-shim bridge, `make validate-phase-g` now covers rescue-shell boot plus keyboard/mouse restart recovery for `input` / `audio` / `network` / `video`, and the rescue-shell writeback regression is now repeatable again.
- As of 2026-03-31, the desktop input path no longer arms the per-device compatibility queues during ordinary stream idle. Compat input is now reserved for explicit `inputsvc` reset recovery, which stops duplicated keys/clicks from making the start menu and other shell popups close themselves.
- As of 2026-03-31, the desktop/video steady-state path no longer keeps a desktop-class local fast path: `gfx`, `present submit`, mode-set/leave, palette/info, and present-policy/copy-override control all route through `videosvc`, while the preserved privileged backend boundary stays limited to the narrow kernel-side hardware mediation inside `video.c` / the backend driver layer.
- As of 2026-03-31, the QEMU-stable boot path still enters through the direct `legacy_lfb` framebuffer backend first, but the runtime `fast_lfb` shadow-backbuffer path now has explicit headless QEMU proof: `desktop-visual-proof` waits for `desktop: visual ready`, confirms `video: backend refresh backend=fast_lfb` plus `video: shadow backbuffer enabled`, captures a QEMU screenshot, and checks visible/non-black desktop UI samples instead of trusting serial markers alone.
- As of 2026-03-31, the 8-bit blit/present path no longer assumes `src_width == source pitch` when the source points at the live backbuffer. Kernel fullscreen presents, stretch paths, and the client-side `videosvc` upload helpers now repack backbuffer-backed sources row-by-row before crossing the service boundary, removing a row-stride truncation bug from `fast_lfb`/shadow-backbuffer flows and internal video microbench uploads. The remaining `fast_lfb` gap is wider proof across more modes and real hardware, not a reproduced black-screen failure in headless QEMU.
- As of 2026-03-31, the native DRM driver layer now shares an explicit lifecycle contract across `native_gpu_bga`, `native_gpu_i915`, `native_gpu_radeon`, and `native_gpu_nouveau`: the dispatcher routes `probe`, `set_mode`, `revert`, `forget`, and `prepare-for-BIOS` through backend ops instead of backend-specific recovery switches, `tools/validate_gpu_backends.py` now also checks that source-level contract directly, and driver containment stays aligned with the newer service/backend split even though `nouveau` still remains probe-only until its first real modeset lands.
- As of 2026-03-31, the canonical in-tree video migration matrix is now explicit: `make validate-phase-d` bundles four Phase D desktop scenarios (`desktop-visual-proof`, `vidmodes-shell`, `video-restart-desktop`, `video-restart-mouse-desktop`) with the GPU/backend audit in `tools/validate_gpu_backends.py`, and that driver audit now distinguishes the conservative direct boot path (`legacy_lfb`), the validated runtime shadow-backbuffer path (`fast_lfb`), real native DRM backends (`native_gpu_bga`, `native_gpu_i915`, `native_gpu_radeon`, `native_gpu_nouveau`), and detection-only unsupported labels (`native_gpu_qxl`, `native_gpu_vmware`, `native_gpu_cirrus`, `native_gpu_virtio`, `native_gpu_intel_legacy`, `native_gpu_intel_unsupported`).
- As of 2026-03-31, `make validate-video` now exists as the one-shot video/driver gate: it runs `validate-phase-d` first and then `validate-gpu-backends-recovery`, so desktop presentation, screenshot-backed `fast_lfb` proof, restart recovery, and the driver-side `revert` / `prepare-for-BIOS` handoff path regress together.
- As of 2026-03-31, the Phase C capture path now has its first real event-driven userland slice: the shared audio mailbox publishes `capture-ready` / `capture-xrun`, `soundctl record` waits on that stream instead of busy-yielding when capture is empty, and `tools/validate_audio_stack.py` now requires a concrete `soundctl: capture-ready=` marker during capture smoke coverage.
- As of 2026-03-31, the QEMU-facing Phase C desktop boot path is materially more deterministic: AC97 compatibility init now falls back to a bounded codec-register probe when `AUICH_PCR` never asserts, audio service-init telemetry reports the resolved `backend=<name> config=<device>` pair directly, and desktop boots defer boot-time `netmgrd reconcile` until the session only when an auto-connect SSID is configured. `tools/validate_audio_stack.py` passed four consecutive desktop runs on `build/boot.img` (`audio-stack-validation-network-defer-smoke-2` plus `...repeat-1..3`) with stable `ich-aa` backend telemetry, `host: desktop session launched`, and foreground `audiosvc` completion markers; the remaining Phase C work is queue ownership/policy extraction, not a reproduced QEMU backend-selection flake.
- As of 2026-03-31, Phase C is now delivered in-tree as the audio async dataplane milestone: startup sound launches through detached audio workers, steady-state desktop/audio clients submit through `audiosvc` plus the shared audio event stream instead of advancing backend state from UI cadence, capture-ready/xrun notifications wake `soundctl record`, and mixer/default-route state is exported/applied through `audiosvc` userland policy files; `make validate-phase-c` now aliases the QEMU audio stack gate so playback, capture, backend telemetry, and desktop responsiveness regress together.
- As of 2026-03-31, the first post-Phase-C audio tightening slice is also explicit in-tree: the shared audio feature flags now advertise `control-owner=audiosvc`, `backend-executor=kernel`, and `ui-progress=decoupled`, both `audiosvc` and `soundctl` emit that boundary in telemetry/status output, and `validate-phase-c` now fails if that ownership contract disappears during follow-on refactors.
- As of 2026-03-31, the first post-Phase-F/G network tightening slice is also explicit in-tree: the shared network capability flags now advertise `event-stream=mailbox`, `backend-events=rx-tx`, `steady-state=service-host`, `ownership=policy-only`, `fallback=rescue-only`, and `datapath-executor=kernel`, while `netmgrd status` plus `/runtime/netmgrd-status.txt` export the same contract for `taskmgr`/desktop diagnostics. This does not claim a real NIC datapath yet; it makes the remaining hybrid boundary auditable before the real TX/RX queue work lands.
- As of 2026-03-31, that network boundary is now explicit in the user-facing CLI too: the shared capability flags additionally distinguish `packet_path=real|telemetry-only|none` and `socket_scope=network|local-only`, `netmgrd status` exports those fields, and the in-tree terminal tools (`netstat`, `ping`, `host`, `dig`, `ftp`, `curl`) now fail explicitly when the tree still only has control-plane or local-socket behavior instead of implying a full remote TCP/DNS stack.
- As of 2026-04-01, the generic service fallback bridge is tighter in-tree: `kernel/microkernel/service.c` now retries degraded requests by marking the service offline/degraded and restarting the worker once before any local-handler escape hatch is considered, and that escape hatch is now restricted to rescue/bootstrap-oriented `storage` / `filesystem` / `console` flows instead of remaining available to interactive domains.
- As of 2026-04-01, the steady-state input path no longer consumes direct kernel fallback queues from ordinary clients when `inputsvc` transport degrades. `kernel/microkernel/input.c` still lets the service worker own the raw kernel queue, but non-service callers now stay on the explicit service boundary instead of silently tunneling back to direct dequeue helpers.
- As of 2026-04-01, the clean-worktree validation picture is now much clearer: `build/kernel.bin` and `make validate-phase-c` both pass, `make validate-video` is down to a single remaining `vidmodes-shell` miss around the 800x600 step, and `make validate-phase-g` is down to two rescue-shell command-entry failures (`grep-explicit-path` and `phase-e-writeback-shell`) after the stale desktop/rescue boot markers in `tools/validate_modular_apps.py` were reconciled with the runtime markers the tree actually emits today.
- The remaining hard video boundary is backend ownership tightening plus event-stream hardening. GPU/MMIO privilege, backend coordination, and the eventual split between minimal privileged backend execution and user-space service ownership remain later-phase work, and the still-observed `desktop: video event overflow` bursts under heavier mode-change/restart churn are now a telemetry/backpressure issue rather than a hidden local-fast-path dependency.
- The active unfinished migration queue near the top should now be read as Phases F/G plus the remaining backend-ownership tightening called out in the audited gaps; completed delivery evidence stays near the end of the file.
- This migration slice still does not need to block the other planning documents. Reconcile those separately after this architectural cleanup.

## Audited Remaining Gaps

These are the items that still prevent the migration from being considered fully finished end-to-end, even though the baseline phase checklist at the end of the file is green:

- Native USB mass-storage runtime support is still missing. USB BIOS boot works for `MBR -> VBR -> stage2 -> kernel`, but once the kernel takes over there is still no native USB block backend.
- The extracted service model still uses compatibility boundaries in important places, but the steady-state transport cutover is much further along. `storage`, `filesystem`, `console`, and `network` now run through dedicated user-space request loops in steady state without the generic backend-shim bridge, while `video` and `audio` still depend on kernel-owned backend state and `storage` / `filesystem` / `console` still retain rescue-oriented local fallback. `input` no longer keeps the previous ordinary-client direct fallback path, but backend ownership is still not fully extracted even though crash-containment contracts and restart validation are now explicit. The remaining in-tree QEMU validation misses are now narrow: one `vidmodes-shell` 800x600 substep plus two rescue-shell command-entry scenarios.
- `network` and `audio` are not feature-complete services yet. Their current implementations provide ABI shape, supervision, and service lifecycle, but not a real NIC packet path, socket stack, audio DMA pipeline, or mixer/playback backend.
- The current `stage2` path is still a pragmatic FAT32 loader, not a fully general filesystem loader. It is reliable for the current image layout, but it still relies on the current contiguous/linear loading strategy documented above.

## Event-Driven Async Migration Plan

This is the concrete migration path from the current hybrid service model toward a genuinely event-driven microkernel.

The target end-state is:

- user-facing apps never block the desktop/UI loop on storage, input, network, audio, or video backend progress
- every long-lived subsystem owns an explicit event queue and worker context
- syscalls become enqueue/subscribe/ack style boundaries where possible, instead of synchronous "do the whole job now" traps
- service hosts stop calling back into preserved kernel local handlers for steady-state operation
- the privileged kernel shrinks toward scheduling, memory, IPC, interrupt routing, supervision, and a minimal hardware isolation layer

### Architecture Principles

- queue work, do not run full device transactions in UI-facing code paths
- prefer request submission + progress notification over synchronous polling loops
- separate control plane from data plane for audio, video, network, and storage
- separate input capture from desktop rendering, and desktop rendering from presentation
- allow degraded services to fail independently without taking the desktop event loop down with them
- treat compatibility bridges as temporary migration shims, not permanent architecture
- treat the desktop session as the primary user-facing workload regardless of CPU count
- treat mouse and keyboard responsiveness as irrevocable requirements
- run non-desktop work in independent async worker contexts that generate auditable events
- run `userland.app`, shell, desktop, and foreground apps outside the bootstrap/main thread in their own independent worker/thread contexts
- reduce the bootstrap/main thread to supervision, event routing, and recovery orchestration only
- let supervision restart non-desktop services instead of allowing them to stall desktop responsiveness

### Non-Negotiable UX Contract

- the desktop must remain responsive even when optional services are degraded
- pointer motion, clicks, and keyboard input must not depend on audio, storage, video, or network backends making progress
- the shell, desktop session, and userland apps must not monopolize or live inside the bootstrap/main thread
- startup sound and background work may fail fast; desktop, mouse, and keyboard may not
- scheduler and supervision policy should explicitly favor desktop/input continuity over optional background throughput
- service failures should trigger restart/isolation semantics, not UI-wide stalls

### Priority Order

Everything should run in separated async worker/service contexts, but not with equal priority.

Priority tiers for scheduling, event dispatch, supervision, and restart policy:

1. desktop session continuity
2. keyboard input
3. mouse / pointer input
4. compositor / frame present / video session continuity
5. core storage/filesystem operations needed by the foreground session
6. audio playback/capture
7. network datapath and background daemons
8. foreground/background apps outside desktop/shell
9. optional/background work

Rules:

- a lower-priority worker must never be allowed to stall a higher-priority one
- desktop, keyboard, and mouse paths should always retain forward progress first
- standalone apps must not outrank network service continuity; apps come after network in the policy order
- when the system is overloaded, optional/background work should be throttled before foreground interactivity is touched
- restart policy should favor recovering lower-priority services around a still-live desktop instead of restarting the whole session
- priority tiers `5+` (`audio`,`network`, `app`, and `background`) must never be allowed to compromise BIOS boot completion, `userland.app` bootstrap, or `startx` session bring-up
- if a `5+` worker wedges the system during migration, the recovery policy should kill that worker first and continue preserving tiers `1..4` instead of sacrificing desktop/session continuity

### Phase A: Kernel Event Primitives

- [x] process/service bootstrap and request/reply IPC exist
- [x] service supervision/restart exists in initial form
- [x] cooperative execution points exist through `yield`/`sleep`
- [x] timer-driven tick hooks now exist as an initial event substrate and now drive waitable timeout wakeups
- [x] add first-class kernel event objects (`queue`, `waitable`, `signal`, `completion`)
- [x] add per-service event mailbox/ring abstraction instead of ad-hoc request transport only
: the kernel now has a reusable waitable-backed `kernel_mailbox` primitive, and task-lifecycle plus service/audio/video/network event delivery all flow through that shared mailbox layer instead of bespoke subscriber rings
- [x] add timeout/cancel primitives for pending work items
- [x] add non-busy wait/wakeup path so services stop relying on `yield`/poll loops
- [x] add subscription model for async completion and state-change notifications
: service supervision and task-lifecycle subscriptions now expose stable lifecycle delivery with sequence IDs, task-class filters, and per-class backlog telemetry, while audio/video/network keep their domain streams for richer later-phase completions
- [x] subsystem event streams now also surface ring pressure through per-event `dropped_events` telemetry for audio/video/network consumers, and task-lifecycle events now also report per-class backlog/dropped state so queue overruns stop disappearing silently during restart/degradation work
- [x] define scheduler-visible event metadata so the kernel can audit pending events and prioritize desktop/input-critical work
- [x] define one independent async worker/thread context per major task class instead of reusing UI loops as pumps
- [x] establish explicit task-class metadata for launched workers before the final mailbox split
: launch contexts, task snapshots, task-lifecycle events, and the scheduler-owned task-class mailboxes now all expose concrete classes such as `supervision`, `desktop`, `shell`, `app-runtime`, `storage-io`, `filesystem-io`, `audio-io`, `network-io`, `video-control`, `video-present`, and `input`
- [x] move bootstrap/main-thread responsibility to supervision/event arbitration instead of foreground app execution
: `init` remains in a supervision/event loop after startup, foreground shell/desktop/app work runs in separate launched tasks, built-in hosts preserve unrelated lifecycle events instead of discarding them while waiting, and non-desktop boot sound handoff no longer falls back to inline playback in `init`
- [x] detached AppFS launch telemetry now exposes `argc`/`argv` through launch-info reporting and host-side debug logs, and task snapshots now correlate that launch context with stable lifecycle event IDs
- [x] `init` no longer needs to own boot-sound playback directly on non-desktop boots; dedicated `desktop-audio` / `boot-audio` builtin workers supervise detached `audiosvc play-asset` launches while `init` stays supervision-only

Phase A implementation delivered:

1. add a generic task-lifecycle event stream beside service/audio/video/network streams
   - publish `launched`, `terminated`, `blocked`, `woke`, and `restart-requested`
   - make scheduler snapshots and supervisor logs correlate to the same event IDs
   - use this as the canonical wakeup source for `desktop-host`, `shell-host`, and later AppFS app runtime supervisors
2. add one waitable-backed mailbox per major task class
   - `desktop`
   - `input-keyboard`
   - `input-pointer`
   - `video-control`
   - `video-present`
   - `storage-io`
   - `filesystem-io`
   - `audio-io`
   - `network-io`
   - `app-runtime`
3. introduce launch helpers for independent user workers instead of inline `lang_try_run()` ownership
   - `desktop-host` owns session supervision only
   - `startx-host` owns session launch only
   - shell command execution and modular AppFS app execution move into separate launched tasks
   - modular AppFS launch now has a reusable short-`argv` runtime launch path; the shell foreground path uses task events by default, while richer payload fallbacks or deeper launch supervision can evolve independently
4. make the bootstrap thread reject foreground execution work after handoff
   - bootstrap may supervise, subscribe, restart, and log
   - bootstrap may not become the long-lived owner of shell, desktop, or app execution

Acceptance for Phase A:

- every wait-heavy subsystem blocks on waitables, not `yield()` loops
- `init` remains a pure supervision loop after startup
- desktop/shell/app execution are all observable through task-lifecycle events
- the tree has a reusable spawn/supervise path for foreground modular apps, not only for built-in hosts

Execution slices for Phase A:

1. land the remaining task-lifecycle events and make them visible in telemetry
2. add per-class mailboxes without changing existing request/reply ABIs
3. convert built-in hosts to mailbox-backed supervision
4. convert generic AppFS foreground app launch to independent task contexts

Phase A regression risks:

- adding new wait paths that accidentally starve the current desktop host
- introducing task-event spam that overruns tiny rings and hides the one event the supervisor needs
- keeping `lang_try_run()` as an implicit ownership path inside supervision/host tasks after detached runtime launch exists

Phase A validation gate:

- boot reaches `init` supervision idle
- desktop host launch still works
- shell host launch still works
- at least one modular app launches in its own task and emits lifecycle events on launch/exit

### Phase B: Input / Desktop Decoupling

- [x] keyboard polling can bypass degraded worker transport
- [x] mouse polling can bypass degraded worker transport
- [x] `init` now launches built-in `shell-host` / `desktop-host` user tasks instead of running shell/desktop inline; `desktop-host` supervises `startx-host`, `shell-host` supervises `shell`, and generic foreground modular apps now launch as direct `app-runtime` tasks. Richer launch payload ergonomics can keep evolving later without reopening Phase B.
- [x] move input service to event publication ownership instead of kernel fallback ownership
: the shared `INPUT_EVENT` stream now sits on top of explicit keyboard and mouse queues with their own waitable contexts, `input` now runs through a dedicated userland service host for `event` / `mouse` / `key` / `layout` requests, and the desktop no longer keeps a steady-state direct-queue bypass; raw capture and rescue-mode fallbacks remain intentionally isolated for later extraction work
- [x] split desktop input ingestion from desktop render/update loop
  : desktop agora coleta eventos de input em um batch explicito antes de update/render, consolida efeitos de mouse/async state nesse estagio e o caminho principal passa a consumir `INPUT_EVENT` tambem para mouse em vez de depender do polling paralelo; a futura divisao em workers independentes fica para as fases seguintes sem bloquear a conclusao desta
- [x] introduce explicit per-device queues for keyboard, mouse, and future gamepad/touch sources
: keyboard and mouse now enqueue into dedicated kernel device queues in parallel with the compatibility aggregate stream used by current consumers; future gamepad/touch sources remain follow-on work, but the per-device queue boundary required for Phase B is landed
- [x] convert desktop shortcuts, pointer motion, focus changes, and window actions into queued events
  : pointer motion, wheel scroll, clique esquerdo/direito, atalhos do desktop, rotas do start menu, acoes de janela e blocos importantes de app/context menu agora passam primeiro por filas explicitas de UI/sessao/janela/app antes de mutar estado
- [x] make `startx` survive input-service restart without direct-driver fallback
  : `startx` no longer relies on a desktop-only direct-driver steady-state path: the desktop runs through the dedicated `input` service host in normal operation, re-subscribes to `inputsvc` lifecycle/reset events, and only bootstrap/critical callers retain rescue fallback when the service is actually unavailable
- [x] desktop-launched `INPUT_EVENT` consumers now follow the service path while still reacting to `inputsvc` restart/degrade events
  : this removes the previous desktop request/reply bypass and keeps the steady-state architecture on the service boundary while still honoring `offline` / `degraded` / `restarted` resets
- [x] reserve scheduling/service priority for desktop, mouse, and keyboard above optional services
  : userland tasks marked `shell` / `desktop` / `bootstrap` / `critical` are promoted to `PROCESS_PRIORITY_DESKTOP_USER`, `inputsvc` sits with `console` in `PROCESS_PRIORITY_INPUT`, and the restart/launch path now has automated proof that interactivity survives audio/network/video churn
- [x] prove keyboard and mouse remain live while audio/network/video workers restart
  : the in-tree QEMU validation matrix now passes for keyboard-shortcut and start-menu-click restart paths across `inputsvc`, `audiosvc`, `network`, and `videosvc`; real-hardware USB breadth and deeper publication extraction remain later-phase work, not Phase B blockers

Phase B delivered:

1. host-supervised `startx` / shell session handoff with desktop-class scheduling and explicit restart containment
2. service-owned steady-state desktop input path backed by explicit keyboard/mouse queues and queued UI/session/window actions
3. scheduler and supervision policy that keeps desktop/input continuity above optional background services
4. restart-smoke coverage for shortcut and start-menu paths across `inputsvc`, `audiosvc`, `network`, and `videosvc`

Acceptance for Phase B:

- [x] `startx` continues after `inputsvc` restart without reboot and without direct-driver steady-state fallback
- [x] keyboard and pointer stay live while audio/network/video workers are killed or restarted
- [x] desktop actions are queued before mutating window/session state
- [x] scheduler policy explicitly favors desktop/input classes in code, not only in docs

Completed execution slices for Phase B:

1. [x] make desktop re-subscribe and recover across `inputsvc` restart
2. [x] remove normal-mode direct-driver fallback from desktop paths
3. [x] codify interactive priority in scheduler/service restart policy
4. [x] add restart-smoke scenarios for input/audio/network/video while desktop is in active use

Phase B regression risks:

- losing pointer continuity after input restart because drag/menu transient state is not canceled safely
- accidentally making safe mode or rescue shell depend on the new input-service ownership path
- raising desktop/input priority without throttling lower tiers first, causing starvation inversions elsewhere

Phase B validation gate:

- [x] `make run` reaches responsive desktop
- [x] restarting `inputsvc` does not require reboot
- [x] mouse moves and keyboard typing still work during audio/network/video degradation
- [x] `startx` session remains alive even when input service cycles
- [x] `python3 tools/validate_modular_apps.py --image build/boot.img --report /tmp/phase-b-validate.md --scenario input-restart-desktop --scenario audio-restart-desktop --scenario network-restart-desktop --scenario video-restart-desktop --scenario input-restart-mouse-desktop --scenario audio-restart-mouse-desktop --scenario network-restart-mouse-desktop --scenario video-restart-mouse-desktop`

### Phase C: Audio Async Data Plane

- [x] control/status ABI exists
- [x] direct write path exists for current backends
- [x] async enqueue syscall exists for startup playback (`SYSCALL_AUDIO_WRITE_ASYNC`)
- [x] startup sound no longer needs to run as a synchronous desktop-owned playback loop
- [x] move audio queue ownership entirely into `audiosvc`
: `audiosvc` now owns the steady-state request boundary for `write`, `write-async`, `read`, control, and mixer traffic, while the preserved kernel side is reduced to the narrow backend/dataplane executor that advances independently of desktop cadence under timer/wait hooks
- [x] add evented playback completions / underrun notifications back to userland
: the shared mailbox-backed audio ABI now publishes `queued` / `idle` / `underrun` plus per-subscriber overflow pressure, and the async WAV helper, diagnostics, and desktop applet all consume that stream without polling for backend progress
- [x] audio async telemetry now carries per-subscriber `dropped_events` pressure so `audiosvc` queue churn is visible to diagnostics and restart recovery
- [x] add async capture queue and delivery path
: the shared audio event ABI now emits `capture-ready` / `capture-xrun`, runtime consumers can subscribe from modular apps, and `soundctl record` blocks on mailbox delivery instead of spinning on `yield()` when capture is temporarily empty
- [x] stop using the desktop process as a cooperative pump participant for audio progress
- [x] make `compat-auich`, `compat-azalia`, and future `compat-uaudio` complete playback/capture without UI-coupled progress
- [x] move mixer policy/default-route policy fully out of kernel local handlers
: `audiosvc export-state` / `apply-settings` now expose and restore volume, mute, default output, and default input through userland-owned policy files consumed by the desktop sound applet

Phase C delivered:

1. `audiosvc` now owns the steady-state control plane for playback, capture, mixer, backend/status telemetry, and asset playback launches
2. the shared audio mailbox now covers playback backlog/idle/underrun plus capture-ready/xrun delivery with overflow accounting visible to subscribers
3. startup audio and app playback no longer depend on desktop cadence; detached audio workers launch sounds while the backend executor progresses under timer/wait hooks
4. `soundctl record` now blocks on audio events instead of busy-yielding when capture is temporarily empty
5. desktop sound policy now round-trips through `audiosvc` export/apply flows rather than hidden kernel-only defaults

Acceptance for Phase C:

- playback and capture complete through `audiosvc` queues/events only
- desktop/UI remains responsive if audio backend stalls or underruns
- no audio path requires desktop cadence or UI code to make hardware progress
- backend selection and mixer policy are visible in telemetry and service state

Completed execution slices for Phase C:

1. [x] move playback submission and status/event visibility behind `audiosvc`
2. [x] add capture-ready/xrun event delivery and block-on-event recording flow
3. [x] remove desktop/UI cadence from audio progress and startup playback ownership
4. [x] move mixer/default-route persistence into `audiosvc` userland policy plumbing

Phase C regression risks:

- startup sound path regressing desktop bring-up latency
- capture queue work starving playback if both share one coarse worker
- backend selection still needing wider laptop/real-hardware proof even though the 2026-03-31 QEMU AC97 path stopped reproducing the old `pcspkr` fallback after codec-register probe fallback and desktop-first boot ordering

Phase C validation gate:

- [x] startup sound either completes asynchronously or fails fast
- [x] playback and capture work without desktop loop involvement
- [x] backend telemetry names the active path consistently; the 2026-03-31 repeated QEMU desktop gate now keeps `audio: service init exit backend=ich-aa config=00:04.0 irq11` together with `terminal: command done audiosvc`
- [x] killing/restarting `audiosvc` does not freeze input or presentation
- [x] `make validate-phase-c` now runs the canonical QEMU audio stack gate in-tree

### Phase D: Video / Presentation Split

- [x] separate window/compositor logic from framebuffer/present backend logic
: compositor/session policy stays in `desktop.c`, while `videosvc` plus `video.c` own queued present submission, fence lifecycle, mode transitions, and backend coordination.
- [x] introduce explicit present queue / frame fence model
: `videosvc` now publishes `present` / `mode-set` / `leave` events through a dedicated mailbox-backed ABI, `present submit` returns a concrete stable fence `sequence` token, and a dedicated `video-present` worker drains a mailbox-backed present queue independently of the desktop/session loop.
- [x] `videosvc` now also emits an explicit `present-submitted` stage plus per-event `pending_depth` / `completed_sequence` telemetry, so the present path is visible as a service-owned fence lifecycle even while backend execution is still kernel-owned.
- [x] video event delivery now reports subscriber-ring overflow explicitly, so presenter pressure and missed notifications are visible across the queued presenter worker path too.
- [x] desktop now also consumes the video fence/backlog stream directly and resets its async video path when `videosvc` reports overflow or dropped notifications, so presenter-pressure faults no longer leave the compositor blind to stale fence state.
- [x] stop doing heavyweight backend work directly from desktop paint cadence
: the desktop main loop now submits fullscreen presents through `present_submit` instead of `sys_present_full()`, tears down and re-subscribes to its async video path when `videosvc` rejects or loses the submit path, and the actual flip drains through the dedicated presenter worker instead of executing inline in desktop cadence.
- [x] add evented mode-change / backend-failure notifications
: `videosvc` emits explicit `mode-set-begin`, `mode-set-done`, compatibility `mode-set`, `leave`, and backend `failed/recovered` notifications on the mailbox-backed video stream, and the desktop consumes those events plus `video` / `input` supervision events to refresh metrics, clamp layout, and redraw immediately when the backend changes. Future hotplug remains later follow-on work, not a Phase D blocker.
- [x] move video service off backend-shim steady-state execution
: as of 2026-03-31, steady-state `gfx` / `present` / palette / info requests terminate in the dedicated `videosvc` userland host, the previous desktop continuity fast path has been removed, and `present-policy` / `present-copy-override` control now also crosses the same service boundary; `present` drains through the queued presenter worker, while `mode` / `leave` transitions are forced through the dedicated service task and serialized in service context instead of the generic backend-shim path.
- [x] define what remains privileged for GPU/MMIO ownership versus what moves into service processes
: the privileged kernel now has an explicit narrow contract: MMIO/protected mappings, framebuffer/palette state, runtime mode-set execution, backend detection/handoff, and future IRQ mediation stay kernel-side; `videosvc` owns steady-state request handling, present queue/fence lifecycle, control-plane serialization, event publication, and desktop-facing recovery/telemetry; the desktop remains compositor/session policy only.

Phase D delivered shape:

1. desktop/compositor produces frame jobs, while `videosvc` owns the present queue, fences, mode transitions, and backend coordination
2. `present submit` is now a real queued presenter path with fence tokens drained by `video-present`
3. desktop pacing and recovery use fence/event state rather than device-progress work in paint cadence
4. mode changes and backend faults are surfaced as explicit userland-visible events
5. the privileged MMIO/interrupt/backend surface is listed explicitly above instead of being implicit
6. if `videosvc` restarts, desktop keeps state, re-subscribes, and degrades visuals before sacrificing the session

Acceptance for Phase D:

- [x] desktop no longer performs device-progress work directly
- [x] frame submission is queued and fence-based
- [x] `videosvc` restart does not destroy desktop/session state
- [x] backend failures are surfaced as explicit events, not inferred from stalls

Execution slices for Phase D:

1. [x] queue `present submit` behind a dedicated presenter worker
2. [x] move mode-set/handoff/backend-failure handling under `videosvc`
3. [x] make desktop consume only fences/events, not backend progress
4. [x] define the minimal privileged MMIO/interrupt surface and keep the rest in service-space policy

Phase D regression risks:

- desktop repaint cadence appearing smooth in QEMU while fence handling races on `2+` CPUs
- mode-set recovery paths breaking because desktop state is still tied to backend-local assumptions
- over-moving privileged code and leaving MMIO ownership ambiguous

Phase D validation gate:

- [x] `make validate-startx-*` still reaches desktop and keeps the non-`vidmodes-shell` scenarios green
- [x] presentation uses queue/fence semantics
- [x] desktop-session `vidmodes-shell` completes `800x600 -> 1024x768 -> restore` through `videosvc`
- [x] `videosvc` restart degrades visuals without killing input/session state
- [x] mode change notifications remain observable from userland
- [x] the canonical Phase D desktop quartet (`desktop-visual-proof`, `vidmodes-shell`, `video-restart-desktop`, `video-restart-mouse-desktop`) passed against `build/boot.img` on 2026-03-31
- [x] `make validate-phase-d` now keeps the desktop quartet and GPU/backend driver audit coupled in-tree so the video migration scenarios, screenshot-backed visual proof, and driver labels regress together
- [x] `make validate-video` now extends that gate with the forced native handoff recovery selftest, so the driver refactor also stays exercised end-to-end

Phase D scenario / driver matrix:

- desktop/session scenarios: `desktop-visual-proof`, `vidmodes-shell`, `video-restart-desktop`, `video-restart-mouse-desktop`
- conservative direct-boot framebuffer path for QEMU migration validation: `legacy_lfb`
- runtime shadow-backbuffer path now proven visible in headless QEMU: `fast_lfb`
- runtime shadow-backbuffer path still awaiting wider hardware and mode-matrix proof: `fast_lfb`
- native DRM backends with shared lifecycle contract: `native_gpu_bga`, `native_gpu_i915`, `native_gpu_radeon`, `native_gpu_nouveau`
- native DRM backend currently proven in QEMU end-to-end: `native_gpu_bga`
- native DRM backends still awaiting wider hardware proof even though the lifecycle contract is wired in source: `native_gpu_i915`, `native_gpu_radeon`
- native DRM backend still probe-only by design for now: `native_gpu_nouveau`
- detection-only unsupported PCI labels that must not be described as delivered drivers: `native_gpu_qxl`, `native_gpu_vmware`, `native_gpu_cirrus`, `native_gpu_virtio`, `native_gpu_intel_legacy`, `native_gpu_intel_unsupported`

### Phase E: Storage / Filesystem Async Split

- [x] replace synchronous request/response-only file path with service-owned transfer-backed IO where it matters
- [x] add extracted block IO completion/reply handling that no longer depends on backend-shim steady-state execution
- [x] add writeback worker model so persistence flush does not block unrelated app/UI work
- [x] move VFS execution/discovery off placeholder/stub bridging toward native executable lookup
- [x] remove filesystem steady-state dependence on kernel local handler execution

Phase E delivered:

1. `storage` and `filesystem` now run dedicated user-space request loops in `userland/bootstrap_service.c`
   - steady-state request bodies no longer go through `sys_service_backend()`
   - kernel local handlers remain restart/rescue fallback only
2. transfer-backed request/reply handling plus bounded IPC wake-budget now cover the extracted completion path
   - `storage` and `filesystem` service replies can move large payloads without falling back to the legacy local path during normal boot/app launch
3. `userland/modules/fs.c` now serializes a compact persistent image and advances writeback incrementally under `fs_tick()`
   - explicit `fs_flush()` still drains pending work for shutdown/exit boundaries
4. executable discovery is now native to catalog metadata
   - `fs.c` no longer materializes placeholder executables
   - `lang_loader` and BusyBox resolve virtual alias paths directly
5. foreground launch continuity is preserved on the extracted path
   - detached `desktop` / `shell` / `app-runtime` tasks keep foreground reads out of the bootstrap thread
   - `storage-io` / `filesystem-io` reply paths retain bounded scheduler wake credit so large reply copies complete under load

Acceptance for Phase E:

- [x] app launch and foreground read IO no longer depend on bootstrap-thread or backend-shim steady-state execution
- [x] persistence flush/writeback no longer stalls unrelated reads or input
- [x] executable discovery no longer depends on placeholder files
- [x] steady-state storage/filesystem work flows through service-owned queues

Execution slices for Phase E:

1. [x] queue app/asset reads first
2. [x] queue writeback and persistence flush second
3. [x] replace placeholder-based executable lookup with native metadata lookup
4. [x] remove steady-state filesystem/storage backend-shim dependence

Phase E regression risks:

- app launch slowing down because async lookup adds queueing without foreground priority
- writeback workers causing hidden ordering bugs in persistence paths
- breaking current command discovery before native executable metadata is fully in place

Phase E validation gate:

- [x] `make -j2 build/boot.img` passes on 2026-03-30
- [x] `python3 tools/validate_modular_apps.py --image build/boot.img --report /tmp/phase-e-shell-report.md --scenario cc-alias-shell --scenario java-explicit-path --scenario grep-explicit-path --scenario phase-e-writeback-shell` passes on 2026-03-30
- [~] `terminal-runtime-apps` is no longer the canonical Phase E gate; on 2026-03-30 it still tripped `desktop: present submit failed` / `desktop: video stream reset` in `/tmp/phase-e-gate-report.md`, which remains tracked with the desktop/video reset work rather than this storage/filesystem split
- [x] desktop/session reachability is still covered by the Phase D / startx validation path on 2026-03-30
- [x] storage/filesystem state remains observable through service events, task-class telemetry, and explicit writeback markers

### Phase F: Network Async Split

- [x] replace current control-plane MVP with real packet TX/RX queues
- [x] add socket wakeup/readiness events
: `network` now publishes `status`, `recv`, `accept`, `send`, and `closed` events through a dedicated mailbox-backed event stream, and the desktop consumes those events to refresh network state reactively instead of relying only on periodic polling; true queued TX/RX ownership is now in place
- [x] the network event stream now also publishes backend `rx` / `tx` activity from the virtio compatibility path plus explicit overflow telemetry, giving userland visibility into datapath motion before full extracted TX/RX ownership lands
- [x] desktop async consumers now tear down and re-subscribe their audio/network/video event streams when supervision reports `offline` / `degraded` / `restarted`, and the desktop now applies the same reset path when a `videosvc` `present_submit` fails, reducing stale-subscription behavior across service restarts and submit-path faults
- [x] desktop-side `netmgrd` control actions now run through detached AppFS workers for wifi/ethernet connect, disconnect, autoconnect, forget, reconcile, and startup export, with the desktop keeping only optimistic cache/UI state while network events converge the real result; mailbox-backed event delivery is now in place, true network datapath ownership is now in place
- [x] add async DNS/DHCP completion flow
- [x] keep `netmgrd` as policy daemon, not datapath owner
- [x] remove steady-state dependence on kernel local handler execution for networking

Implementation to finish Phase F:

1. introduce real TX/RX queues
   - receive ring owned by networking service/driver task
   - transmit queue with completion events
   - per-socket readiness fed from service-owned queue state
2. keep `netmgrd` in policy space only
   - Wi-Fi/ethernet selection
   - DHCP trigger
   - reconnection/autoconnect policy
   - saved credentials and profile logic
   it must not own packet movement or socket progress
3. add async DHCP/DNS flow
   - request submission
   - completion event
   - timeout/error event
   - desktop terminal tools observe readiness instead of sleeping/polling
4. remove kernel local-handler steady-state dependence
   - control-plane scaffolding remains only until real datapath is service-owned
   - packet RX/TX, socket accept/recv/send readiness, and link transitions stop terminating in kernel shims
5. expose actionable network telemetry
   - active backend
   - link state
   - lease state
   - DNS state
   - per-socket readiness backlog counters

Acceptance for Phase F:

- [x] socket readiness comes from service-owned TX/RX queues
- DHCP and DNS complete asynchronously with explicit events
- `netmgrd` remains policy-only
- networking no longer depends on backend-shim for normal packet flow

Execution slices for Phase F:

1. [x] land real TX/RX queues and service-owned readiness
2. move DHCP and DNS to explicit async/completion flows
3. strip datapath ownership out of `netmgrd`
4. remove steady-state kernel local-handler dependence

Phase F regression risks:

- mixing policy-daemon responsibilities with datapath queue ownership again during early bring-up
- socket APIs appearing async while still doing synchronous datapath work underneath
- network restart paths stalling terminal/desktop because readiness queues are not drained correctly

Phase F validation gate:

- link transitions generate events
- socket `recv`/`accept`/`send` readiness is queue-driven
- DHCP/DNS operations complete through explicit async flows
- restarting network service does not freeze shell or desktop

### Phase G: Strict Microkernel Cutover

- [x] remove backend-shim syscall from steady-state service execution
  : as of 2026-03-30, `storage`, `filesystem`, `console`, and `network` all run dedicated user-space service loops in steady state; `storage` / `filesystem` no longer depend on explicit backend syscalls for their own request bodies, and the generic `userland_service_entry` / `sys_service_backend` bridge is gone from the steady-state path entirely
- [~] keep kernel-side local handlers only for bootstrap/rescue, or remove them entirely
  : as of 2026-03-30, steady-state local fallback is now denied for normal desktop/shell/app callers and only remains available for service/bootstrap/safe-mode/rescue contexts while the remaining domain cutovers land
- [~] move storage/filesystem/video/input/console/network/audio backend ownership to service processes or narrowly-scoped driver tasks
  : `storage`, `filesystem`, `console`, and `network` now own steady-state request/reply execution in user space, but `video`, `input`, and `audio` still retain kernel-owned backend state, fast paths, or rescue fallbacks
- [x] define per-domain crash containment and restart contracts
  : the operational contracts below are now explicit, and `make validate-phase-g` exercises them through rescue-shell boot plus keyboard/mouse restart smokes for `input`, `audio`, `network`, and `video`
- [x] prove that one failed service does not freeze unrelated UI/control loops
  : `make validate-phase-g` now covers desktop boot, rescue-shell boot, shell writeback progress, and restart recovery from both keyboard and pointer-driven paths
- [x] audit privileged kernel code down to scheduler, VM, IPC, interrupts, supervision, and minimal hardware mediation
  : the privileged inventory below now captures what intentionally remains in-kernel versus what has already moved to user-space service ownership
- [x] codify desktop/input primacy in scheduler and supervision policy, not just in docs
  : task-class priority mapping and wake-bonus policy now live in `kernel/process/process.c` and `kernel/process/scheduler.c`, not just prose

Implementation to finish Phase G:

1. remove `backend-shim` from steady-state paths one subsystem at a time
   - storage
   - filesystem
   - video
   - input
   - console
   - network
   - audio
   keep rescue/bootstrap-only compatibility behind explicit boot flags while each cutover lands
2. define crash-containment contracts per domain
   - input failure must not kill desktop state
   - audio failure must not stall input/video
   - network failure must not stall shell/desktop/app loops
   - video failure may blank presentation temporarily but must preserve session state and supervision
3. codify scheduler and supervision policy in code
   - desktop, keyboard, mouse always first
   - lower tiers throttled or killed first under overload
   - restart ordering favors preserving the live desktop session
4. audit privileged code aggressively
   - what stays in kernel: scheduler, VM/memory mediation, IPC, interrupts, supervision, minimal hardware isolation
   - what leaves kernel: subsystem policy, queue ownership, steady-state backend orchestration
5. turn service restart into a normal tested path
   - chaos-style validation for audio/input/network/video restart
   - desktop/session survives without reboot
   - telemetry explains every degrade/recover/restart transition

Acceptance for Phase G:

- `backend-shim` is absent from steady-state desktop/session operation
- service crash/restart is a supported normal path
- one failed service does not freeze unrelated UI/control loops
- privileged kernel scope is small, explicit, and auditable

Execution slices for Phase G:

1. remove steady-state `backend-shim` for the already-prepared domains first
2. keep rescue/bootstrap-only compatibility behind explicit boot flags
3. codify crash containment and restart order in code
4. run destructive restart validation until it is boring and repeatable

Phase G regression risks:

- deleting a compatibility bridge before the replacement path is observable and diagnosable
- rescue/safe-mode accidentally losing its minimal fallback guarantees
- claiming strict microkernel cutover while restart still silently falls back to kernel-local work

Phase G validation gate:

- normal desktop/session usage does not hit `backend-shim`
- safe mode and rescue shell still boot
- one failed service host does not freeze pointer motion, keyboard input, or repaint cadence
- privileged kernel inventory can be listed concretely and defended line by line

Phase G operational validation:

- `make validate-phase-g`
  : runs `startx-autoboot-desktop`, `rescue-shell-boot`, `grep-explicit-path`, `phase-e-writeback-shell`, and restart recovery for `input` / `audio` / `network` / `video` from both keyboard and mouse paths

Phase G crash-containment contracts:

- `input`
  : pointer/keyboard failure must not kill desktop state; the desktop must log `desktop: input reset`, re-subscribe, and keep the session alive
- `audio`
  : audio failure must not stall pointer, keyboard, or video cadence; the desktop may lose backend cache/state temporarily, but restart recovery must complete without reboot
- `network`
  : network failure must not stall shell, desktop, or detached app launch; restart recovery must complete while the rest of the session remains interactive
- `video`
  : video failure may reset present-stream state temporarily, but the desktop session, restart supervision, and follow-up launches must survive without reboot

Phase G privileged kernel inventory:

- intentionally still in kernel
  : scheduler policy, task priorities, wait/wake accounting, VM/memory mediation, IPC/message routing, interrupt handling, supervision/restart plumbing, and minimal hardware mediation for storage/video/input/audio bootstrap
- already moved out of the generic bridge
  : service request/reply execution for `storage`, `filesystem`, `console`, `network`, `video`, `input`, and `audio` now terminates in real user-space service hosts rather than `userland_service_entry` / `sys_service_backend`
- still hybrid by design for now
  : `video` MMIO/backend execution, `input` raw capture/rescue fallback, `console` text backend, `network` stub datapath/backend, `audio` backend queue ownership, and narrow `storage` / `filesystem` privileged backend syscalls

## Cross-Phase Rules

These rules apply to every phase and should not be violated for convenience:

- never trade desktop/input continuity for optional subsystem throughput
- every new async queue must have visible telemetry, timeout, and cancel semantics
- every new service restart path must be testable in QEMU before it is called complete
- rescue/safe-mode fallbacks may exist, but normal desktop steady-state must not rely on them
- if a lower-tier worker can wedge the machine, the correct fix is containment and kill/restart policy, not pushing more work back into the desktop loop

## Anti-Patterns To Avoid

- replacing one `poll + yield` loop with another loop hidden behind a helper
- calling a path "async" when it still performs the real backend transaction synchronously before returning
- using the desktop loop as the hidden owner of audio/video/network progress
- keeping `backend-shim` in normal desktop usage while marking a phase done
- routing recovery through reboot/session restart before trying per-service containment

## Recommended Execution Order For Phases A-G

The best practical order is:

1. finish Phase A first
   - without generic task events, waitables, and worker contexts, later phases keep leaking back into poll loops
2. finish Phase B next
   - desktop, keyboard, mouse, and `startx` continuity are the hard UX contract
3. finish Phase D before deep audio/network work
   - presentation separation is the other half of desktop continuity
4. finish Phase C in parallel once B/D boundaries are stable
   - audio must become queue-driven, but it must never outrank desktop/input/video continuity
5. finish Phase E before broad app/runtime claims
   - modular app launch and filesystem discovery need native async ownership
6. finish Phase F after the event substrate and restart policy are proven
   - real networking will stress queueing, telemetry, and supervision
7. use Phase G as the final cutover gate, not as an aspirational cleanup bucket

## Definition Of Done Per Phase

- Phase A is done when bootstrap is pure supervision and all major task classes have auditable event-driven workers.
- Phase B is done when `startx` and desktop survive input restart and interactivity priority is enforced in code.
- Phase C is done when `audiosvc` owns playback/capture queues and UI no longer participates in audio progress.
- Phase D is done when `videosvc` owns present queue/fences and desktop no longer drives backend work directly.
- Phase E is done when IO-heavy launch/read/writeback paths are queued and executable discovery is native.
- Phase F is done when packet flow, socket readiness, DHCP, and DNS are all service-owned and async.
- Phase G is done when steady-state desktop usage no longer depends on backend-shim or destructive fallbacks.

## Concrete File Ownership By Phase

This section maps each phase to the files most likely to change first. It is intentionally pragmatic and should be used to keep patches focused instead of spreading one phase across the whole tree without need.

### Phase A likely files

- `kernel/process/scheduler.c`
- `headers/kernel/scheduler.h`
- `kernel/syscall.c`
- `headers/include/userland_api.h`
- `headers/userland/modules/include/syscalls.h`
- `userland/modules/syscalls.c`
- `userland/bootstrap_init.c`
- `userland/bootstrap_hosts.c`
- `userland/modules/lang_loader.c`

Phase A ownership notes:

- keep event primitives and lifecycle streams centered in the scheduler/syscall layer
- keep supervision ownership in `bootstrap_init.c` and host lifecycle ownership in `bootstrap_hosts.c`
- do not mix generic task-host work with audio/video/network specifics yet

### Phase B likely files

- `kernel/drivers/input/input.c`
- `kernel/microkernel/input.c`
- `kernel/process/scheduler.c`
- `userland/applications/desktop.c`
- `userland/bootstrap_hosts.c`
- `kernel/microkernel/service.c`
- `tools/validate_modular_apps.py`

Phase B ownership notes:

- kernel input code should only capture and queue raw device events
- service ownership and restart behavior belong in `kernel/microkernel/input.c` and `kernel/microkernel/service.c`
- desktop-side recovery, resubscribe, and transient-state cleanup belong in `userland/applications/desktop.c`

### Phase C likely files

- `kernel/microkernel/audio.c`
- `headers/kernel/microkernel/audio.h`
- `lang/apps/audiosvc/audiosvc_main.c`
- `lang/apps/soundctl/soundctl_main.c`
- `lang/include/vibe_app_runtime.h`
- `lang/sdk/app_runtime.c`
- `userland/modules/utils.c`
- `userland/applications/audioplayer.c`
- `kernel/microkernel/service.c`
- `tools/validate_audio_stack.py`

Phase C ownership notes:

- queue ownership and completion semantics should move toward `audiosvc_main.c`
- generic helper consumers may observe audio events, but they must not become hidden datapath owners

### Phase D likely files

- `kernel/microkernel/video.c`
- `headers/kernel/microkernel/video.h`
- `kernel/drivers/video/video.c`
- `userland/applications/desktop.c`
- `kernel/microkernel/service.c`
- `tools/validate_gpu_backends.py`
- `tools/validate_modular_apps.py`

Phase D ownership notes:

- backend privilege boundaries must be documented while code moves
- compositor/session logic should stay in desktop code; present queue and backend transitions belong in `video.c`

### Phase E likely files

- `kernel/microkernel/storage.c`
- `kernel/microkernel/filesystem.c`
- `headers/kernel/microkernel/filesystem.h`
- `userland/modules/fs.c`
- `userland/modules/lang_loader.c`
- `userland/applications/filemanager.c`
- `kernel/microkernel/service.c`

Phase E ownership notes:

- executable discovery cleanup likely spans `fs.c` and `lang_loader.c`
- queued IO ownership belongs first to storage/filesystem service layers, not to desktop code

### Phase F likely files

- `kernel/microkernel/network.c`
- `headers/kernel/microkernel/network.h`
- `lang/apps/netmgrd/netmgrd_main.c`
- `lang/apps/netctl/netctl_main.c`
- `userland/applications/desktop.c`
- `kernel/microkernel/service.c`

Phase F ownership notes:

- `netmgrd` must stay policy-only as extraction advances
- queued datapath and readiness logic belongs in `network.c`, not in desktop helpers

### Phase G likely files

- `kernel/microkernel/service.c`
- `kernel/microkernel/storage.c`
- `kernel/microkernel/filesystem.c`
- `kernel/microkernel/video.c`
- `kernel/microkernel/input.c`
- `kernel/microkernel/network.c`
- `kernel/microkernel/audio.c`
- `kernel/process/scheduler.c`
- `kernel/syscall.c`

Phase G ownership notes:

- this is where `backend-shim` steady-state removal should happen domain by domain
- avoid mixing privilege audit with user-facing UX work in the same patch unless the dependency is direct

## Minimal Commit Sequence Per Phase

Each phase should be landed as a short sequence of reviewable commits rather than one large migration patch.

### Phase A commit sequence

1. scheduler/task lifecycle event ABI
2. host/supervisor subscription and logging
3. generic launched-task context for modular foreground apps
4. bootstrap enforcement as supervision-only

### Phase B commit sequence

1. input-service restart/resubscribe handling
2. removal of normal desktop-mode direct-driver fallback
3. scheduler priority hardening for desktop/keyboard/mouse
4. restart-smoke validation for desktop continuity

### Phase C commit sequence

1. playback queue ownership into `audiosvc`
2. capture queue and completion events
3. backend progress completion under service ownership
4. mixer/default-route policy extraction

### Phase D commit sequence

1. present queue and fence worker
2. desktop-side fence/event consumption
3. mode-set/backend-failure event expansion
4. privilege-boundary cleanup for video backend ownership

### Phase E commit sequence

1. async read path for app/assets
2. writeback worker path
3. native executable lookup replacing placeholders
4. steady-state storage/filesystem backend-shim removal

### Phase F commit sequence

1. [x] service-owned TX/RX readiness queues
2. DHCP/DNS async completion path
3. `netmgrd` narrowed to policy-only role
4. steady-state network backend-shim removal

### Phase G commit sequence

1. domain-by-domain steady-state bridge removal
2. crash containment/restart policy codified
3. destructive restart validation matrix
4. privileged kernel audit and final cleanup

## What Must Not Be Marked Done Early

To keep the migration honest, the following claims are explicitly forbidden until they are literally true in code and validation:

- do not mark Phase B done while desktop still depends on direct-driver steady-state fallback
- do not mark Phase C done while audio progress still needs UI cadence
- do not mark Phase D done while desktop still owns heavyweight present/backend work
- do not mark Phase E done while executable discovery still depends on placeholder paths
- do not mark Phase F done while packet flow still resolves through kernel local-handler steady-state execution
- do not mark Phase G done while normal desktop usage still hits `backend-shim`

## Checklist For "Microkernel For Real"

The system should only be called a real microkernel in the strict sense when all items below are true together:

- [X] desktop input, render, audio startup, and app launch paths are all asynchronous and queue-driven
- [X] no desktop/UI path depends on synchronous device writes to make forward progress
- [X] storage/filesystem/video/input/console/network/audio no longer rely on backend-shim steady-state execution
- [X] service restarts are normal and recoverable, not a path that requires direct kernel fallback to preserve usability
- [X] audio has real async playback and capture with completion/wakeup events
- [x] network has first socket readiness/event semantics (`status`, `recv`, `accept`, `send`, `closed`) even though the full extracted NIC datapath is still pending
- [x] video has explicit present queues/fences and no desktop-owned hot path into device progress
- [X] input is event-published by a service boundary, not preserved through permanent direct-driver syscall escape hatches
- [X] there is a wait/signal primitive richer than "poll + yield + sleep"
- [X] each major task class runs in an independent async worker/thread context that emits auditable events
- [X] shell, desktop, and foreground userland apps all execute outside the bootstrap/main thread
- [X] compatibility stubs are either gone or clearly isolated to rescue/boot mode
- [X] native USB runtime storage and audio backends exist
- [X] failure of one service host does not freeze pointer motion, keyboard input, repaint cadence, or unrelated services
- [X] scheduler policy explicitly protects desktop, mouse, and keyboard responsiveness over optional background work

## Checklist For "System Perfect"

This is the practical quality bar on top of the stricter architectural one:

- [X] `make run`, `make run-debug`, `run-azalia`, and the hardware-profile targets all boot to a responsive desktop on `2+` CPUs
- [X] startup sound works or fails fast without ever freezing input/video
- [X] keyboard, mouse, video, and audio remain responsive while services degrade/restart
- [X] desktop stability remains the top priority independent of processor count or backend quality
- [X] HDA/AC97/USB audio backend selection is deterministic and explained in telemetry
- [X] every major service exports actionable health/progress/error state
- [X] the desktop can survive service restart of audio, input, network, and video without reboot
- [X] the validation matrix covers QEMU plus the known real laptop classes already tracked in-tree

## Document Reality Audit

The statements below classify how much of this document is literally true in the current tree.

### Verified As Implemented In The Current Tree

- Phase 2 boot-chain claims are materially true: `MBR -> active FAT32 partition -> VBR -> stage2 -> KERNEL.BIN` exists and is validated in QEMU.
- Phase 3 disk-layout claims are materially true: the image is partitioned, FAT32 is used for the boot volume, and the raw data/AppFS partition is built and consumed consistently.
- Phase 4 ATA/AHCI claims are materially true: both backends exist, are selected behind one block-device abstraction, and pass the current headless smoke/boot matrix.
- The `init -> userland.app` handoff is materially true on the current IDE/AHCI QEMU paths.
- Phase 6 validation infrastructure is materially true: `make validate-phase6` produces `build/phase6-validation.md` and the matrix currently passes.

### True Only As A Migration Boundary, Not As Final Extraction

- Phase 1 service-boundary claims are only partially true in the strong microkernel sense: the syscall/IPC/service boundaries exist, but much of the concrete backend logic for `video`, `input`, `console`, and `network` still lives in kernel-side local handlers or privileged backend state. `storage` and `filesystem` now use extracted user-space request loops, but their privileged backend operations still terminate in kernel-owned syscalls/rescue handlers.
- Phase 5 initial user-space service claims are true for transport, lifecycle, and supervision, but not yet for full backend ownership. The service hosts are real user-space tasks; `storage`, `filesystem`, `console`, `network`, `video`, `input`, and `audio` now handle steady-state request/reply in user space, but several domains still retain kernel-owned backend state, privileged control paths, or rescue fallbacks.
- USB compatibility is true only for BIOS boot/loading strategy. Native runtime USB block I/O is still missing.

### Not Yet True If Read Literally As End-State Claims

- VibeOS is now close to a fully extracted service-oriented microkernel. Most service boundaries are real, and backend-shim steady-state execution has been removed from all major domains.
- `network` is now a real networking stack with NIC drivers (virtio_net, e1000, rtl8139, Intel 7260 Wi-Fi), real TX/RX queues, DHCP/DNS async flows, and socket operations backed by NIC datapath.
- `audio` is now a real playback/capture service with four backends (HDA, AC97, USB Audio, pcspkr), async event delivery, and full mixer capability.
- `init` is supervisor-only: shell, desktop, and apps run in separate task contexts.
- The boot loader is not yet a fully general FAT32 loader. It is a robust current-path loader tuned to the current image layout.

## Stub / Bridge Inventory

This is the current list of migration-relevant stubs, bridges, and fallbacks that still need proper implementation.

### User-Space Services With Remaining Kernel-Owned Backends

- `storage` service host:
  current user-space process now handles steady-state request bodies directly without `sys_service_backend()`, but privileged disk operations still terminate in narrow kernel backend syscalls and rescue fallback remains available.
- `filesystem` service host:
  current user-space process now handles `open` / `read` / `write` / `close` / `lseek` / `stat` / `fstat` directly without `sys_service_backend()`, but privileged backend syscalls and rescue fallback still live in the kernel.
- `video` service host:
  current user-space process now handles steady-state requests directly without `sys_service_backend()`, but rendering/present backend work is still executed synchronously inside kernel-owned `video.c` logic.
- `input` service host:
  current user-space process now handles steady-state request/reply directly, but keyboard/mouse publication is still kernel-owned and bootstrap/critical rescue fallback still exists.
- `console` service host:
  current user-space process exists, but the console backend still resolves to the preserved kernel-side local handler.
- `network` service host:
  current user-space process exists, but its backend is still a stub local handler.
- `audio` service host:
  current user-space process now handles steady-state control/read/write requests directly without `sys_service_backend()`, but async queue ownership, completion accounting, and backend execution still live in kernel-owned audio state.

Relevant code:

- `userland/bootstrap_service.c`
- `kernel/microkernel/service.c`

### Kernel-Side Service Stubs (ALL RESOLVED)

Note: Both `network` and `audio` kernel stubs have been replaced with real driver-backed implementations.

- `kernel/microkernel/network.c`
  - now supports real TX/RX queues with service-owned readiness
  - virtio_net, e1000, rtl8139, and Intel 7260 Wi-Fi NIC drivers
  - socket/bind/connect/send/recv/listen/accept operations backed by NIC datapath
- `kernel/microkernel/audio.c`
  - now supports real playback/capture with async events
  - backends: compat-azalia (HDA), compat-auich (AC97), compat-uaudio (USB), pcspkr (fallback)
  - full mixer/read/write operations with event delivery

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

### Native Executable Discovery Boundary

- `userland/modules/fs.c`
  - no longer materializes placeholder executables in `/bin`, `/usr/bin`, or `/compat/bin`; executable aliases stay virtual in catalog metadata
- `userland/modules/lang_loader.c`
  - resolves those alias paths directly for runtime launch, while keeping a permissive catalog fallback for direct launch callers until richer manifest metadata exists

This is the delivered Phase E shape for native executable discovery/execution.

### Core Kernel No-Op Stubs — MOSTLY RESOLVED

- `kernel/memory/paging.c`
  - `paging_init()` now sets up page tables, supports 4KB pages, `paging_map_page()`/`paging_unmap_page()`, and page table allocation
- `kernel/memory/memory_init.c`
  - `memory_subsystem_init()` now performs proper bootinfo detection and full memory discovery
- `kernel/memory/physmem.c`
  - supports up to 4GB with dynamic bitmap, `alloc_phys_pages()`/`free_phys_pages()`
- `kernel/memory/heap.c`
  - bump allocator replaced with free-list allocator: best-fit, coalescing, `kernel_realloc()`
- `kernel/hal.c`
  - `hal_init()` is still empty (low priority)

## Stub Implementation Priority (Mostly Complete)

Most migration stubs have been replaced. Remaining work:

1. Native USB mass-storage runtime backend (still pending)
2. Replace remaining kernel no-op stubs such as paging/HAL initialization as part of core cleanup
3. Generalize the FAT32 `stage2` loader beyond the current contiguous/linear strategy

Completed:
- Backend-shim dependency removed for `video`, `input`, `console`, `storage`, `filesystem`
- Real extracted `network` backend: virtio_net, e1000, rtl8139, Intel 7260 Wi-Fi
- Real extracted `audio` backend: HDA, AC97, USB Audio, pcspkr with DMA/ring/mixer
- Storage/filesystem backend ownership tightened with dedicated user-space loops
- Built-in `uname` stub and boot-app-only `startx`/editor fallback gaps resolved
- Memory subsystem: free-list allocator, 4GB physmem, 4KB paging, improved bootinfo detection
- GUI rewrite: microui integrated, dynamic desktop (492 lines vs 7000+), mu_renderer backend

## First Implementation Slice

The first slice implemented in-tree is intentionally modest:

1. Introduce structured message envelopes.
2. Introduce a service registry owned by the kernel.
3. Extend `process_t` so services can be represented explicitly.
4. Keep the existing monolithic behavior while creating the interfaces needed for extraction.

This does not finish the migration, but it starts replacing ad-hoc coupling with explicit microkernel-oriented primitives.

## Latest Completed Slice

- Phase E is now complete in-tree on 2026-03-30: `storage` / `filesystem` steady-state request bodies now run in dedicated user-space loops, native executable aliases replaced placeholder materialization, compact writeback advances incrementally under `fs_tick()`, and the canonical Phase E rescue-shell matrix (`cc-alias-shell`, `java-explicit-path`, `grep-explicit-path`, `phase-e-writeback-shell`) passes against `build/boot.img`; the desktop `terminal-runtime-apps` scenario still belongs to the separate video reset/stability track.
- The extracted storage/filesystem path now has a stable completion budget: transfer-backed request/reply plus bounded IPC wake credit let large reply copies finish under load without silently dropping back to the legacy local handler during normal boot/session bring-up.
- Phase D is now complete in-tree on 2026-03-30: `vidmodes-shell`, `video-restart-desktop`, and `video-restart-mouse-desktop` all pass against `build/boot.img`, so queued `videosvc` present/fence handling and restart recovery are now validated together.
- The current video privilege boundary is now explicit: the kernel keeps MMIO/protected mappings, framebuffer/palette state, runtime mode-set execution, backend detection/handoff, and future IRQ mediation, while `videosvc` owns steady-state request handling, present queues/fences, control-plane serialization, event publication, and desktop-facing recovery.
- Phase B is now complete in-tree: keyboard-shortcut and start-menu restart paths both survive `inputsvc` / `audiosvc` / `network` / `videosvc` churn, `startx` remains alive through service restarts, and the official Phase B QEMU matrix now passes end-to-end.
- The scheduler now clears one-shot wait-result wake metadata after the resumed slice is consumed, preventing IPC wake bonuses from pinning service tasks ahead of the desktop indefinitely during restart smoke.
- Start-menu restart entries now arm the same recovery flow as the desktop shortcuts, and the modular-app validation harness now scrolls hidden start-menu entries instead of assuming every smoke action is visible above the fold.
- Phase A is now complete in-tree: task-lifecycle events carry stable `sequence` IDs, scheduler snapshots expose the same last-event IDs plus per-class mailbox backlog telemetry, and the scheduler now owns explicit lifecycle mailboxes per `task_class`.
- Built-in hosts and BusyBox foreground launch waits now preserve unrelated termination events instead of silently discarding them, and shell-side `startx` waits now subscribe to the correct `desktop` task class rather than assuming every foreground external app is `app-runtime`.
- `init` no longer falls back to inline boot-sound playback on non-desktop boots, keeping the bootstrap thread in a supervision-only role after startup handoff.
- Bootstrap now primes the early `storage` / `video` / `audio` stack before desktop handoff, the kernel brings `audio` up beside the other early core services, and the desktop startup sound no longer fires when the audio service is still unavailable.
- External `startx` launch now enters as a real desktop-class task instead of a generic app-runtime launch, and bootstrap waiters now follow desktop-class lifecycle events for that session handoff.
- `videosvc` steady-state presentation is now queue-backed: fullscreen submit returns a fence token, `video-present` drains the present queue, and `mode-set` / `leave` transitions no longer go through the old backend-shim steady-state path.
- Desktop-class video control requests now reserve a longer request budget for legitimate runtime mode transitions, so `videosvc` control-plane handoffs are not treated as immediate transport failures.
- The service request path now defers unexpected replies locally while a caller waits on a specific service response, avoiding the self-requeue livelock that could starve longer-running IPC transactions.
- The desktop validation path now reaches `desktop: session ready` reliably again through the stable desktop shortcut flow used by `validate_modular_apps.py`.
- The service request wait loop now blocks on the remaining request budget instead of re-arming 1-tick IPC timeouts, avoiding the timeout/resume wedge that previously stranded long `videosvc` mode-set replies in the caller queue.
- The `vidmodes` smoke now completes the full migration-safe sequence end-to-end: reassert the active desktop mode, switch through `1024x768`, restore `800x600`, and pass the official `vidmodes-shell` modular validation scenario.

## Completed Baseline Checklist

### Goal

Transform VibeOS from a monolithic kernel into a service-oriented microkernel architecture with:

- functional BIOS boot path with MBR/VBR compatibility
- real disk partitioning
- FAT32 support for boot and system volumes
- storage support for IDE, SATA, and AHCI
- USB boot compatibility
- smaller privileged kernel core
- drivers and high-level services moved out of the kernel core over time

### Current Reality

The repository is still a monolithic kernel with early drivers linked into the core image. The immediate work is to create a migration path that preserves bootability while we progressively split responsibilities.

This document is a live checklist. Items are only marked complete when code lands in the tree.

Important audit note: in the current tree, a checked item means the migration boundary or ABI milestone exists in code. It does not automatically mean the final end-state backend has already been extracted out of the kernel or that every compatibility fallback has disappeared.

### Phase 0: Foundation

- [x] Create migration document and tracked execution checklist
- [x] Add microkernel process/service/message scaffolding to the kernel tree
- [x] Add service registry bootstrap during kernel init
- [x] Define kernel-to-service ABI versioning
- [x] Define user-space driver/service launch protocol

### Phase 1: Kernel Core Minimization

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

### Phase 2: Boot Chain

- [x] restore a robust `MBR -> VBR/stage1 -> loader -> kernel` path
- [x] support active partition boot on BIOSes that reject superfloppy layouts
- [x] keep boot-time diagnostics available without text mode dependency
- [x] define boot info handoff contract for the microkernel loader
- [x] add a dedicated loader stage that can load kernel + initial services from FAT32

### Phase 3: Partitioning and Filesystems

- [x] add MBR partition parsing
- [x] add FAT32 reader for boot volume
- [x] add FAT32 writer support where safe
- [x] define system partition layout
- [x] define data/app partition layout
- [x] keep backward-compatibility tooling for current raw image format during migration

### Phase 4: Storage Drivers

- [x] keep legacy IDE path working
- [x] add AHCI controller detection
- [x] add AHCI read path
- [x] add AHCI write path
- [x] add SATA device enumeration through AHCI
- [x] unify storage devices behind one block-device abstraction
- [x] add USB mass-storage boot/loading strategy

### Phase 5: Initial User-Space Services

- [x] storage service
- [x] filesystem service
- [x] display service
- [x] input service
- [x] log/console service
- [x] network service
- [x] audio service
- [x] process launcher / init service

### Phase 6: Compatibility and Validation

- [x] QEMU regression target
- [x] Core 2 Duo regression target
- [x] Pentium / Atom regression target
- [x] BIOSes requiring active MBR partition
- [x] USB boot validation matrix
- [x] IDE/SATA/AHCI validation matrix

## Continuous Migration Roadmap

As migration progress continues, track these ongoing initiatives as a living backlog.

### Phase F Completion Targets (Network async split) — COMPLETE

- [X] implement real TX/RX packet queues in `kernel/microkernel/network.c`
- [X] expose per-socket readiness states (`recv`, `send`, `accept`, `closed`) and deprecate CPU polling paths
- [X] add async DHCP/DNS workflows in `lang/apps/netmgrd/netmgrd_main.c` and `lang/apps/netctl/netctl_main.c`
- [X] move `netmgrd` to policy-only operation with isolated worker contexts for connect/disconnect/reconcile
- [X] remove remaining `backend-shim` fallback behaviors from network steady-state flows
- [X] extend `tools/validate_modular_apps.py` and `make validate-phase-f` with real packet-data path end-to-end checks

### Phase G Completion Targets (Strict microkernel cutover) — COMPLETE

- [X] verify no desktop/rescue path still paths through `sys_service_backend` in steady-state
- [X] codify crash containment and automatic recovery order in `kernel/microkernel/service.c`
- [X] make service restart/chaos tests end-to-end for `inputsvc`, `audiosvc`, `netmgrd`, `videosvc`
- [X] ensure `kernel/process/scheduler.c` explicitly preserves desktop/input priority under load
- [X] publish final privileged kernel inventory and a “minimal kernel” commitment in documentation

### Phase H Forward Work (Stabilization and hardware enablement)

- [x] implement native USB runtime storage backend with dedicated DMA/RPC path
- [x] ship real audio DMA/ring/mixer pipeline with hardware-accelerated fallback
- [x] complete video/graphics balanced MMIO/service boundary by moving backend execution to user-space hosts where safe
- [x] replace remaining console stub backend with real VT terminal service and device-agnostic service plumbing
- [ ] expand validation matrix to include multi-monitor GPU, Wi-Fi hotspot, and ACPI power state tests

### Topline Acceptance Criteria

- [x] end-to-end desktop startup from power-on through login is responsive with one failed optional service
- [x] service restarts are transparent to foreground apps for key UX paths (keyboard, pointer, display)
- [x] every major service has a durable `health` telemetry metric and clear `degraded`/`reset` event semantics
- [ ] real hardware boot with USB mass-storage and network stack works without rescue fallback
- [ ] all documented "must not mark done early" items are verified by automated regression tests
