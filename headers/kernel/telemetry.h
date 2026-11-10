#ifndef KERNEL_TELEMETRY_H
#define KERNEL_TELEMETRY_H

/*
 * Boot telemetry knob (closure plan item 0.3).
 *
 * The kernel's diagnostic witnesses print through small per-site budgets
 * so a hung or spammy subsystem cannot flood the serial line during a
 * slow physical boot (the T61/T400 campaigns rely on exactly these
 * traces). The cost of that safety: on a long boot only the first ~8
 * samples survive, which is often too few to see a pattern.
 *
 * `make BOOT_TELEMETRY=verbose` multiplies every witness budget by
 * VIBE_TRACE_SCALE; the DEFAULT (`quiet`) keeps each site at its historical
 * value (scale 1 — bit-for-bit the same output as before the knob).
 * This is a compile-time knob; it is intentionally NOT a runtime switch:
 * a field boot that needs verbose traces is reflashed with
 * `make BOOT_TELEMETRY=verbose imb`, and the default artifact stays quiet.
 *
 * The mode is delivered through build/generated/boot_telemetry.h (a
 * generated header, so the dependency tracking rebuilds exactly the
 * objects that observe a mode change). The fallback below keeps this
 * header usable by tools/tests that compile without the build system.
 */

#if defined(__has_include)
#  if __has_include(<boot_telemetry.h>)
#    include <boot_telemetry.h>
#  endif
#endif

#if !defined(VIBE_BOOT_TELEMETRY_VERBOSE) && !defined(VIBE_BOOT_TELEMETRY_QUIET)
/* Neither the generated header nor a -D supplied a mode: default quiet. */
#define VIBE_BOOT_TELEMETRY_QUIET 1
#endif

#if defined(VIBE_BOOT_TELEMETRY_VERBOSE) && defined(VIBE_BOOT_TELEMETRY_QUIET)
#error "BOOT_TELEMETRY must be quiet XOR verbose, not both"
#endif

#ifdef VIBE_BOOT_TELEMETRY_VERBOSE
#define VIBE_TRACE_SCALE 8u
#else
#define VIBE_TRACE_SCALE 1u
#endif

/* VIBE_TRACE_BUDGET(8) == 8 in quiet mode, 64 in verbose mode. */
#define VIBE_TRACE_BUDGET(base) ((base) * VIBE_TRACE_SCALE)

#endif /* KERNEL_TELEMETRY_H */
