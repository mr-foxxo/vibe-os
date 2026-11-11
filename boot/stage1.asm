BITS 16
ORG 0x7C00

    jmp short boot_start
    nop
times 90-($-$$) db 0

%define STAGE2_SEG 0x0900
%define STAGE2_OFF 0x0000
%ifndef STAGE2_START_LBA
%define STAGE2_START_LBA 1
%endif
%ifndef STAGE2_SECTORS
%define STAGE2_SECTORS 8
%endif
%define BOOTDEBUG_ADDR 0x1000
%define BOOTDEBUG_MAGIC 0x47444256
%define BOOTDEBUG_DIRTY 4
%define BOOTDEBUG_LEN 5
%define BOOTDEBUG_LAST 6
%define BOOTDEBUG_TRACE 7
%define BOOTDEBUG_TRACE_MAX 48
; P0 adendo 44: crashed-trace stash slot (kernel reads + clears PLEN).
; 55-58 belong to stage2 (KEY_HI/KEY_LO/EXT/ACT); stash starts at 64.
; stage1 used to PRINT the previous trace itself and wait for a keypress --
; the keypress gate blocks unattended real-hardware boots, so the reporting
; moved to the kernel, which has serial + space).
%define BOOTDEBUG_PLEN 64
%define BOOTDEBUG_PLAST 65
%define BOOTDEBUG_PREV 66

boot_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov [boot_drive], dl
    call bootdebug_prepare
    mov al, 'S'
    call bootdebug_append

    call load_stage2
    jc disk_error

    mov dl, [boot_drive]
    jmp STAGE2_SEG:STAGE2_OFF

load_stage2:
    pusha

    mov al, 'R'
    call bootdebug_append
    mov word [disk_address_packet.sectors], STAGE2_SECTORS
    mov word [disk_address_packet.offset], STAGE2_OFF
    mov word [disk_address_packet.segment], STAGE2_SEG
    mov eax, [0x7C1C]
    add eax, STAGE2_START_LBA
    mov dword [disk_address_packet.lba_low], eax
    mov dword [disk_address_packet.lba_high], 0

    ; Real-hardware robustness (COMPLETION_ACTION_PLAN P0): the first
    ; AH=42h read after power-on often returns transient CF on real
    ; media (drive spin-up, PLL not settled, USB bridge busy); QEMU
    ; never reports it, so a single-shot read is a hardware-only
    ; failure. Bounded retry with a media reset between attempts —
    ; same discipline as stage2's read_sector_lba. INT 13h preserves
    ; DL per spec, so the drive is loaded once for the whole loop.
    mov bx, 3
    mov dl, [boot_drive]
.retry:
    mov si, disk_address_packet
    mov ah, 0x42
    int 0x13
    jnc .read_ok

    push bx
    xor ah, ah
    int 0x13
    pop bx
    dec bx
    jnz .retry

.fail:
    mov al, 'E'
    call bootdebug_append
    popa
    stc
    ret

.read_ok:
    mov al, 'L'
    call bootdebug_append
    popa
    clc
    ret

disk_error:
    mov ax, 0x0003
    int 0x10
    mov si, disk_error_msg
    call print_string
    mov si, prev_trace_msg
    call print_string
    mov si, BOOTDEBUG_ADDR + BOOTDEBUG_TRACE
    call print_string
    mov si, prev_last_msg
    call print_string
    mov al, [BOOTDEBUG_ADDR + BOOTDEBUG_LAST]
    call print_char
    call print_newline
    mov al, 'E'
    out 0xE9, al
.halt:
    hlt
    jmp .halt

bootdebug_prepare:
    cmp dword [BOOTDEBUG_ADDR], BOOTDEBUG_MAGIC
    jne .reset
    cmp byte [BOOTDEBUG_ADDR + BOOTDEBUG_DIRTY], 1
    jne .reset
    cmp byte [BOOTDEBUG_ADDR + BOOTDEBUG_LEN], 0
    je .reset

    ; stash the crashed window for the kernel to report (serial + VGA),
    ; then re-arm a fresh window below. No keypress gate: unattended boot.
    mov al, [BOOTDEBUG_ADDR + BOOTDEBUG_LEN]
    mov [BOOTDEBUG_ADDR + BOOTDEBUG_PLEN], al
    mov al, [BOOTDEBUG_ADDR + BOOTDEBUG_LAST]
    mov [BOOTDEBUG_ADDR + BOOTDEBUG_PLAST], al
    mov si, BOOTDEBUG_ADDR + BOOTDEBUG_TRACE
    mov di, BOOTDEBUG_ADDR + BOOTDEBUG_PREV
    mov cx, BOOTDEBUG_TRACE_MAX
    rep movsb

.reset:
    mov dword [BOOTDEBUG_ADDR], BOOTDEBUG_MAGIC
    mov byte [BOOTDEBUG_ADDR + BOOTDEBUG_DIRTY], 1
    mov byte [BOOTDEBUG_ADDR + BOOTDEBUG_LEN], 0
    mov byte [BOOTDEBUG_ADDR + BOOTDEBUG_LAST], '?'
    mov byte [BOOTDEBUG_ADDR + BOOTDEBUG_TRACE], 0
    ret

bootdebug_append:
    push ax
    push bx

    cmp dword [BOOTDEBUG_ADDR], BOOTDEBUG_MAGIC
    jne .done
    mov [BOOTDEBUG_ADDR + BOOTDEBUG_LAST], al
    xor bx, bx
    mov bl, [BOOTDEBUG_ADDR + BOOTDEBUG_LEN]
    cmp bl, BOOTDEBUG_TRACE_MAX - 1
    jae .done
    mov [BOOTDEBUG_ADDR + BOOTDEBUG_TRACE + bx], al
    inc bl
    mov [BOOTDEBUG_ADDR + BOOTDEBUG_LEN], bl
    mov byte [BOOTDEBUG_ADDR + BOOTDEBUG_TRACE + bx], 0

.done:
    pop bx
    pop ax
    ret

print_string:
    cld
.next:
    lodsb
    test al, al
    jz .done
    call print_char
    jmp .next
.done:
    ret

print_char:
    push bx
    mov ah, 0x0E
    mov bh, 0x00
    mov bl, 0x07
    int 0x10
    pop bx
    ret

print_newline:
    mov al, 0x0D
    call print_char
    mov al, 0x0A
    call print_char
    ret

boot_drive db 0
prev_boot_msg db 'PREV BOOT ', 0
prev_trace_msg db 'TRACE ', 0
prev_last_msg db ' LAST ', 0
continue_msg db 0x0D, 0x0A, 'PRESS KEY', 0
disk_error_msg db 'STAGE1 DISK ERROR', 0x0D, 0x0A, 0

disk_address_packet:
    db 16
    db 0
.sectors:
    dw STAGE2_SECTORS
.offset:
    dw STAGE2_OFF
.segment:
    dw STAGE2_SEG
.lba_low:
    dd STAGE2_START_LBA
.lba_high:
    dd 0

times 510-($-$$) db 0
dw 0xAA55
