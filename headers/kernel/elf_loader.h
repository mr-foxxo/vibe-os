#ifndef KERNEL_ELF_LOADER_H
#define KERNEL_ELF_LOADER_H

#include <stddef.h>
#include <stdint.h>
#include <kernel/process.h>

/* Simple ELF loader used by the exec subsystem.  Given a pointer to an in-
   memory ELF image it will allocate space for the program segments, copy the
   contents, and return a freshly created process object ready to run. */

/* load a 32‑bit little‑endian ELF binary; returns NULL on failure. */
process_t *elf_load(const void *elf_data, size_t size);

#endif /* KERNEL_ELF_LOADER_H */
