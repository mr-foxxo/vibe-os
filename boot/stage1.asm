BITS 16
ORG 0x7C00

%ifndef STAGE2_SECTORS
%define STAGE2_SECTORS 16
%endif

%ifndef STAGE2_LOAD_OFF
%define STAGE2_LOAD_OFF 0x7E00
%endif

%define CODE_SEG 0x08
%define DATA_SEG 0x10
%define VESA_INFO_ADDR 0x500  ; Fixed address for VESA info struct

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl
    call load_stage2
    jc disk_error

    ; Try VESA 0x101 (640x480x8), fallback to VGA 13h
    call init_vesa_mode
    cmp word [VESA_INFO_ADDR], 0
    jne vesa_ok
    ; Fallback to VGA 13h
    mov ax, 0x0013
    int 0x10
vesa_ok:

    cli
    call enable_a20
    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 0x1
    mov cr0, eax
    jmp CODE_SEG:protected_mode_start

disk_error:
    mov si, msg_disk_error
    call print_string

.halt:
    hlt
    jmp .halt

print_string:
.next_char:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp .next_char
.done:
    ret

load_stage2:
    pusha

    xor ax, ax
    mov es, ax
    mov bx, STAGE2_LOAD_OFF

    xor ch, ch
    mov cl, 0x02
    xor dh, dh
    mov dl, [boot_drive]
    mov si, STAGE2_SECTORS

.read_next:
    mov ah, 0x02
    mov al, 0x01
    int 0x13
    jc .error
    cmp al, 0x01
    jne .error

    add bx, 512

    inc cl
    cmp cl, 19
    jb .continue

    mov cl, 1
    xor dh, 1
    jnz .continue
    inc ch

.continue:
    dec si
    jnz .read_next

    clc
    popa
    ret

.error:
    stc
    popa
    ret

enable_a20:
    in al, 0x92
    or al, 0x02
    out 0x92, al
    ret

; VESA mode initialization (real mode)
; Tries to set VESA mode 0x101 (640x480x8)
; Stores result at 0x500 as struct:
;   offset 0: uint16_t mode
;   offset 2: uint32_t fb_addr
;   offset 6: uint16_t pitch
;   offset 8: uint16_t width
;   offset 10: uint16_t height
;   offset 12: uint8_t bpp
init_vesa_mode:
    pusha
    push es

    ; Initialize struct at 0x500 to all zeros
    xor ax, ax
    mov es, ax
    mov di, VESA_INFO_ADDR
    mov cx, 16
    rep stosb

    ; Get VESA controller info
    mov ax, 0x4F00
    mov di, 0x600  ; temporary buffer
    xor cx, cx
    int 0x10
    cmp ax, 0x004F
    jne init_vesa_fail

    ; Try mode 0x101
    mov ax, 0x4F01
    mov cx, 0x0101
    mov di, 0x700  ; temporary buffer
    int 0x10
    cmp ax, 0x004F
    jne init_vesa_fail

    ; Set mode 0x101 with linear fb
    mov ax, 0x4F02
    mov bx, 0x4101  ; mode + linear fb bit
    int 0x10
    cmp ax, 0x004F
    jne init_vesa_fail

    ; Success: store info at 0x500
    xor ax, ax
    mov es, ax

    mov word [VESA_INFO_ADDR + 0], 0x0101  ; mode
    
    mov ax, word [0x700 + 16]  ; LFB low word
    mov dx, word [0x700 + 18]  ; LFB high word
    mov [VESA_INFO_ADDR + 2], ax
    mov [VESA_INFO_ADDR + 4], dx
    
    mov ax, word [0x700 + 10]  ; pitch
    mov [VESA_INFO_ADDR + 6], ax
    
    mov ax, word [0x700 + 12]  ; width
    mov [VESA_INFO_ADDR + 8], ax
    
    mov ax, word [0x700 + 14]  ; height
    mov [VESA_INFO_ADDR + 10], ax
    
    mov byte [VESA_INFO_ADDR + 12], 8  ; bpp

    jmp init_vesa_done

init_vesa_fail:
    ; Struct stays zeroed

init_vesa_done:
    pop es
    popa
    ret

boot_drive db 0
msg_disk_error db "Erro ao carregar stage2", 0

align 8
gdt_start:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

BITS 32
protected_mode_start:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x9FC00

    mov eax, STAGE2_LOAD_OFF
    jmp eax

times 510 - ($ - $$) db 0
dw 0xAA55
