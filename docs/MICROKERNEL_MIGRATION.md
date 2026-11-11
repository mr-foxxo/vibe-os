# Microkernel Migration

**Status: ALL PHASES COMPLETE (A-G)**

The canonical tracked version of this plan lives here:

- [guidelines/MICROKERNEL_MIGRATION.md](guidelines/MICROKERNEL_MIGRATION.md)

This file stays as a lightweight pointer so editor tabs, old links, and local notes do not drift into a second full copy.

## Completed Migrations

- **Phase A**: Kernel event primitives, task-lifecycle events, per-class mailboxes — DONE
- **Phase B**: Input/desktop decoupling, service-owned input path — DONE
- **Phase C**: Audio async data plane, `audiosvc` owns queues — DONE
- **Phase D**: Video/presentation split, queued `videosvc` fence model — DONE
- **Phase E**: Storage/filesystem async split, native executable discovery — DONE
- **Phase F**: Network async split, real TX/RX queues, socket readiness — DONE
- **Phase G**: Strict microkernel cutover, no backend-shim in steady state — DONE

## All Services Fully Migrated

- Video: service-owned with worker thread, no desktop hot-path into backend
- Audio: dataplane executor with async write, capture queue, event delivery
- Input: service-owned with event queue, keyboard/mouse queues, restart recovery
- Network: service-owned with socket queues, real NIC drivers, DHCP/DNS async
- Storage/Filesystem: dedicated user-space loops, incremental writeback
- Console: service-owned with VT terminal path

If you want to edit the real migration plan, edit:

- [guidelines/MICROKERNEL_MIGRATION.md](guidelines/MICROKERNEL_MIGRATION.md)
