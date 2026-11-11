# Guidelines Map

This directory holds planning documents, migration notes, compatibility plans, and historical implementation guides.

These files are useful for execution and context, but they are not the canonical explanation of how the current code works. For the code-guided architecture docs, start at:

- [../README.md](../README.md)
- [../overview.md](../overview.md)

## Key Active Guides

- [COMPLETION_ACTION_PLAN.md](COMPLETION_ACTION_PLAN.md) — unified active plan for finishing the full implementation
- [MICROKERNEL_MIGRATION.md](MICROKERNEL_MIGRATION.md)
- [QUICK_BUILD.md](QUICK_BUILD.md)
- [smp.md](smp.md)

## Compatibility And Porting Plans

- [COMPAT_PLAN.md](COMPAT_PLAN.md)
- [COMPAT_PLAN2.md](COMPAT_PLAN2.md)
- [COMPAT_SYS_REUSE.md](COMPAT_SYS_REUSE.md)
- [COMPAT_GAMES_MENU_PORT_PLAN.md](COMPAT_GAMES_MENU_PORT_PLAN.md)
- [BUILD_LANGS.md](BUILD_LANGS.md)
- [java.md](java.md)
- [keyboard_layouts.md](keyboard_layouts.md)

## Integration And Bootloader Planning

- [MODULAR_APP_INTEGRATION_PLAN.md](MODULAR_APP_INTEGRATION_PLAN.md)
- [VIBELOADER_PLAN.md](VIBELOADER_PLAN.md)
- [VIBELOADER_BIOS_DEBUG_HANDOFF.md](VIBELOADER_BIOS_DEBUG_HANDOFF.md)

## Canonical Location Rules

- Keep code-reference explanations in `docs/`.
- Keep plans, migration checklists, build notes, and historical handoff docs in `docs/guidelines/`.
- Prefer pointer files over duplicated full copies when an old path still needs to exist.
