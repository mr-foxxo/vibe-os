# Kernel Initialization Map

Primary source: [`kernel/entry.c`](../../kernel/entry.c)

## Overview

The kernel entry path is a strict bootstrap sequence. It does not jump directly into a full userspace environment. Instead it builds just enough kernel infrastructure to launch a built-in `init` service, then that service brings up the external app world.

## Actual initialization order

`kernel_entry()` performs work in this order:

1. zero `.bss`
2. initialize debug driver
3. initialize microkernel launch registry
4. initialize microkernel service registry
5. initialize HAL
6. initialize CPU state
7. initialize GDT
8. bind video to the boot framebuffer if stage 2 provided a valid VESA mode
9. initialize text console
10. initialize LAPIC only when SMP-capable hardware is detected
11. initialize IDT and remap PIC
12. initialize PIT, keyboard, and mouse
13. enable IRQ delivery
14. initialize physical memory, paging helpers, and heap selection
15. initialize transfer support for microkernel message payload handling
16. initialize storage backend
17. initialize scheduler
18. initialize registered drivers via the driver manager
19. initialize VFS
20. initialize storage/filesystem/video/input/console/network/audio services
21. initialize syscalls
22. launch built-in `init`

## Early console strategy

The kernel uses the boot framebuffer if stage 2 already placed the machine in a valid 8-bit VESA LFB mode.

If that mode is absent or invalid, text output falls back to legacy VGA text memory at `0xB8000`.

This explains why early messages can appear either as:

- text rendered into the graphics buffer, or
- real VGA text mode output

depending on how the loader path succeeded.

## Memory selection details

The memory bootstrap is driven by:

- `physmem_usable_base()`
- `physmem_usable_end()`
- kernel image end
- framebuffer reservation
- app arena reservation

The kernel then finds the largest remaining free span and uses it as the heap.

If that calculation fails, it falls back to a hardcoded heap range:

- `0x00500000` to `0x00900000`

That fallback is a safety net, not the preferred path.

## Storage probing

`kernel_storage_init()` resets the block-device layer and then tries:

1. AHCI
2. ATA

The first successful backend becomes the active block device provider. There is no generic bus enumeration layer yet.

## Driver manager reality

The driver manager is currently minimal:

- it stores `(name, type, init)` records
- `driver_manager_init()` just calls all registered init functions

It is useful as an orchestration point, but it is not yet a full dependency-aware device manager.

## Service layer reality

The microkernel service layer is more mature than the driver manager. By the time `userland_run()` executes, the kernel has:

- service type registry slots
- launch descriptors and contexts
- scheduler-backed service tasks
- IPC/message helpers
- syscall shims that route many requests through services

Important point:

Some "service" functionality is still served directly from kernel code rather than only through isolated workers. The codebase is in transition, not in a pure microkernel end state.

## Built-in init handoff

The built-in init launch descriptor is:

- kind: service
- service type: `MK_SERVICE_INIT`
- flags: bootstrap, critical, built-in
- entry: `userland_entry()` from `userland/bootstrap_init.c`

The scheduler is entered immediately after creating this process. If the scheduler ever returns, the kernel panics.

## What the kernel does not yet do

- it does not mount a mature on-disk VFS from the raw data partition
- it does not exec general ELF binaries from disk as the main application path
- it does not expose a complete process-isolated userspace in the POSIX sense

The current executable app path still goes through the custom AppFS runtime.
