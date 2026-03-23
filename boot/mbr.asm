BITS 16
ORG 0x7C00

%ifndef IMAGE_TOTAL_SECTORS
%define IMAGE_TOTAL_SECTORS 3417969
%endif

%define STAGE1_LOAD_ADDR 0x7E00
%define VESA_INFO_ADDR 0x0500
%define VESA_MODEINFO_ADDR 0x9000
%define VESA_MODE_640X480X8 0x0101

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov [boot_drive], dl

    call try_enable_vesa
    call load_stage1
    jc disk_error

    jmp 0x0000:STAGE1_LOAD_ADDR

load_stage1:
    mov dl, [boot_drive]
    xor ah, ah
    int 0x13
    jc .fail

    xor ax, ax
    mov es, ax
    mov bx, STAGE1_LOAD_ADDR
    mov ah, 0x02
    mov al, 0x01
    xor ch, ch
    mov cl, 0x02
    xor dh, dh
    mov dl, [boot_drive]
    int 0x13
    jnc .ok

    mov dl, [boot_drive]
    xor ah, ah
    int 0x13
    jc .fail

    mov ah, 0x02
    mov al, 0x01
    xor ch, ch
    mov cl, 0x02
    xor dh, dh
    mov dl, [boot_drive]
    int 0x13
    jc .fail

.ok:
    clc
    ret

.fail:
    stc
    ret

try_enable_vesa:
    mov ax, 0x4F00
    mov di, VESA_MODEINFO_ADDR
    int 0x10
    cmp ax, 0x004F
    jne .done
    cmp dword [VESA_MODEINFO_ADDR], 'ASEV'
    jne .done

    mov word [VESA_INFO_ADDR + 0], 0

    mov ax, 0x4F01
    mov cx, VESA_MODE_640X480X8
    mov di, VESA_MODEINFO_ADDR
    int 0x10
    cmp ax, 0x004F
    jne .done

    mov ax, [VESA_MODEINFO_ADDR + 0]
    and ax, 0x0091
    cmp ax, 0x0091
    jne .done
    cmp byte [VESA_MODEINFO_ADDR + 25], 8
    jne .done

    mov ax, 0x4F02
    mov bx, 0x4101
    int 0x10
    cmp ax, 0x004F
    jne .done

    mov word [VESA_INFO_ADDR + 0], VESA_MODE_640X480X8
    mov eax, [VESA_MODEINFO_ADDR + 40]
    mov [VESA_INFO_ADDR + 2], eax
    mov ax, [VESA_MODEINFO_ADDR + 16]
    mov [VESA_INFO_ADDR + 6], ax
    mov ax, [VESA_MODEINFO_ADDR + 18]
    mov [VESA_INFO_ADDR + 8], ax
    mov ax, [VESA_MODEINFO_ADDR + 20]
    mov [VESA_INFO_ADDR + 10], ax
    mov al, [VESA_MODEINFO_ADDR + 25]
    mov [VESA_INFO_ADDR + 12], al

.done:
    ret

disk_error:
    mov word [0xB8000], 0x0F45
.halt:
    hlt
    jmp .halt

boot_drive db 0

times 446-($-$$) db 0

db 0x80
db 0x00, 0x02, 0x00
db 0x06
db 0xFE, 0xFF, 0xFF
dd 1
dd IMAGE_TOTAL_SECTORS - 1

times 16 * 3 db 0

dw 0xAA55
