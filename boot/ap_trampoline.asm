BITS 16
ORG 0

%define AP_DEBUG_STAGE_ADDR 0x7FF0

start:
    cli
    cld
    mov byte [AP_DEBUG_STAGE_ADDR], 0x11
    xor esi, esi
    mov si, cs
    shl esi, 4
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    lgdt [cs:gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    mov byte [AP_DEBUG_STAGE_ADDR], 0x12
    jmp far dword [cs:protected_mode_ptr]

BITS 32

protected_mode:
    mov byte [AP_DEBUG_STAGE_ADDR], 0x21
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov eax, [esi + page_dir_ptr]
    test eax, eax
    jz .paging_ready
    mov cr3, eax
    mov eax, cr4
    or eax, 0x10
    mov cr4, eax
    mov eax, cr0
    or eax, 0x80000000     ; PG
    and eax, 0x9FFFFFFF    ; CD=0 NW=0 in the SAME cr0 write: never enter
                           ; paging with caches disabled. QEMU/TCG ignores
                           ; these bits; a real Core2 with CR0.CD=1 forces
                           ; the whole map uncacheable (video/heap at bus
                           ; speed). Paging-off window above is safe, so no
                           ; WBINVD is needed on this hot AP path.
    mov cr0, eax
    jmp short .paging_ready
.paging_ready:
    mov esp, [esi + stack_ptr]
    mov eax, [esi + entry_ptr]
    mov byte [AP_DEBUG_STAGE_ADDR], 0x22
    jmp eax

    ; Anchor the data section at a FIXED offset (0x100), independent of how
    ; much real-mode/PM code precedes it (as long as code stays < 0x100).
    ; Previously the data followed `align 8`, so ANY code edit silently
    ; shifted entry_ptr/stack_ptr/page_dir_ptr and the kernel patched the
    ; wrong bytes (observed: AP hung at sipi1 after a 5-byte CR0 change).
    ; The Makefile asserts code size < 0x100; smp_prepare_trampoline
    ; read-backs entry_ptr to catch drift on real hardware too.
times 0x100 - ($ - $$) db 0
gdt_start:
    dq 0
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd 0x47445431        ; "GDT1" magic — kernel verifies before patching

protected_mode_ptr:
    dd 0x504D4F44        ; "PMOD"
    dw 0x08

entry_ptr:
    dd 0x454E5452        ; "ENTR"

stack_ptr:
    dd 0x53544B52        ; "STKR"

page_dir_ptr:
    dd 0x50444952        ; "PDIR"

end:
