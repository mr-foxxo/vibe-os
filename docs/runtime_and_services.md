# Runtime, Scheduler, Services, and Userland Bootstrap

This page covers everything after the kernel is alive but before normal apps are running.

## 1. Process model

Relevant files:

- [`kernel/process/process.c`](../../kernel/process/process.c)
- [`kernel/process/scheduler.c`](../../kernel/process/scheduler.c)

Current model:

- processes are kernel-owned task records with:
  - PID
  - register snapshot
  - stack allocation
  - state
  - kind
  - service type
- stacks are kernel-allocated
- there is no full address-space-per-process model yet

`process_create_with_stack()` is the main constructor used by the launch layer.

## 2. Scheduler

The scheduler is a simple ready-list scheduler with per-CPU current/cursor tracking.

Behavior:

- tracks process runtime ticks and context switches
- assigns a preferred CPU when SMP information exists
- dispatches the first runnable task with the assembly `context_switch`
- exposes task snapshot data to userland

This is enough for cooperative/preemptive kernel-task scheduling, but it is still a compact bootstrap scheduler rather than a production-grade process manager.

## 3. Launch layer

Relevant file:

- [`kernel/microkernel/launch.c`](../../kernel/microkernel/launch.c)

The launch layer creates the metadata bridge between boot decisions and runtime tasks.

For each launched task it stores:

- PID
- launch kind
- service type
- flags
- name
- boot flags copied from `BOOTINFO`
- boot and data partition geometry

This is why userland code can query `sys_launch_info()` and know whether the system was booted in desktop, safe, or rescue mode.

## 4. Service registry

Relevant files:

- [`kernel/microkernel/service.c`](../../kernel/microkernel/service.c)
- headers in [`headers/kernel/microkernel/`](../../headers/kernel/microkernel)

Registered service types:

- init
- storage
- filesystem
- video
- input
- console
- network
- audio

The service layer can:

- register a service record
- launch a worker task for a service
- restart degraded/offline workers
- route request/reply messages
- fall back to local handlers in some cases

This is a hybrid design: some capabilities look like microkernel services, but not every subsystem is fully separated from kernel-local execution.

## 5. Syscall surface

Relevant file:

- [`kernel/syscall.c`](../../kernel/syscall.c)

Important syscall groups:

- graphics:
  - clear, rect, text, flip, blit, palette, set mode, get caps
- input:
  - poll key
  - poll mouse
  - keyboard layout control
- storage:
  - raw load/save
  - read/write sectors
  - total sector count
- filesystem:
  - open, read, write, close, lseek, stat, fstat
- runtime:
  - getpid
  - yield
  - write debug
  - launch info
  - task snapshot
  - task terminate
- service messaging:
  - receive
  - send
  - backend dispatch

Important practical detail:

- keyboard polling intentionally bypasses an extra service hop and reads from the kernel PS/2 queue directly

## 6. Built-in init

Relevant file:

- [`userland/bootstrap_init.c`](../../userland/bootstrap_init.c)

This is the first scheduled bootstrap service after the kernel starts userland execution.

Responsibilities:

- initialize the userland console layer
- initialize the userland filesystem layer
- run a storage smoke test
- print the bootstrap banner
- inspect boot mode flags
- launch the external boot app path
- fall back to the built-in shell if needed

Boot-mode behavior:

- normal boot:
  - try `userland`
  - if that fails and desktop boot was requested, try `startx`
- rescue shell:
  - skip `userland`
  - remain in the bootstrap shell path

## 7. Userland filesystem layer

Relevant file:

- [`userland/modules/fs.c`](../../userland/modules/fs.c)

This is not the same thing as the kernel `ramfs`.

The userland FS layer maintains:

- a small in-memory node tree
- persisted metadata image in reserved sectors
- path-based helper functions used by the shell and desktop
- lazy registration of raw bundled assets

It also synthesizes shell-visible command stubs from the generated app catalog and can expose raw image-backed files such as wallpapers or game assets.

## 8. AppFS loading path

Relevant files:

- [`lang/include/vibe_appfs.h`](../../lang/include/vibe_appfs.h)
- [`userland/modules/lang_loader.c`](../../userland/modules/lang_loader.c)
- [`tools/build_appfs.py`](../../tools/build_appfs.py)

AppFS is the current executable app format directory, not a full filesystem.

The loader:

- reads the AppFS directory from the raw data partition
- validates magic/version/checksum
- locates an app by short name
- loads the app image from its reserved sectors
- maps it into one of the fixed virtual arenas
- supplies a host API with console, FS, storage, layout, and debug helpers

This is the main runtime execution path for modular apps.

## 9. Boot app, shell, and desktop handoff

Relevant files:

- [`userland/userland.c`](../../userland/userland.c)
- [`userland/modules/shell.c`](../../userland/modules/shell.c)

`userland.app` is the default external boot app.

Behavior:

- it queries launch info
- in normal desktop boot mode, it auto-runs `startx`
- then it enters the modular shell

The shell provides:

- line editing
- history
- tokenization
- built-in command dispatch
- external AppFS app launching through the generated catalog and language loader

## 10. Why apps are reachable even though kernel VFS is minimal

This is a common point of confusion in the tree.

The reason apps can run despite the kernel VFS still being just `ramfs` is:

- the kernel only needs enough runtime plumbing to start `init`
- `init` and the userland loader use raw sector access through syscalls
- AppFS provides app discovery and image loading outside the kernel VFS path

So the current system is operational because the custom AppFS runtime carries most of the app-loading responsibility.
