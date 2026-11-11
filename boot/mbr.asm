BITS 16
ORG 0x7C00

%define RELOC_SEG 0x0700
%define RELOC_OFF 0x0000
%define RELOC_ADDR 0x7000
%define BOOTINFO_ADDR 0x8D00
%define BOOTINFO_VIDEO_MODE_SIZE 8
%define BOOTINFO_MAX_VESA_MODES 16
%define BOOTINFO_VIDEO_CATALOG_SIZE (4 + (BOOTINFO_MAX_VESA_MODES * BOOTINFO_VIDEO_MODE_SIZE))
%define BOOTINFO_SIZE (64 + BOOTINFO_VIDEO_CATALOG_SIZE)
%define BOOTINFO_WORDS (BOOTINFO_SIZE / 2)
%define BOOTINFO_MAGIC 0x544F4256
%define BOOTINFO_VERSION 2
%define BOOTINFO_FLAG_VESA_VALID 0x00000001
%define BOOTINFO_FLAG_PARTITIONS_VALID 0x00000004
%define BOOTINFO_VESA_MODE 16
%define BOOTINFO_VESA_FB 20
%define BOOTINFO_VESA_PITCH 24
%define BOOTINFO_VESA_WIDTH 26
%define BOOTINFO_VESA_HEIGHT 28
%define BOOTINFO_VESA_BPP 30
%define BOOTINFO_DISK_BOOT_LBA 48
%define BOOTINFO_DISK_BOOT_SECTORS 52
%define BOOTINFO_DISK_DATA_LBA 56
%define BOOTINFO_DISK_DATA_SECTORS 60

%ifndef IMAGE_TOTAL_SECTORS
%define IMAGE_TOTAL_SECTORS 262144
%endif
%ifndef BOOT_PARTITION_START_LBA
%define BOOT_PARTITION_START_LBA 2048
%endif
%ifndef BOOT_PARTITION_SECTORS
%define BOOT_PARTITION_SECTORS 65536
%endif
%ifndef SOFTWARE_PARTITION_START_LBA
%define SOFTWARE_PARTITION_START_LBA BOOT_PARTITION_START_LBA + BOOT_PARTITION_SECTORS
%endif
%ifndef SOFTWARE_PARTITION_SECTORS
%define SOFTWARE_PARTITION_SECTORS IMAGE_TOTAL_SECTORS - SOFTWARE_PARTITION_START_LBA
%endif
%macro TRACE_CHAR 1
%ifdef VIBELOADER_DEBUG_TRACE
    mov al, %1
    out 0xE9, al
%endif
%endmacro

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    cld
    mov si, 0x7C00
    mov di, RELOC_ADDR
    mov cx, 256
    rep movsw
    jmp 0x0000:(RELOC_ADDR + relocated_start - start)

relocated_start:
    mov [boot_drive], dl

    TRACE_CHAR 'M'
    call init_bootinfo
    TRACE_CHAR 'I'
    call load_active_vbr
    jc disk_error
    TRACE_CHAR 'J'
    jmp 0x0000:0x7C00

load_active_vbr:
    mov si, partition_table
    mov cx, 4
.find_active:
    cmp byte [si], 0x80
    je .found
    add si, 16
    loop .find_active

    mov si, partition_table
.found:
    mov dl, [boot_drive]
    mov bx, 0x55AA
    mov ah, 0x41
    int 0x13
    jc .fail
    cmp bx, 0xAA55
    jne .fail
    test cx, 1
    jz .fail

    xor ax, ax
    mov es, ax
    mov bx, 0x0600
    mov word [dap.offset], bx
    mov word [dap.segment], ax
    mov word [dap.sectors], 1
    mov ax, [si + 8]
    mov [dap.lba_low], ax
    mov ax, [si + 10]
    mov [dap.lba_low + 2], ax
    xor ax, ax
    mov [dap.lba_high], ax
    mov [dap.lba_high + 2], ax
    ; Real-hardware robustness (COMPLETION_ACTION_PLAN P0): bounded
    ; retry with media reset — the first read after power-on returns
    ; transient CF on real drives (invisible under QEMU). si is free
    ; here (dap already filled); bx is the attempt counter.
    mov bx, 3
.retry:
    mov si, dap
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jnc .read_ok

    push bx
    mov dl, [boot_drive]
    xor ah, ah
    int 0x13
    pop bx
    dec bx
    jnz .retry
    stc
    ret

.read_ok:
    ; The media reset above may clobber es; re-establish it before the
    ; rep movsw copy (ds/es were 0 at entry).
    xor ax, ax
    mov ds, ax
    mov es, ax
    cld
    mov si, 0x0600
    mov di, 0x7C00
    mov cx, 256
    rep movsw
    clc
    ret

.fail:
    stc
    ret

init_bootinfo:
    xor ax, ax
    mov es, ax
    mov di, BOOTINFO_ADDR
    mov cx, BOOTINFO_WORDS
    cld
    rep stosw
    mov dword [BOOTINFO_ADDR + 0], BOOTINFO_MAGIC
    mov dword [BOOTINFO_ADDR + 4], BOOTINFO_VERSION
    mov dword [BOOTINFO_ADDR + 8], BOOTINFO_FLAG_PARTITIONS_VALID
    mov dword [BOOTINFO_ADDR + BOOTINFO_DISK_BOOT_LBA], BOOT_PARTITION_START_LBA
    mov dword [BOOTINFO_ADDR + BOOTINFO_DISK_BOOT_SECTORS], BOOT_PARTITION_SECTORS
    mov dword [BOOTINFO_ADDR + BOOTINFO_DISK_DATA_LBA], SOFTWARE_PARTITION_START_LBA
    mov dword [BOOTINFO_ADDR + BOOTINFO_DISK_DATA_SECTORS], SOFTWARE_PARTITION_SECTORS
    ret

disk_error:
    TRACE_CHAR 'E'
.halt:
    hlt
    jmp .halt

boot_drive db 0
dap:
    db 16
    db 0
.sectors:
    dw 1
.offset:
    dw 0x7C00
.segment:
    dw 0
.lba_low:
    dd 0
.lba_high:
    dd 0

times 446-($-$$) db 0
partition_table:
db 0x80
db 0x00, 0x02, 0x00
db 0x0C
db 0xFE, 0xFF, 0xFF
dd BOOT_PARTITION_START_LBA
dd BOOT_PARTITION_SECTORS

db 0x00
db 0x00, 0x01, 0x00
db 0x83
db 0xFE, 0xFF, 0xFF
dd SOFTWARE_PARTITION_START_LBA
dd SOFTWARE_PARTITION_SECTORS

times 16 * 2 db 0

dw 0xAA55
