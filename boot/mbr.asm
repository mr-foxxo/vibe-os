BITS 16
ORG 0x7C00

%ifndef IMAGE_TOTAL_SECTORS
%define IMAGE_TOTAL_SECTORS 3417969
%endif
%ifndef BOOT_PARTITION_START_LBA
%define BOOT_PARTITION_START_LBA 1
%endif
%ifndef BOOT_PARTITION_SECTORS
%define BOOT_PARTITION_SECTORS 1281
%endif
%ifndef SOFTWARE_PARTITION_START_LBA
%define SOFTWARE_PARTITION_START_LBA 1282
%endif
%ifndef SOFTWARE_PARTITION_SECTORS
%define SOFTWARE_PARTITION_SECTORS IMAGE_TOTAL_SECTORS - SOFTWARE_PARTITION_START_LBA
%endif

%define STAGE1_LOAD_ADDR 0x7E00
%define VESA_INFO_ADDR 0x0500
%define VBE_MODE_INFO_ADDR 0x0600
%define VBE_MODE_640X480X8 0x0101

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [boot_drive], dl

    mov al, 'M'
    call putc
    call post_code

    call setup_vesa

    call load_stage1
    jc disk_error

    mov al, '1'
    call putc
    call post_code
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
    jc .fail

    clc
    ret

.fail:
    stc
    ret

setup_vesa:
    xor ax, ax
    mov es, ax

    mov di, VESA_INFO_ADDR
    xor ax, ax
    mov cx, 7
.clear_boot_info:
    stosw
    loop .clear_boot_info

    mov ax, 0x4F01
    mov cx, VBE_MODE_640X480X8
    mov di, VBE_MODE_INFO_ADDR
    int 0x10
    cmp ax, 0x004F
    jne .done

    mov ax, 0x4F02
    mov bx, 0x4000 | VBE_MODE_640X480X8
    int 0x10
    cmp ax, 0x004F
    jne .done

    mov si, VBE_MODE_INFO_ADDR
    mov di, VESA_INFO_ADDR

    mov ax, VBE_MODE_640X480X8
    stosw

    mov ax, [si + 0x28]
    stosw
    mov ax, [si + 0x2A]
    stosw

    mov ax, [si + 0x10]
    stosw
    mov ax, [si + 0x12]
    stosw
    mov ax, [si + 0x14]
    stosw

    mov al, [si + 0x19]
    stosb
.done:
    ret

putc:
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    ret

post_code:
    out 0x80, al
    ret

disk_error:
    mov al, 'E'
    call putc
    call post_code
.halt:
    hlt
    jmp .halt

boot_drive db 0

times 446-($-$$) db 0

db 0x80
db 0x00, 0x02, 0x00
db 0x06
db 0xFE, 0xFF, 0xFF
dd BOOT_PARTITION_START_LBA
dd BOOT_PARTITION_SECTORS

db 0x00
db 0x00, 0x01, 0x00
db 0x06
db 0xFE, 0xFF, 0xFF
dd SOFTWARE_PARTITION_START_LBA
dd SOFTWARE_PARTITION_SECTORS

times 16 * 2 db 0

dw 0xAA55
