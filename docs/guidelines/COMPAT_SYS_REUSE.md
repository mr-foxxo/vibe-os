# compat/sys Reuse Plan

## Scope

The imported `compat/sys` tree is useful as a reference library for ABI shape, data structures, and driver decomposition. It is not a viable drop-in kernel port for VibeOS.

The repository metadata already says this explicitly:

- `compat/metadata/compat_inventory.txt`: `compat/sys` is "codigo de kernel de referencia"
- `compat/metadata/openbsd_inventory.txt`: `sys/` is "For reference and study only"

This also clarifies a naming issue: the imported tree is predominantly OpenBSD-style, not a ready-made FreeBSD subsystem port.

## What We Reused Now

- `headers/sys/socket.h`
  - imports the BSD socket vocabulary most extracted services will expect: `sockaddr`, `sockaddr_storage`, `msghdr`, `SOCK_*`, `AF_*`, `SO_*`, `MSG_*`
- `headers/sys/uio.h`
  - imports `struct iovec`, needed by socket/message-oriented code
- `headers/sys/audioio.h`
  - imports the audio control-plane vocabulary from BSD audio: `audio_swpar`, `audio_status`, `audio_device_t`, mixer descriptors, and the common symbolic names
- `headers/kernel/microkernel/network.h`
  - fixes a Vibe-native message ABI for socket-style operations
- `headers/kernel/microkernel/audio.h`
  - fixes a Vibe-native message ABI for audio control/data operations
- `kernel/microkernel/network.c`
  - registers `network` as a microkernel service and answers capability probes
- `kernel/microkernel/audio.c`
  - registers `audio` as a microkernel service and answers capability/device/parameter probes
- `kernel/microkernel/service.c` + `kernel/ipc/ipc.c`
  - now provide a synchronous request/reply transport used by built-in service workers, including the current storage/filesystem/video/input/console/network/audio set
  - built-in services can now degrade to their local handler when the worker transport path is unhealthy, which keeps the BSD-shaped syscall/control-plane compatibility usable while the extracted worker path is still being hardened
  - the service registry now also treats transport degradation as a supervision event and can terminate/relaunch a stale built-in worker from its saved launch metadata before later requests
- `userland/bootstrap_service.c` + syscall IPC/backend shims
  - the initial `storage/filesystem/video/input/console/network/audio` services now run as user-space service hosts launched by the microkernel bootstrap path instead of the old in-kernel worker loop
  - these hosts still invoke the preserved kernel local handlers through a backend-shim syscall, which keeps the existing compatibility logic reusable while moving service transport/lifecycle into user space first
- `kernel/microkernel/transfer.c`
  - now enforces per-transfer ownership and explicit read/write grants so shared buffers line up with the service boundary instead of behaving like global kernel scratch space
  - the service layer also keeps enough launch metadata to relaunch a missing built-in worker before request dispatch, which is a useful stepping stone before extracting more failure-prone drivers
- `kernel/microkernel/input.c`
  - now falls back to direct keyboard/mouse driver reads and layout helpers when the input worker transport degrades, and it also retries service recovery through `mk_service_ensure()` so compatibility fallback does not have to become a permanent stuck state

## Best References In compat/sys

- Network control plane:
  - `compat/sys/sys/socket.h`
  - `compat/sys/sys/domain.h`
  - `compat/sys/sys/protosw.h`
  - `compat/sys/sys/mbuf.h`
  - `compat/sys/net/if.h`
  - `compat/sys/net/route.h`
- Early network-driver candidates:
  - `compat/sys/dev/ic/ne2000.c`
  - `compat/sys/dev/ic/rtl81x9.c`
  - `compat/sys/dev/pci/if_em.c`
  - `compat/sys/dev/pci/if_igc.c`
  - `compat/sys/dev/pci/virtio_pci.c`
- Audio control and driver candidates:
  - `compat/sys/sys/audioio.h`
  - `compat/sys/dev/audio_if.h`
  - `compat/sys/dev/pci/azalia.h`
  - `compat/sys/dev/pci/auvia.c`
  - `compat/sys/dev/pci/cs4280.c`
  - `compat/sys/dev/pci/eap.c`

## What Is Still Missing

- No generic PCI enumeration and attach framework suitable for extracted drivers
- No `bus_space` / `bus_dma` compatibility layer
- No device interrupt routing contract for user-space drivers
- No real socket layer, protocol switching, `mbuf` allocator, or packet path
- No NIC RX/TX ring management outside the kernel
- No audio DMA ring, IRQ completion path, or mixer backend
- Cross-process request/reply IPC now exists for fixed-size `mk_message` envelopes, and transfer buffers now have basic owner/grant enforcement, but the model is still single-grant and does not yet provide richer shared-memory lifecycle semantics
- The current built-in-service fallback path is a compatibility bridge, not the final shape; transport-triggered relaunch now exists for built-in workers, but truly extracted drivers/services still need a richer supervisor contract and a cleaner exit/lifecycle model than the current in-kernel safety net

## Practical Migration Order

1. Keep the control-plane ABI BSD-shaped.
2. Extend the Vibe-native IPC transport with transfer-buffer-backed bulk sharing across process boundaries.
3. Add a PCI service with enumeration, BAR mapping policy, IRQ leasing, and DMA-safe buffer registration.
4. Extract one narrow network target first.
   Suggested order: `virtio-net` if we want QEMU velocity, otherwise `ne2000` for simplicity.
5. Extract one narrow audio target first.
   Suggested order: AC'97/`auvia`-class or a minimal PCI stub before attempting HD Audio / Azalia.
6. Only after the service boundaries are stable should we pull algorithmic pieces from BSD networking or audio paths.

## Hard Problems Logged Into The Plan

- `mbuf` is not a header problem; it is a memory ownership and lifetime problem.
- BSD NIC drivers assume bus abstractions and interrupt glue we do not have yet.
- BSD audio drivers assume DMA descriptors, ring refill, and wakeup semantics we do not have yet.
- Pulling full drivers before IPC and PCI policy exist would lock VibeOS back into a monolithic architecture.
