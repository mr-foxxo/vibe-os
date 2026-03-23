BITS 16
%ifndef BOOT_LOAD_ADDR
%define BOOT_LOAD_ADDR 0x7C00
%endif
ORG BOOT_LOAD_ADDR

%define KERNEL_SEG 0x1000
%define KERNEL_OFF 0x0000
%define BOOTINFO_ADDR 0x8000
%define BOOTINFO_MAGIC 0x56424D49
%define E820_BUF 0x8020
%define VESA_INFO_ADDR 0x0500
%define VESA_MODEINFO_ADDR 0x9000
%define MIN_USABLE_ADDR 0x00100000
; Number of 512-byte sectors to read for the kernel image.
; Keep enough room for the monolithic userland while preserving the disk layout.
%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 1280
%endif
%ifndef KERNEL_START_LBA
%define KERNEL_START_LBA 1
%endif

%define CODE_SEG 0x08
%define DATA_SEG 0x10

start:

    cli
    xor ax,ax
    mov ds,ax
    mov es,ax
    mov ss,ax
    mov sp,0x7C00
    ; keep interrupts disabled until IDT is set by the kernel

    mov [boot_drive],dl

    call load_kernel
    jc disk_error

    call detect_memory

    call enable_a20

    lgdt [gdt_descriptor]

    cli                 ; ensure IF=0 before entering protected mode
    mov eax,cr0
    or eax,1
    mov cr0,eax

    jmp CODE_SEG:pmode

; -----------------------------
; kernel loader
; -----------------------------

load_kernel:

    pusha
    call disk_reset
    call load_kernel_chs

    popa
    ret

disk_reset:

    mov dl,[boot_drive]
    xor ah,ah
    int 0x13
    ret

load_kernel_chs:

    mov dl,[boot_drive]
    mov ah,0x08
    int 0x13
    jc .fail

    xor ax,ax
    mov al,cl
    and ax,0x003F
    jz .fail
    mov [chs_sectors_per_track],ax

    xor ax,ax
    mov al,dh
    inc ax
    jz .fail
    mov [chs_heads_count],ax

    mov ax,KERNEL_SEG
    mov es,ax
    mov dword [chs_lba],KERNEL_START_LBA
    mov word [chs_remaining],KERNEL_SECTORS

.next:
    mov eax,[chs_lba]
    xor edx,edx
    xor ecx,ecx
    mov cx,[chs_sectors_per_track]
    div ecx
    inc dl
    mov [chs_sector],dl

    xor edx,edx
    xor ecx,ecx
    mov cx,[chs_heads_count]
    div ecx
    cmp eax,1023
    ja .fail

    mov [chs_head],dl
    mov ch,al
    mov cl,[chs_sector]
    mov bl,ah
    and bl,0x03
    shl bl,6
    or cl,bl
    mov dh,[chs_head]
    mov dl,[boot_drive]
    mov ah,0x02
    mov al,0x01
    xor bx,bx
    int 0x13
    jc .fail

    mov ax,es
    add ax,0x20
    mov es,ax
    inc dword [chs_lba]
    dec word [chs_remaining]
    jnz .next

    clc
    ret

.fail:
    stc
    ret

; -----------------------------
; memory map
; -----------------------------

detect_memory:

    mov dword [BOOTINFO_ADDR + 0], BOOTINFO_MAGIC
    mov dword [BOOTINFO_ADDR + 4], 0x00500000
    mov dword [BOOTINFO_ADDR + 8], 0x00400000
    mov dword [BOOTINFO_ADDR + 12], 0x00900000

    xor ax,ax
    mov es,ax
    xor ebx,ebx

.next:
    mov eax,0xE820
    mov edx,0x534D4150
    mov ecx,20
    mov di,E820_BUF
    int 0x15
    jc .done
    cmp eax,0x534D4150
    jne .done
    cmp dword [E820_BUF + 16],1
    jne .cont
    cmp dword [E820_BUF + 4],0
    jne .cont
    cmp dword [E820_BUF + 12],0
    jne .cont

    mov eax,[E820_BUF + 0]
    mov edx,[E820_BUF + 8]
    test edx,edx
    jz .cont
    mov ecx,eax
    add ecx,edx
    jc .cont
    cmp ecx,MIN_USABLE_ADDR
    jbe .cont
    cmp eax,MIN_USABLE_ADDR
    jae .size_ready
    mov eax,MIN_USABLE_ADDR
    mov edx,ecx
    sub edx,eax
    jz .cont

.size_ready:
    cmp edx,[BOOTINFO_ADDR + 8]
    jbe .cont
    mov [BOOTINFO_ADDR + 4],eax
    mov [BOOTINFO_ADDR + 8],edx
    mov [BOOTINFO_ADDR + 12],ecx

.cont:
    test ebx,ebx
    jne .next

.done:
    ret

; -----------------------------
; A20
; -----------------------------

enable_a20:

    in al,0x92
    or al,2
    out 0x92,al
    ret

; -----------------------------
; error
; -----------------------------

disk_error:

.halt:
    hlt
    jmp .halt

; -----------------------------
; gdt
; -----------------------------

align 8
gdt_start:

dq 0
dq 0x00CF9A000000FFFF
dq 0x00CF92000000FFFF

gdt_end:

gdt_descriptor:
dw gdt_end-gdt_start-1
dd gdt_start

; -----------------------------
; data
; -----------------------------

boot_drive db 0
chs_sectors_per_track dw 0
chs_heads_count dw 0
chs_lba dd 0
chs_remaining dw 0
chs_head db 0
chs_sector db 0

; -----------------------------
; protected mode
; -----------------------------

BITS 32

pmode:

    cli
    mov ax,DATA_SEG
    mov ds,ax
    mov es,ax
    mov fs,ax
    mov gs,ax
    mov ss,ax

    mov esp,0x70000

    ; jump to 32-bit kernel entry at physical 0x10000 (identity mapped)
    jmp CODE_SEG:0x10000

times 510-($-$$) db 0
dw 0xAA55
