BITS 16
ORG 0x7C00

%define KERNEL_SEG 0x1000
%define KERNEL_OFF 0x0000
; Number of 512-byte sectors to read for the kernel image.
; Kernel is ~21 KiB now; give headroom.
; Kernel has grown past 24 KiB; load 96 sectors (48 KiB) to be safe.
%define KERNEL_SECTORS 96

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

    mov ax,0x0003
    int 0x10

    mov si,msg_boot
    call print

    call load_kernel
    jc disk_error

    mov si,msg_loaded
    call print

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

    mov ax,KERNEL_SEG
    mov es,ax
    xor bx,bx

    mov dh,0          ; head
    mov ch,0          ; track
    mov cl,2          ; sector (starts at 2, sector 1 is boot)

    mov dl,[boot_drive]

    mov si,KERNEL_SECTORS

.next:

    mov ah,0x02
    mov al,1
    int 0x13
    jc .fail

    add bx,512
    inc cl                   ; next sector

    ; advance CHS: if sector > 18, wrap and advance head/track
    cmp cl,19
    jl .dec_counter
    mov cl,1                 ; wrap sector
    inc dh                   ; next head
    cmp dh,2
    jl .dec_counter
    mov dh,0
    inc ch                   ; next track

.dec_counter:
    dec si
    jnz .next

    popa
    clc
    ret

.fail:

    popa
    stc
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
; print
; -----------------------------

print:

.next:
    lodsb
    test al,al
    jz .done

    mov ah,0x0E
    mov bx,0x0007
    int 0x10

    jmp .next

.done:
    ret

; -----------------------------
; error
; -----------------------------

disk_error:

    mov si,msg_error
    call print

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

msg_boot db "bootloader start",13,10,0
msg_loaded db "kernel loaded",13,10,0
msg_error db "disk error",13,10,0

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
