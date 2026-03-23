BITS 16
ORG 0x7C00

%define KERNEL_SEG 0x1000
%define KERNEL_OFF 0x0000
%define BOOTINFO_ADDR 0x8000
%define BOOTINFO_MAGIC 0x56424D49
%define E820_BUF 0x8020
%define VESA_INFO_ADDR 0x0500
%define VESA_MODEINFO_ADDR 0x9000
%define MIN_USABLE_ADDR 0x00100000
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

    mov [boot_drive],dl
    mov word [VESA_INFO_ADDR + 0], 0
    call setup_vesa

    call load_kernel
    jc disk_error

    call detect_memory
    call enable_a20

    lgdt [gdt_descriptor]
    cli
    mov eax,cr0
    or eax,1
    mov cr0,eax
    jmp CODE_SEG:pmode

load_kernel:
    pusha

    mov word [disk_address_packet.sectors],1
    mov word [disk_address_packet.offset],0
    mov word [disk_address_packet.segment],KERNEL_SEG
    mov dword [disk_address_packet.lba_low],KERNEL_START_LBA
    mov dword [disk_address_packet.lba_high],0
    mov cx,KERNEL_SECTORS

.next:
    mov si,disk_address_packet
    mov dl,[boot_drive]
    mov ah,0x42
    int 0x13
    jc .fail

    add word [disk_address_packet.segment],0x20
    inc dword [disk_address_packet.lba_low]
    adc dword [disk_address_packet.lba_high],0
    loop .next

    popa
    clc
    ret

.fail:
    popa
    stc
    ret

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

setup_vesa:
    mov word [VESA_INFO_ADDR + 0], 0

    mov ax,0x4F02
    mov bx,0x4101
    int 0x10
    cmp ax,0x004F
    jne .done

    xor ax,ax
    mov es,ax
    mov di,VESA_MODEINFO_ADDR
    mov ax,0x4F01
    mov cx,0x0101
    int 0x10
    cmp ax,0x004F
    jne .done

    mov ax,[VESA_MODEINFO_ADDR + 0]
    test ax,0x0081
    jz .done

    mov ax,0x0101
    mov [VESA_INFO_ADDR + 0],ax
    mov eax,[VESA_MODEINFO_ADDR + 40]
    mov [VESA_INFO_ADDR + 2],eax
    mov ax,[VESA_MODEINFO_ADDR + 16]
    mov [VESA_INFO_ADDR + 6],ax
    mov ax,[VESA_MODEINFO_ADDR + 18]
    mov [VESA_INFO_ADDR + 8],ax
    mov ax,[VESA_MODEINFO_ADDR + 20]
    mov [VESA_INFO_ADDR + 10],ax
    mov al,[VESA_MODEINFO_ADDR + 25]
    mov [VESA_INFO_ADDR + 12],al

.done:
    ret

enable_a20:
    in al,0x92
    or al,2
    out 0x92,al
    ret

disk_error:
.halt:
    hlt
    jmp .halt

align 8
gdt_start:
    dq 0
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end-gdt_start-1
    dd gdt_start

boot_drive db 0

disk_address_packet:
    db 16
    db 0
.sectors:
    dw 1
.offset:
    dw 0
.segment:
    dw KERNEL_SEG
.lba_low:
    dd KERNEL_START_LBA
.lba_high:
    dd 0

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
    jmp CODE_SEG:0x10000

times 510-($-$$) db 0
dw 0xAA55
