BITS 16
ORG 0x9000

%define KERNEL_LOAD_SEG 0x1000
%define REALMODE_STACK_TOP 0xC000
%define PMODE_FAULT_STACK_TOP 0x6F000
%define BACKGROUND_LOAD_SEG 0x8000
%define BACKGROUND_DRAW_ADDR 0x00080000
%define BOOT_AUDIO_LOAD_SEG 0x8800
%define BOOT_AUDIO_LOAD_ADDR 0x00088000
%define BOOT_AUDIO_BUFFER_CAP 16384
%define BACKGROUND_SOURCE_WIDTH 192
%define BACKGROUND_SOURCE_HEIGHT 144
%define BACKGROUND_DRAW_BYTES (BACKGROUND_SOURCE_WIDTH * BACKGROUND_SOURCE_HEIGHT)
%define BACKGROUND_BUFFER_CAP (((BACKGROUND_DRAW_BYTES + 511) / 512) * 512)
%define LEGACY_VESA_ADDR 0x0500
%define SCRATCH_BUFFER 0x0600
%define VBE_INFO_ADDR 0x0A00
%define VBE_MODE_INFO_ADDR 0x0C00
%define BOOTINFO_ADDR 0x8D00
%define BOOTINFO_MAGIC 0x544F4256
%define BOOTINFO_VERSION 2
%define BOOTINFO_FLAGS 8
%define BOOTINFO_FLAG_VESA_VALID 0x00000001
%define BOOTINFO_FLAG_MEMINFO_VALID 0x00000002
%define BOOTINFO_FLAG_BOOT_TO_DESKTOP 0x00010000
%define BOOTINFO_FLAG_BOOT_SAFE_MODE 0x00020000
%define BOOTINFO_FLAG_BOOT_RESCUE_SHELL 0x00040000
%define BOOTINFO_FLAG_EXPERIMENTAL_I915_COMMIT 0x00100000
%define BOOTINFO_FLAG_FORCE_LEGACY_VIDEO 0x00200000
%define BOOTINFO_FLAG_EXPERIMENTAL_SMP 0x00400000
%define BOOTINFO_FLAG_BOOT_MODE_MASK (BOOTINFO_FLAG_BOOT_TO_DESKTOP | BOOTINFO_FLAG_BOOT_SAFE_MODE | BOOTINFO_FLAG_BOOT_RESCUE_SHELL)
%define BOOTINFO_VESA_MODE 16
%define BOOTINFO_VESA_FB 20
%define BOOTINFO_VESA_PITCH 24
%define BOOTINFO_VESA_WIDTH 26
%define BOOTINFO_VESA_HEIGHT 28
%define BOOTINFO_VESA_BPP 30
%define BOOTINFO_MEM_LARGEST_BASE 32
%define BOOTINFO_MEM_LARGEST_SIZE 36
%define BOOTINFO_MEM_LARGEST_END 40
%define BOOTINFO_VIDEO_MODE_COUNT 64
%define BOOTINFO_VIDEO_ACTIVE_INDEX 65
%define BOOTINFO_VIDEO_FALLBACK_INDEX 66
%define BOOTINFO_VIDEO_SELECTED_INDEX 67
%define BOOTINFO_VIDEO_MODES 68
%define BOOTINFO_VIDEO_MODE_SIZE 8
%define BOOTINFO_VIDEO_MODE_FIELD_MODE 0
%define BOOTINFO_VIDEO_MODE_FIELD_WIDTH 2
%define BOOTINFO_VIDEO_MODE_FIELD_HEIGHT 4
%define BOOTINFO_VIDEO_MODE_FIELD_BPP 6
%define BOOTINFO_MAX_VESA_MODES 16
%define BOOTINFO_VIDEO_CATALOG_SIZE (4 + (BOOTINFO_MAX_VESA_MODES * BOOTINFO_VIDEO_MODE_SIZE))
%define BOOTINFO_VIDEO_CATALOG_WORDS (BOOTINFO_VIDEO_CATALOG_SIZE / 2)
%define BOOTINFO_SIZE (64 + BOOTINFO_VIDEO_CATALOG_SIZE)
%define BOOTINFO_WORDS (BOOTINFO_SIZE / 2)
%define BOOTINFO_VIDEO_INDEX_NONE 0xFF
%define ROOT_NAME_LEN 11

%ifndef BOOT_PREFERRED_VIDEO_WIDTH
%define BOOT_PREFERRED_VIDEO_WIDTH 0
%endif

%ifndef BOOT_PREFERRED_VIDEO_HEIGHT
%define BOOT_PREFERRED_VIDEO_HEIGHT 0
%endif

%define CODE_SEG 0x08
%define DATA_SEG 0x10
%define CODE16_SEG 0x18
%define DATA16_SEG 0x20

%define FAT32_END_OF_CHAIN 0x0FFFFFF8
%define VBE_LFB_ENABLE 0x4000
%define MENU_ENTRY_COUNT 4
%define MENU_TIMEOUT_SECONDS 5
%define PIT_COUNTS_PER_SECOND 1193182
%define MENU_TIMEOUT_COUNTS (PIT_COUNTS_PER_SECOND * MENU_TIMEOUT_SECONDS)
%define BOOT_AUDIO_SAMPLE_RATE 4000
%define BOOT_AUDIO_SAMPLE_COUNTS ((PIT_COUNTS_PER_SECOND + (BOOT_AUDIO_SAMPLE_RATE / 2)) / BOOT_AUDIO_SAMPLE_RATE)
%define BOOT_AUDIO_PWM_COUNTS (BOOT_AUDIO_SAMPLE_COUNTS - 2)
%define GRAPHICS_MIN_FB_ADDR 0x00100000
%define GRAPHICS_MAX_WIDTH 4096
%define GRAPHICS_MAX_HEIGHT 2160
%define DEBUG_TRACE_MAX 48
%define A20_TEST_LOW 0x0500
%define A20_TEST_HIGH_SEG 0xFFFF
%define A20_TEST_HIGH_OFF 0x0510
%define A20_TEST_ALT_LOW 0x7DFE
%define A20_TEST_ALT_HIGH_SEG 0xFFFF
%define A20_TEST_ALT_HIGH_OFF 0x7E0E
%define A20_WAIT_LOOPS 0xFFFF
%define BOOTDEBUG_ADDR 0x1000
%define BOOTDEBUG_MAGIC 0x47444256
%define BOOTDEBUG_DIRTY 4
%define BOOTDEBUG_LEN 5
%define BOOTDEBUG_LAST 6
%define BOOTDEBUG_TRACE 7
%define BOOTDEBUG_TRACE_MAX 48
%define BOOTDEBUG_KEY_HI (BOOTDEBUG_TRACE + BOOTDEBUG_TRACE_MAX)
%define BOOTDEBUG_KEY_LO (BOOTDEBUG_KEY_HI + 1)
%define BOOTDEBUG_EXT (BOOTDEBUG_KEY_HI + 2)
%define BOOTDEBUG_ACT (BOOTDEBUG_KEY_HI + 3)

%macro DEBUG_CHAR 1
    mov al, %1
    mov [debug_last_code], al
%ifdef VIBELOADER_DEBUG_TRACE
    out 0xE9, al
%endif
%endmacro

%macro DEBUG_BOOT_CHAR 1
    ; Real-mode only: this also calls debug_log_char, which is assembled in BITS 16.
    DEBUG_CHAR %1
    call debug_log_char
%endmacro

%macro DEBUG_PMODE_CHAR 1
    ; Protected-mode safe variant that uses the BITS 32 logger below.
    DEBUG_CHAR %1
    call debug_log_char32
%endmacro

%macro IDT_GATE 1
    dw %1
    dw CODE_SEG
    db 0
    db 0x8E
    dw %1 >> 16
%endmacro

%macro EXCEPTION_STUB 1
pm_exception_stub_%1:
    push dword %1
    jmp pm_exception_common
%endmacro

stage2_entry:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, REALMODE_STACK_TOP
    cld
    call init_bootinfo
    call debug_log_reset
    call a20_debug_reset

    DEBUG_BOOT_CHAR '2'
    mov [boot_drive], dl
    call parse_bpb

    DEBUG_BOOT_CHAR 'F'
    call load_optional_background
    DEBUG_BOOT_CHAR 'A'
    call load_optional_boot_audio
    DEBUG_BOOT_CHAR 'V'
    call detect_vesa_modes
    DEBUG_BOOT_CHAR 'B'
    call detect_memory
    call populate_legacy_vesa_info
    call enable_a20
    xor ax, ax
    mov ds, ax
    mov es, ax
    call a20_is_enabled_any
    cmp ax, 1
    jne a20_error

.a20_ready:

    DEBUG_CHAR 'P'
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    and eax, 0x9FFFFFFF    ; PE=1, CD=0 NW=0: enable caches entering PM.
                           ; Paging is still OFF here, so this is the
                           ; textbook safe cache-enable (no WBINVD needed,
                           ; no paging boundary crossed). Hardware-real:
                           ; if BIOS left CR0.CD=1 the loader menu would
                           ; draw the VESA framebuffer at bus speed —
                           ; invisible under QEMU/TCG (ignores these bits).
    mov cr0, eax
    jmp CODE_SEG:pmode

init_bootinfo:
    push ax
    push cx
    push di

    xor ax, ax
    mov es, ax
    mov di, BOOTINFO_ADDR
    mov cx, BOOTINFO_WORDS
    cld
    rep stosw

    mov dword [BOOTINFO_ADDR + 0], BOOTINFO_MAGIC
    mov dword [BOOTINFO_ADDR + 4], BOOTINFO_VERSION
    mov byte [BOOTINFO_ADDR + BOOTINFO_VIDEO_ACTIVE_INDEX], BOOTINFO_VIDEO_INDEX_NONE
    mov byte [BOOTINFO_ADDR + BOOTINFO_VIDEO_FALLBACK_INDEX], BOOTINFO_VIDEO_INDEX_NONE
    mov byte [BOOTINFO_ADDR + BOOTINFO_VIDEO_SELECTED_INDEX], BOOTINFO_VIDEO_INDEX_NONE

    pop di
    pop cx
    pop ax
    ret

parse_bpb:
    mov ax, [0x7C0E]
    mov [reserved_sectors], ax
    mov al, [0x7C10]
    xor ah, ah
    mov [fat_count], ax
    mov eax, [0x7C24]
    mov [fat_size_sectors], eax
    mov eax, [0x7C2C]
    mov [root_cluster], eax
    mov eax, [0x7C1C]
    mov [hidden_sectors], eax
    mov al, [0x7C0D]
    mov [sectors_per_cluster], al

    movzx eax, word [reserved_sectors]
    add eax, [hidden_sectors]
    mov [fat_start_lba], eax

    movzx eax, word [fat_count]
    mul dword [fat_size_sectors]
    add eax, [fat_start_lba]
    mov [data_start_lba], eax
    ret

cluster_to_lba:
    ; eax = cluster
    sub eax, 2
    xor edx, edx
    mov dl, [sectors_per_cluster]
    mul edx
    add eax, [data_start_lba]
    ret

read_sector_lba:
    ; eax = lba, es:bx = buffer
    mov [dap.offset], bx
    mov [dap.segment], es
    mov [dap.lba_low], eax
    mov dword [dap.lba_high], 0
    mov word [dap.sectors], 1
    push cx
    mov cx, 3
.retry:
    mov si, dap
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jnc .ok
    mov dl, [boot_drive]
    xor ah, ah
    int 0x13
    loop .retry
    pop cx
    stc
    ret
.ok:
    pop cx
    clc
    ret

find_root_entry:
    mov [search_name_ptr], si
    mov eax, [root_cluster]
.next_cluster:
    mov [current_cluster], eax
    call cluster_to_lba
    mov [cluster_lba], eax
    xor ecx, ecx
    mov cl, [sectors_per_cluster]
    xor edi, edi
.next_sector:
    push cx
    push di
    mov eax, [cluster_lba]
    add eax, edi
    xor dx, dx
    mov es, dx
    mov bx, SCRATCH_BUFFER
    call read_sector_lba
    jc .fail

    xor si, si
.scan_entry:
    mov al, [SCRATCH_BUFFER + si]
    cmp al, 0x00
    je .not_found_popped
    cmp al, 0xE5
    je .advance
    cmp byte [SCRATCH_BUFFER + si + 11], 0x0F
    je .advance

    push si
    mov bx, [search_name_ptr]
    mov di, si
    mov cx, ROOT_NAME_LEN
.compare_name:
    mov al, [SCRATCH_BUFFER + di]
    cmp al, [bx]
    jne .name_mismatch
    inc di
    inc bx
    loop .compare_name
    pop si
    jmp .found
.name_mismatch:
    pop si

.advance:
    add si, 32
    cmp si, 512
    jb .scan_entry

    pop di
    pop cx
    inc edi
    loop .next_sector

    mov eax, [current_cluster]
    call fat_next_cluster
    jc .fail_no_pop
    cmp eax, FAT32_END_OF_CHAIN
    jae .not_found
    jmp .next_cluster

.found:
    xor eax, eax
    mov ax, [SCRATCH_BUFFER + si + 20]
    shl eax, 16
    mov ax, [SCRATCH_BUFFER + si + 26]
    mov [file_first_cluster], eax
    mov eax, [SCRATCH_BUFFER + si + 28]
    mov [file_size], eax
    pop di
    pop cx
    clc
    ret

.not_found_popped:
    pop di
    pop cx
.not_found:
    stc
    ret

.fail:
    pop di
    pop cx
.fail_no_pop:
    stc
    ret

fat_next_cluster:
    ; eax = cluster, returns eax = next cluster
    push ebx
    push ecx
    push edx

    mov ebx, eax
    shl ebx, 2
    mov eax, ebx
    shr eax, 9
    add eax, [fat_start_lba]
    mov [fat_sector_lba], eax
    and ebx, 511
    mov [fat_entry_offset], ebx

    xor dx, dx
    mov es, dx
    mov bx, SCRATCH_BUFFER
    mov eax, [fat_sector_lba]
    call read_sector_lba
    jc .error

    mov ebx, [fat_entry_offset]
    cmp ebx, 509
    jb .single_sector

    xor dx, dx
    mov es, dx
    mov bx, SCRATCH_BUFFER + 512
    mov eax, [fat_sector_lba]
    inc eax
    call read_sector_lba
    jc .error

.single_sector:
    mov ebx, [fat_entry_offset]
    mov eax, [SCRATCH_BUFFER + ebx]
    and eax, 0x0FFFFFFF
    clc
    jmp .done

.error:
    stc
.done:
    pop edx
    pop ecx
    pop ebx
    ret

load_file_to_memory:
    mov eax, [file_size]
    add eax, 511
    shr eax, 9
    mov [file_remaining_sectors], eax
    mov eax, [file_first_cluster]

.cluster_loop:
    cmp dword [file_remaining_sectors], 0
    je .done
    cmp eax, 2
    jb .fail

    mov [current_cluster], eax
    call cluster_to_lba
    mov [cluster_lba], eax

    xor ecx, ecx
    mov cl, [sectors_per_cluster]
    xor edi, edi
.sector_loop:
    cmp dword [file_remaining_sectors], 0
    je .done

    push cx
    push di
    mov eax, [cluster_lba]
    add eax, edi
    push eax
    mov ax, [load_segment]
    mov es, ax
    pop eax
    xor bx, bx
    call read_sector_lba
    jc .read_fail

    add word [load_segment], 0x20
    dec dword [file_remaining_sectors]
    pop di
    pop cx
    inc edi
    loop .sector_loop

    cmp dword [file_remaining_sectors], 0
    je .done

    mov eax, [current_cluster]
    call fat_next_cluster
    jc .fail
    cmp eax, FAT32_END_OF_CHAIN
    jae .fail
    jmp .cluster_loop

.read_fail:
    pop di
    pop cx
.fail:
    stc
    ret

.done:
    clc
    ret

load_optional_background:
    mov byte [background_available], 0
    mov si, background_name
    call find_root_entry
    jc .done

    mov eax, [file_size]
    cmp eax, BACKGROUND_DRAW_BYTES
    jne .done
    add eax, 511
    and eax, ~511
    cmp eax, BACKGROUND_BUFFER_CAP
    ja .done

    mov word [load_segment], BACKGROUND_LOAD_SEG
    call load_file_to_memory
    jc .done

    mov byte [background_available], 1
.done:
    clc
    ret

load_optional_boot_audio:
    mov byte [boot_audio_available], 0
    mov dword [boot_audio_size], 0
    mov si, boot_audio_name
    call find_root_entry
    jc .done

    mov eax, [file_size]
    cmp eax, 1
    jb .done
    cmp eax, BOOT_AUDIO_BUFFER_CAP
    ja .done

    mov [boot_audio_size], eax
    mov word [load_segment], BOOT_AUDIO_LOAD_SEG
    call load_file_to_memory
    jc .done

    mov byte [boot_audio_available], 1
.done:
    clc
    ret

load_kernel_file:
    mov si, kernel_name
    call find_root_entry
    jc .fail

    mov word [load_segment], KERNEL_LOAD_SEG
    call load_file_to_memory
    ret

.fail:
    stc
    ret

detect_memory:
    and dword [BOOTINFO_ADDR + BOOTINFO_FLAGS], ~BOOTINFO_FLAG_MEMINFO_VALID
    mov dword [BOOTINFO_ADDR + BOOTINFO_MEM_LARGEST_BASE], 0
    mov dword [BOOTINFO_ADDR + BOOTINFO_MEM_LARGEST_SIZE], 0
    mov dword [BOOTINFO_ADDR + BOOTINFO_MEM_LARGEST_END], 0
    xor ebx, ebx

detect_memory_e820_loop:
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 24
    mov di, SCRATCH_BUFFER
    mov dword [SCRATCH_BUFFER + 20], 1
    int 0x15
    jc detect_memory_e820_done
    cmp eax, 0x534D4150
    jne detect_memory_e820_done
    cmp dword [SCRATCH_BUFFER + 16], 1
    jne detect_memory_e820_next
    cmp dword [SCRATCH_BUFFER + 4], 0
    jne detect_memory_e820_next
    cmp dword [SCRATCH_BUFFER + 12], 0
    jne detect_memory_e820_next

    mov eax, [SCRATCH_BUFFER + 0]
    cmp eax, 0x00100000
    jae detect_memory_e820_base_ok
    mov eax, 0x00100000
detect_memory_e820_base_ok:
    mov edx, [SCRATCH_BUFFER + 0]
    add edx, [SCRATCH_BUFFER + 8]
    jc detect_memory_e820_clamp_end
    jmp detect_memory_e820_end_ok
detect_memory_e820_clamp_end:
    mov edx, 0xFFFFF000
detect_memory_e820_end_ok:
    cmp edx, eax
    jbe detect_memory_e820_next
    mov ecx, edx
    sub ecx, eax
    cmp ecx, [BOOTINFO_ADDR + BOOTINFO_MEM_LARGEST_SIZE]
    jbe detect_memory_e820_next
    mov [BOOTINFO_ADDR + BOOTINFO_MEM_LARGEST_BASE], eax
    mov [BOOTINFO_ADDR + BOOTINFO_MEM_LARGEST_SIZE], ecx
    mov [BOOTINFO_ADDR + BOOTINFO_MEM_LARGEST_END], edx

detect_memory_e820_next:
    test ebx, ebx
    jnz detect_memory_e820_loop

detect_memory_e820_done:
    cmp dword [BOOTINFO_ADDR + BOOTINFO_MEM_LARGEST_SIZE], 0
    jne detect_memory_store_bootinfo

    mov ax, 0xE801
    int 0x15
    jc detect_memory_fallback_88

    test ax, ax
    jnz detect_memory_have_low_kb
    mov ax, cx
detect_memory_have_low_kb:
    test bx, bx
    jnz detect_memory_have_high_blocks
    mov bx, dx
detect_memory_have_high_blocks:
    movzx eax, ax
    shl eax, 10
    movzx edx, bx
    shl edx, 16
    add eax, edx
    jmp detect_memory_store

detect_memory_fallback_88:
    mov ah, 0x88
    int 0x15
    jc detect_memory_done
    movzx eax, ax
    shl eax, 10

detect_memory_store:
    test eax, eax
    jz detect_memory_done
    mov dword [BOOTINFO_ADDR + BOOTINFO_MEM_LARGEST_BASE], 0x00100000
    mov [BOOTINFO_ADDR + BOOTINFO_MEM_LARGEST_SIZE], eax
    mov edx, eax
    add edx, 0x00100000
    mov [BOOTINFO_ADDR + BOOTINFO_MEM_LARGEST_END], edx
detect_memory_store_bootinfo:
    or dword [BOOTINFO_ADDR + BOOTINFO_FLAGS], BOOTINFO_FLAG_MEMINFO_VALID
detect_memory_done:
    ret

clear_video_catalog:
    push ax
    push cx
    push di

    xor ax, ax
    mov es, ax
    mov di, BOOTINFO_ADDR + BOOTINFO_VIDEO_MODE_COUNT
    mov cx, BOOTINFO_VIDEO_CATALOG_WORDS
    cld
    rep stosw
    mov byte [BOOTINFO_ADDR + BOOTINFO_VIDEO_ACTIVE_INDEX], BOOTINFO_VIDEO_INDEX_NONE
    mov byte [BOOTINFO_ADDR + BOOTINFO_VIDEO_FALLBACK_INDEX], BOOTINFO_VIDEO_INDEX_NONE
    mov byte [BOOTINFO_ADDR + BOOTINFO_VIDEO_SELECTED_INDEX], BOOTINFO_VIDEO_INDEX_NONE

    pop di
    pop cx
    pop ax
    ret

detect_vesa_modes:
    call clear_video_catalog

    xor ax, ax
    mov ds, ax
    mov es, ax
    mov di, VBE_INFO_ADDR
    mov cx, 256
    cld
    rep stosw
    mov di, VBE_INFO_ADDR
    mov dword [VBE_INFO_ADDR], 'VBE2'
    mov ax, 0x4F00
    int 0x10
    cmp ax, 0x004F
    jne .after_enum

    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ax, [VBE_INFO_ADDR + 0x10]
    mov [vesa_mode_list_seg], ax
    mov ax, [VBE_INFO_ADDR + 0x0E]
    mov [vesa_mode_list_off], ax
    cld
.enum_loop:
    mov ax, [vesa_mode_list_seg]
    mov es, ax
    mov si, [vesa_mode_list_off]
    mov ax, [es:si]
    add si, 2
    mov [vesa_mode_list_off], si
    cmp ax, 0xFFFF
    je .after_enum
    mov dx, ax
    xor ax, ax
    mov ds, ax
    mov es, ax
    call catalog_try_add_mode
    jmp .enum_loop

.after_enum:
    xor ax, ax
    mov ds, ax
    mov es, ax
    test dword [BOOTINFO_ADDR + BOOTINFO_FLAGS], BOOTINFO_FLAG_VESA_VALID
    jz .finalize
    mov dx, [BOOTINFO_ADDR + BOOTINFO_VESA_MODE]
    call catalog_try_add_mode

.finalize:
    call catalog_sort_modes_by_area
    call catalog_rebuild_fallback_index
    call catalog_finalize_selection
    call ensure_boot_video_mode
    ret

catalog_try_add_mode:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    call vesa_query_mode_info
    jc .done

    xor cx, cx
    mov cl, [BOOTINFO_ADDR + BOOTINFO_VIDEO_MODE_COUNT]
    xor si, si
.scan_existing:
    cmp si, cx
    jae .append
    mov di, si
    shl di, 3
    add di, BOOTINFO_ADDR + BOOTINFO_VIDEO_MODES

    mov ax, [VBE_MODE_INFO_ADDR + 0x12]
    cmp [di + BOOTINFO_VIDEO_MODE_FIELD_WIDTH], ax
    jne .next_existing
    mov ax, [VBE_MODE_INFO_ADDR + 0x14]
    cmp [di + BOOTINFO_VIDEO_MODE_FIELD_HEIGHT], ax
    jne .next_existing
    mov al, [VBE_MODE_INFO_ADDR + 0x19]
    cmp [di + BOOTINFO_VIDEO_MODE_FIELD_BPP], al
    jne .next_existing

    mov ax, [BOOTINFO_ADDR + BOOTINFO_VESA_MODE]
    cmp dx, ax
    je .replace_mode
    mov ax, [di + BOOTINFO_VIDEO_MODE_FIELD_MODE]
    test ax, ax
    je .replace_mode
    cmp dx, ax
    jae .maybe_mark_fallback

.replace_mode:
    mov [di + BOOTINFO_VIDEO_MODE_FIELD_MODE], dx

.maybe_mark_fallback:
    cmp word [VBE_MODE_INFO_ADDR + 0x12], 640
    jne .done
    cmp word [VBE_MODE_INFO_ADDR + 0x14], 480
    jne .done
    cmp byte [BOOTINFO_ADDR + BOOTINFO_VIDEO_FALLBACK_INDEX], BOOTINFO_VIDEO_INDEX_NONE
    jne .done
    mov ax, si
    mov [BOOTINFO_ADDR + BOOTINFO_VIDEO_FALLBACK_INDEX], al
    jmp .done

.next_existing:
    inc si
    jmp .scan_existing

.append:
    cmp byte [BOOTINFO_ADDR + BOOTINFO_VIDEO_MODE_COUNT], BOOTINFO_MAX_VESA_MODES
    jae .done

    xor bx, bx
    mov bl, [BOOTINFO_ADDR + BOOTINFO_VIDEO_MODE_COUNT]
    mov di, bx
    shl di, 3
    add di, BOOTINFO_ADDR + BOOTINFO_VIDEO_MODES

    mov [di + BOOTINFO_VIDEO_MODE_FIELD_MODE], dx
    mov ax, [VBE_MODE_INFO_ADDR + 0x12]
    mov [di + BOOTINFO_VIDEO_MODE_FIELD_WIDTH], ax
    mov ax, [VBE_MODE_INFO_ADDR + 0x14]
    mov [di + BOOTINFO_VIDEO_MODE_FIELD_HEIGHT], ax
    mov al, [VBE_MODE_INFO_ADDR + 0x19]
    mov [di + BOOTINFO_VIDEO_MODE_FIELD_BPP], al
    mov byte [di + BOOTINFO_VIDEO_MODE_FIELD_BPP + 1], 0

    mov al, [BOOTINFO_ADDR + BOOTINFO_VIDEO_MODE_COUNT]
    inc byte [BOOTINFO_ADDR + BOOTINFO_VIDEO_MODE_COUNT]

    cmp word [VBE_MODE_INFO_ADDR + 0x12], 640
    jne .done
    cmp word [VBE_MODE_INFO_ADDR + 0x14], 480
    jne .done
    mov [BOOTINFO_ADDR + BOOTINFO_VIDEO_FALLBACK_INDEX], al

.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

catalog_find_mode_index_by_mode:
    push bx
    push cx
    push di

    xor cx, cx
    mov cl, [BOOTINFO_ADDR + BOOTINFO_VIDEO_MODE_COUNT]
    xor bx, bx
.loop:
    cmp bx, cx
    jae .not_found
    mov di, bx
    shl di, 3
    add di, BOOTINFO_ADDR + BOOTINFO_VIDEO_MODES
    cmp [di + BOOTINFO_VIDEO_MODE_FIELD_MODE], dx
    je .found
    inc bx
    jmp .loop

.found:
    mov ax, bx
    jmp .done

.not_found:
    mov al, BOOTINFO_VIDEO_INDEX_NONE

.done:
    pop di
    pop cx
    pop bx
    ret

catalog_find_mode_index_by_dimensions:
    push bx
    push cx
    push di

    xor cx, cx
    mov cl, [BOOTINFO_ADDR + BOOTINFO_VIDEO_MODE_COUNT]
    xor bx, bx
.loop:
    cmp bx, cx
    jae .not_found
    mov di, bx
    shl di, 3
    add di, BOOTINFO_ADDR + BOOTINFO_VIDEO_MODES
    cmp [di + BOOTINFO_VIDEO_MODE_FIELD_WIDTH], ax
    jne .next
    cmp [di + BOOTINFO_VIDEO_MODE_FIELD_HEIGHT], dx
    je .found
.next:
    inc bx
    jmp .loop

.found:
    mov al, bl
    jmp .done

.not_found:
    mov al, BOOTINFO_VIDEO_INDEX_NONE

.done:
    pop di
    pop cx
    pop bx
    ret

catalog_finalize_selection:
    mov byte [BOOTINFO_ADDR + BOOTINFO_VIDEO_ACTIVE_INDEX], BOOTINFO_VIDEO_INDEX_NONE
    mov byte [BOOTINFO_ADDR + BOOTINFO_VIDEO_SELECTED_INDEX], BOOTINFO_VIDEO_INDEX_NONE

    test dword [BOOTINFO_ADDR + BOOTINFO_FLAGS], BOOTINFO_FLAG_VESA_VALID
    jz .select_default
    cmp byte [BOOTINFO_ADDR + BOOTINFO_VESA_BPP], 8
    jne .select_default

    mov dx, [BOOTINFO_ADDR + BOOTINFO_VESA_MODE]
    call catalog_find_mode_index_by_mode
    cmp al, BOOTINFO_VIDEO_INDEX_NONE
    je .select_default
    mov [BOOTINFO_ADDR + BOOTINFO_VIDEO_ACTIVE_INDEX], al
    mov [BOOTINFO_ADDR + BOOTINFO_VIDEO_SELECTED_INDEX], al
    ret

.select_default:
%if BOOT_PREFERRED_VIDEO_WIDTH > 0
%if BOOT_PREFERRED_VIDEO_HEIGHT > 0
    mov ax, BOOT_PREFERRED_VIDEO_WIDTH
    mov dx, BOOT_PREFERRED_VIDEO_HEIGHT
    call catalog_find_mode_index_by_dimensions
    cmp al, BOOTINFO_VIDEO_INDEX_NONE
    jne .store_selected
%endif
%endif
    cmp byte [BOOTINFO_ADDR + BOOTINFO_VIDEO_MODE_COUNT], 0
    je .done
    mov al, [BOOTINFO_ADDR + BOOTINFO_VIDEO_MODE_COUNT]
    dec al
.store_selected:
    mov [BOOTINFO_ADDR + BOOTINFO_VIDEO_SELECTED_INDEX], al
.done:
    ret

catalog_sort_modes_by_area:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    xor cx, cx
    mov cl, [BOOTINFO_ADDR + BOOTINFO_VIDEO_MODE_COUNT]
    cmp cx, 2
    jb .done
    dec cx
    mov bp, cx

.outer:
    xor si, si
    mov dx, bp
.inner:
    cmp dx, 0
    je .next_pass

    mov di, si
    shl di, 3
    add di, BOOTINFO_ADDR + BOOTINFO_VIDEO_MODES

    mov bx, si
    inc bx
    shl bx, 3
    add bx, BOOTINFO_ADDR + BOOTINFO_VIDEO_MODES

    movzx eax, word [di + BOOTINFO_VIDEO_MODE_FIELD_WIDTH]
    movzx ecx, word [di + BOOTINFO_VIDEO_MODE_FIELD_HEIGHT]
    imul eax, ecx
    mov [catalog_area_left], eax

    movzx eax, word [bx + BOOTINFO_VIDEO_MODE_FIELD_WIDTH]
    movzx ecx, word [bx + BOOTINFO_VIDEO_MODE_FIELD_HEIGHT]
    imul eax, ecx
    mov [catalog_area_right], eax

    mov eax, [catalog_area_left]
    cmp eax, [catalog_area_right]
    ja .swap
    jb .advance

    mov ax, [di + BOOTINFO_VIDEO_MODE_FIELD_WIDTH]
    cmp ax, [bx + BOOTINFO_VIDEO_MODE_FIELD_WIDTH]
    ja .swap
    jb .advance

    mov ax, [di + BOOTINFO_VIDEO_MODE_FIELD_HEIGHT]
    cmp ax, [bx + BOOTINFO_VIDEO_MODE_FIELD_HEIGHT]
    jbe .advance

.swap:
    mov ax, [di + 0]
    xchg ax, [bx + 0]
    mov [di + 0], ax

    mov ax, [di + 2]
    xchg ax, [bx + 2]
    mov [di + 2], ax

    mov ax, [di + 4]
    xchg ax, [bx + 4]
    mov [di + 4], ax

    mov ax, [di + 6]
    xchg ax, [bx + 6]
    mov [di + 6], ax

.advance:
    inc si
    dec dx
    jmp .inner

.next_pass:
    dec bp
    jnz .outer

.done:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

catalog_rebuild_fallback_index:
    push ax
    push bx
    push cx
    push di

    mov byte [BOOTINFO_ADDR + BOOTINFO_VIDEO_FALLBACK_INDEX], BOOTINFO_VIDEO_INDEX_NONE
    xor bx, bx
    xor cx, cx
    mov cl, [BOOTINFO_ADDR + BOOTINFO_VIDEO_MODE_COUNT]

.loop:
    cmp bx, cx
    jae .done
    mov di, bx
    shl di, 3
    add di, BOOTINFO_ADDR + BOOTINFO_VIDEO_MODES
    cmp word [di + BOOTINFO_VIDEO_MODE_FIELD_WIDTH], 640
    jne .next
    cmp word [di + BOOTINFO_VIDEO_MODE_FIELD_HEIGHT], 480
    jne .next
    mov [BOOTINFO_ADDR + BOOTINFO_VIDEO_FALLBACK_INDEX], bl
    jmp .done

.next:
    inc bx
    jmp .loop

.done:
    pop di
    pop cx
    pop bx
    pop ax
    ret

ensure_boot_video_mode:
    test dword [BOOTINFO_ADDR + BOOTINFO_FLAGS], BOOTINFO_FLAG_VESA_VALID
    jz .apply_selected
    cmp byte [BOOTINFO_ADDR + BOOTINFO_VESA_BPP], 8
    jne .apply_selected
    cmp byte [BOOTINFO_ADDR + BOOTINFO_VIDEO_ACTIVE_INDEX], BOOTINFO_VIDEO_INDEX_NONE
    je .sync_selected
    ret

.sync_selected:
    mov al, [BOOTINFO_ADDR + BOOTINFO_VIDEO_SELECTED_INDEX]
    cmp al, BOOTINFO_VIDEO_INDEX_NONE
    je .apply_selected
    mov [BOOTINFO_ADDR + BOOTINFO_VIDEO_ACTIVE_INDEX], al
    ret

.apply_selected:
    mov al, [BOOTINFO_ADDR + BOOTINFO_VIDEO_SELECTED_INDEX]
    cmp al, BOOTINFO_VIDEO_INDEX_NONE
    jne .have_selected
    cmp byte [BOOTINFO_ADDR + BOOTINFO_VIDEO_MODE_COUNT], 0
    je .done
    xor al, al
    mov [BOOTINFO_ADDR + BOOTINFO_VIDEO_SELECTED_INDEX], al

.have_selected:
    xor bx, bx
    mov bl, [BOOTINFO_ADDR + BOOTINFO_VIDEO_SELECTED_INDEX]
    shl bx, 3
    mov dx, [BOOTINFO_ADDR + BOOTINFO_VIDEO_MODES + bx + BOOTINFO_VIDEO_MODE_FIELD_MODE]
    call vesa_set_mode_and_store_bootinfo
.done:
    ret

vesa_query_mode_info:
    push ax
    push bx
    push cx
    push di
    push ds
    push es

    xor ax, ax
    mov ds, ax
    mov es, ax
    mov di, VBE_MODE_INFO_ADDR
    mov cx, 128
    cld
    rep stosw
    mov di, VBE_MODE_INFO_ADDR
    mov ax, 0x4F01
    mov cx, dx
    int 0x10
    cmp ax, 0x004F
    jne .fail

    xor ax, ax
    mov ds, ax
    mov es, ax
    test word [ds:VBE_MODE_INFO_ADDR + 0], 0x0001
    jz .fail
    test word [ds:VBE_MODE_INFO_ADDR + 0], 0x0010
    jz .fail
    test word [ds:VBE_MODE_INFO_ADDR + 0], 0x0080
    jz .fail
    cmp byte [ds:VBE_MODE_INFO_ADDR + 0x19], 8
    jne .fail
    cmp byte [ds:VBE_MODE_INFO_ADDR + 0x1B], 4
    jne .fail
    cmp word [ds:VBE_MODE_INFO_ADDR + 0x12], 640
    jb .fail
    cmp word [ds:VBE_MODE_INFO_ADDR + 0x14], 480
    jb .fail
    cmp word [ds:VBE_MODE_INFO_ADDR + 0x12], GRAPHICS_MAX_WIDTH
    ja .fail
    cmp word [ds:VBE_MODE_INFO_ADDR + 0x14], GRAPHICS_MAX_HEIGHT
    ja .fail
    cmp word [ds:VBE_MODE_INFO_ADDR + 0x10], 0
    je .fail
    mov ax, [ds:VBE_MODE_INFO_ADDR + 0x10]
    cmp ax, [ds:VBE_MODE_INFO_ADDR + 0x12]
    jb .fail
    cmp dword [ds:VBE_MODE_INFO_ADDR + 0x28], GRAPHICS_MIN_FB_ADDR
    jb .fail
    cmp dword [ds:VBE_MODE_INFO_ADDR + 0x28], 0
    je .fail

    pop es
    pop ds
    pop di
    pop cx
    pop bx
    pop ax
    clc
    ret

.fail:
    pop es
    pop ds
    pop di
    pop cx
    pop bx
    pop ax
    stc
    ret

vesa_store_bootinfo_from_mode_info:
    push es
    xor ax, ax
    mov es, ax

    mov [es:BOOTINFO_ADDR + BOOTINFO_VESA_MODE], dx
    mov eax, [es:VBE_MODE_INFO_ADDR + 0x28]
    mov [es:BOOTINFO_ADDR + BOOTINFO_VESA_FB], eax
    mov ax, [es:VBE_MODE_INFO_ADDR + 0x10]
    mov [es:BOOTINFO_ADDR + BOOTINFO_VESA_PITCH], ax
    mov ax, [es:VBE_MODE_INFO_ADDR + 0x12]
    mov [es:BOOTINFO_ADDR + BOOTINFO_VESA_WIDTH], ax
    mov ax, [es:VBE_MODE_INFO_ADDR + 0x14]
    mov [es:BOOTINFO_ADDR + BOOTINFO_VESA_HEIGHT], ax
    mov al, [es:VBE_MODE_INFO_ADDR + 0x19]
    mov [es:BOOTINFO_ADDR + BOOTINFO_VESA_BPP], al
    or dword [es:BOOTINFO_ADDR + BOOTINFO_FLAGS], BOOTINFO_FLAG_VESA_VALID
    pop es
    ret

vesa_set_mode_and_store_bootinfo:
    push ax
    push bx
    push ds
    push es

    xor ax, ax
    mov ds, ax
    mov es, ax
    call vesa_query_mode_info
    jc .fail

    mov ax, 0x4F02
    mov bx, dx
    or bx, VBE_LFB_ENABLE
    int 0x10
    cmp ax, 0x004F
    jne .fail

    xor ax, ax
    mov ds, ax
    mov es, ax
    call vesa_query_mode_info
    jc .fail
    call vesa_store_bootinfo_from_mode_info
    call catalog_find_mode_index_by_mode
    cmp al, BOOTINFO_VIDEO_INDEX_NONE
    je .success
    mov [BOOTINFO_ADDR + BOOTINFO_VIDEO_ACTIVE_INDEX], al
    mov [BOOTINFO_ADDR + BOOTINFO_VIDEO_SELECTED_INDEX], al

.success:
    pop es
    pop ds
    pop bx
    pop ax
    clc
    ret

.fail:
    pop es
    pop ds
    pop bx
    pop ax
    stc
    ret

populate_legacy_vesa_info:
    xor ax, ax
    mov es, ax
    mov di, LEGACY_VESA_ADDR
    mov cx, 7
    cld
    rep stosw

    test dword [BOOTINFO_ADDR + BOOTINFO_FLAGS], BOOTINFO_FLAG_VESA_VALID
    jz .done

    mov ax, [BOOTINFO_ADDR + BOOTINFO_VESA_MODE]
    mov [LEGACY_VESA_ADDR + 0], ax
    mov eax, [BOOTINFO_ADDR + BOOTINFO_VESA_FB]
    mov [LEGACY_VESA_ADDR + 2], eax
    mov ax, [BOOTINFO_ADDR + BOOTINFO_VESA_PITCH]
    mov [LEGACY_VESA_ADDR + 6], ax
    mov ax, [BOOTINFO_ADDR + BOOTINFO_VESA_WIDTH]
    mov [LEGACY_VESA_ADDR + 8], ax
    mov ax, [BOOTINFO_ADDR + BOOTINFO_VESA_HEIGHT]
    mov [LEGACY_VESA_ADDR + 10], ax
    mov al, [BOOTINFO_ADDR + BOOTINFO_VESA_BPP]
    mov [LEGACY_VESA_ADDR + 12], al

.done:
    ret

debug_log_reset:
    mov byte [debug_last_code], '?'
    mov byte [debug_trace_len], 0
    mov byte [debug_trace_buf], 0
    mov byte [pm_exception_vector], 0xFF
    ret

hex_nibble_to_ascii:
    and al, 0x0F
    cmp al, 10
    jb .digit
    add al, 'A' - 10
    ret
.digit:
    add al, '0'
    ret

debug_log_char:
    push ax
    push bx

    xor bx, bx
    mov bl, [debug_trace_len]
    cmp bx, DEBUG_TRACE_MAX - 1
    jae .done
    mov [debug_trace_buf + bx], al
    inc bl
    mov [debug_trace_len], bl
    mov byte [debug_trace_buf + bx], 0

.persist:
    cmp dword [BOOTDEBUG_ADDR], BOOTDEBUG_MAGIC
    jne .done
    cmp byte [BOOTDEBUG_ADDR + BOOTDEBUG_DIRTY], 1
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

a20_probe_settle:
    push cx
    mov cx, 0x1000
.loop:
    loop .loop
    pop cx
    ret

a20_is_enabled_any:
    push bx

    call a20_is_enabled
    mov bx, ax
    call a20_is_enabled_alt
    or ax, bx
    cmp ax, 1
    je .done

    call a20_capture_bios_state
    xor ax, ax
    cmp byte [a20_bios_state_char], '1'
    jne .done
    mov ax, 1

.done:
    pop bx
    ret

bootdebug_store_key16:
    push ax
    push bx

    cmp dword [BOOTDEBUG_ADDR], BOOTDEBUG_MAGIC
    jne .done
    cmp byte [BOOTDEBUG_ADDR + BOOTDEBUG_DIRTY], 1
    jne .done

    mov ah, al
    shr al, 4
    call hex_nibble_to_ascii
    mov [BOOTDEBUG_ADDR + BOOTDEBUG_KEY_HI], al
    mov al, ah
    call hex_nibble_to_ascii
    mov [BOOTDEBUG_ADDR + BOOTDEBUG_KEY_LO], al

    mov al, '0'
    test bl, bl
    jz .store_ext
    mov al, '1'
.store_ext:
    mov [BOOTDEBUG_ADDR + BOOTDEBUG_EXT], al
    mov byte [BOOTDEBUG_ADDR + BOOTDEBUG_ACT], '?'

.done:
    pop bx
    pop ax
    ret

bootdebug_store_action16:
    cmp dword [BOOTDEBUG_ADDR], BOOTDEBUG_MAGIC
    jne .done
    cmp byte [BOOTDEBUG_ADDR + BOOTDEBUG_DIRTY], 1
    jne .done
    mov [BOOTDEBUG_ADDR + BOOTDEBUG_ACT], al
.done:
    ret

a20_debug_reset:
    mov byte [a20_status_pre], '?'
    mov byte [a20_status_bios], '?'
    mov byte [a20_status_fast], '?'
    mov byte [a20_status_kbc], '?'
    mov byte [a20_alt_status_pre], '?'
    mov byte [a20_alt_status_bios], '?'
    mov byte [a20_alt_status_fast], '?'
    mov byte [a20_alt_status_kbc], '?'
    mov byte [a20_bios_cf], '?'
    mov byte [a20_bios_state_char], '?'
    mov byte [a20_bios_state_cf], '?'
    mov byte [a20_bios_support_cf], '?'
    mov byte [a20_port92_before], 0
    mov byte [a20_port92_value], 0
    mov byte [a20_kbc_path], '?'
    mov byte [a20_kbc_step], '?'
    mov byte [a20_kbc_outport_read], 0
    mov byte [a20_kbc_outport_write], 0
    mov word [a20_bios_ax], 0
    mov word [a20_bios_state_ax], 0
    mov word [a20_bios_support_ax], 0
    mov word [a20_bios_support_bx], 0
    ret

enable_a20:
    push ds
    push es
    push bx
    push dx
    xor ax, ax
    mov ds, ax
    mov es, ax
    call a20_capture_bios_support
    DEBUG_BOOT_CHAR 'a'
    mov si, a20_status_pre
    mov di, a20_alt_status_pre
    call a20_record_check
    cmp ax, 1
    je .done

    DEBUG_BOOT_CHAR 'b'
    mov ax, 0x2401
    int 0x15
    mov dx, ax
    pushf
    pop bx
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov [a20_bios_ax], dx
    mov byte [a20_bios_cf], '0'
    test bx, 1
    jz .bios_cf_done
    mov byte [a20_bios_cf], '1'
.bios_cf_done:
    mov si, a20_status_bios
    mov di, a20_alt_status_bios
    call a20_probe_settle
    call a20_record_check
    cmp ax, 1
    je .done

    DEBUG_BOOT_CHAR 'c'
    in al, 0x92
    mov [a20_port92_before], al
    and al, 0xFE
    or al, 2
    mov [a20_port92_value], al
    out 0x92, al
    mov si, a20_status_fast
    mov di, a20_alt_status_fast
    call a20_probe_settle
    call a20_record_check
    cmp ax, 1
    je .done

    DEBUG_BOOT_CHAR 'd'
    call a20_enable_kbc
    mov si, a20_status_kbc
    mov di, a20_alt_status_kbc
    call a20_probe_settle
    call a20_record_check
    cmp ax, 1
    je .done

.capture_bios_state:
    call a20_capture_bios_state
    cmp byte [a20_bios_state_char], '1'
    jne .done
    DEBUG_BOOT_CHAR 'q'
    jmp .done
.done:
    pop dx
    pop bx
    pop es
    pop ds
    ret

a20_record_check:
    push bx

    call a20_is_enabled
    mov byte [si], '0'
    cmp ax, 1
    jne .primary_done
    mov byte [si], '1'
.primary_done:
    mov bx, ax
    call a20_is_enabled_alt
    mov byte [di], '0'
    cmp ax, 1
    jne .alt_done
    mov byte [di], '1'
.alt_done:
    or ax, bx

    pop bx
    ret

a20_capture_bios_support:
    push ax
    push bx
    push cx

    mov ax, 0x2403
    int 0x15
    mov cx, ax
    pushf
    pop ax
    xor dx, dx
    mov ds, dx
    mov es, dx
    mov [a20_bios_support_ax], cx
    mov [a20_bios_support_bx], bx
    mov byte [a20_bios_support_cf], '0'
    test ax, 1
    jz .done
    mov byte [a20_bios_support_cf], '1'

.done:
    pop cx
    pop bx
    pop ax
    ret

a20_capture_bios_state:
    push ax
    push bx

    mov ax, 0x2402
    int 0x15
    mov bx, ax
    pushf
    pop ax
    xor dx, dx
    mov ds, dx
    mov es, dx
    mov [a20_bios_state_ax], bx
    mov byte [a20_bios_state_cf], '0'
    mov byte [a20_bios_state_char], 'X'
    test ax, 1
    jz .cf_done
    mov byte [a20_bios_state_cf], '1'
    mov byte [a20_bios_state_char], 'E'
    jmp .done

.cf_done:
    mov al, bl
    cmp al, 0
    je .disabled
    cmp al, 1
    je .enabled
    jmp .done

.disabled:
    mov byte [a20_bios_state_char], '0'
    jmp .done

.enabled:
    mov byte [a20_bios_state_char], '1'

.done:
    pop bx
    pop ax
    ret

a20_is_enabled:
    pushf
    cli
    push ds
    push es
    push si
    push di

    xor ax, ax
    mov ds, ax
    mov si, A20_TEST_LOW
    mov ax, A20_TEST_HIGH_SEG
    mov es, ax
    mov di, A20_TEST_HIGH_OFF

    mov al, [ds:si]
    mov ah, [es:di]
    push ax

    mov byte [ds:si], 0x00
    mov byte [es:di], 0xFF

    cmp byte [ds:si], 0xFF
    pop ax
    mov [es:di], ah
    mov [ds:si], al
    ; The comparison result MUST be latched before touching ax: the old
    ; form did `xor ax, ax` (which sets ZF=1!) before `je`, so this probe
    ; could never report A20 enabled and boot silently degraded to the
    ; int 15h/2402h BIOS fallback — which real BIOSes often do not
    ; implement, hard-stopping the loader on a20_error while A20 was fine.
    ; pop and mov do not modify flags, so ZF from the cmp is still live.
    mov ah, 0
    setne al

    jmp .done

.done:
    pop di
    pop si
    pop es
    pop ds
    popf
    ret

a20_is_enabled_alt:
    pushf
    cli
    push ds
    push es
    push si
    push di

    xor ax, ax
    mov ds, ax
    mov si, A20_TEST_ALT_LOW
    mov ax, A20_TEST_ALT_HIGH_SEG
    mov es, ax
    mov di, A20_TEST_ALT_HIGH_OFF

    mov al, [ds:si]
    mov ah, [es:di]
    push ax

    mov byte [ds:si], 0x55
    mov byte [es:di], 0xAA

    cmp byte [ds:si], 0xAA

    pop ax
    mov [es:di], ah
    mov [ds:si], al

    ; Same latching discipline as the primary probe: `xor ax, ax` before
    ; `je` clobbered the flags and made this probe unable to confirm A20.
    mov ah, 0
    setne al

    jmp .done

.done:
    pop di
    pop si
    pop es
    pop ds
    popf
    ret

a20_wait_input_empty:
    push cx
    mov cx, A20_WAIT_LOOPS
.loop:
    in al, 0x64
    test al, 0x02
    jz .ready
    loop .loop
    stc
    pop cx
    ret
.ready:
    clc
    pop cx
    ret

a20_wait_output_full:
    push cx
    mov cx, A20_WAIT_LOOPS
.loop:
    in al, 0x64
    test al, 0x01
    jnz .ready
    loop .loop
    stc
    pop cx
    ret
.ready:
    clc
    pop cx
    ret

a20_flush_output:
    push cx
    mov cx, A20_WAIT_LOOPS
.loop:
    in al, 0x64
    test al, 0x01
    jz .done
    in al, 0x60
    loop .loop
.done:
    pop cx
    ret

a20_enable_kbc_direct:
    mov byte [a20_kbc_path], 'D'
    mov byte [a20_kbc_step], '1'
    call a20_wait_input_empty
    jc .done
    mov al, 0xD1
    out 0x64, al

    mov byte [a20_kbc_step], '2'
    call a20_wait_input_empty
    jc .done
    mov al, 0xDF
    mov [a20_kbc_outport_write], al
    out 0x60, al
    mov byte [a20_kbc_step], '3'
    call a20_wait_input_empty

.done:
    ret

a20_enable_kbc:
    mov byte [a20_kbc_path], 'R'
    mov byte [a20_kbc_step], 'A'
    call a20_wait_input_empty
    jc .done
    call a20_flush_output
    mov al, 0xAD
    out 0x64, al

    mov byte [a20_kbc_step], 'B'
    call a20_wait_input_empty
    jc .reenable
    call a20_flush_output
    mov al, 0xD0
    out 0x64, al

    mov byte [a20_kbc_step], 'C'
    call a20_wait_output_full
    jc .try_direct
    in al, 0x60
    mov [a20_kbc_outport_read], al
    push ax

    mov byte [a20_kbc_step], 'D'
    call a20_wait_input_empty
    jc .pop_try_direct
    mov al, 0xD1
    out 0x64, al

    mov byte [a20_kbc_step], 'E'
    call a20_wait_input_empty
    jc .pop_try_direct
    pop ax
    or al, 0x03
    mov [a20_kbc_outport_write], al
    out 0x60, al
    call a20_wait_input_empty
    jmp .reenable

.pop_try_direct:
    pop ax
.try_direct:
    call a20_enable_kbc_direct

.reenable:
    mov byte [a20_kbc_step], 'F'
    call a20_wait_input_empty
    mov al, 0xAE
    out 0x64, al
    mov byte [a20_kbc_step], 'G'

.done:
    ret

disk_error:
    DEBUG_BOOT_CHAR 'X'
    mov ax, 0x0003
    int 0x10
    mov si, disk_error_msg
    call print_string
    call print_newline
    mov si, last_label
    call print_string
    mov al, [debug_last_code]
    call print_char
    call print_newline
    mov si, trace_label
    call print_string
    mov si, debug_trace_buf
    call print_string
.halt:
    hlt
    jmp .halt

a20_error:
    DEBUG_BOOT_CHAR 'A'
    mov ax, 0x0003
    int 0x10
    mov si, a20_error_msg
    call print_string
    call print_newline
    mov si, last_label
    call print_string
    mov al, [debug_last_code]
    call print_char
    call print_newline
    mov si, trace_label
    call print_string
    mov si, debug_trace_buf
    call print_string
    call print_newline
    mov si, a20_status_label
    call print_string
    mov al, [a20_status_pre]
    call print_char
    mov si, a20_status_bios_label
    call print_string
    mov al, [a20_status_bios]
    call print_char
    mov si, a20_status_fast_label
    call print_string
    mov al, [a20_status_fast]
    call print_char
    mov si, a20_status_kbc_label
    call print_string
    mov al, [a20_status_kbc]
    call print_char
    call print_newline
    mov si, a20_alt_status_label
    call print_string
    mov al, [a20_alt_status_pre]
    call print_char
    mov si, a20_alt_status_bios_label
    call print_string
    mov al, [a20_alt_status_bios]
    call print_char
    mov si, a20_alt_status_fast_label
    call print_string
    mov al, [a20_alt_status_fast]
    call print_char
    mov si, a20_alt_status_kbc_label
    call print_string
    mov al, [a20_alt_status_kbc]
    call print_char
    call print_newline
    mov si, a20_bios_label
    call print_string
    mov ax, [a20_bios_ax]
    call print_hex_word
    mov si, a20_cf_label
    call print_string
    mov al, [a20_bios_cf]
    call print_char
    mov si, a20_port92_label
    call print_string
    mov al, [a20_port92_before]
    call print_hex_byte
    mov al, '>'
    call print_char
    mov al, [a20_port92_value]
    call print_hex_byte
    mov si, a20_kbc_label
    call print_string
    mov al, [a20_kbc_path]
    call print_char
    call print_newline
    mov si, a20_bios_state_label
    call print_string
    mov al, [a20_bios_state_char]
    call print_char
    mov si, a20_bios_ax_label
    call print_string
    mov ax, [a20_bios_state_ax]
    call print_hex_word
    mov si, a20_cf_label
    call print_string
    mov al, [a20_bios_state_cf]
    call print_char
    call print_newline
    mov si, a20_bios_support_label
    call print_string
    mov ax, [a20_bios_support_ax]
    call print_hex_word
    mov si, a20_bx_label
    call print_string
    mov ax, [a20_bios_support_bx]
    call print_hex_word
    mov si, a20_cf_label
    call print_string
    mov al, [a20_bios_support_cf]
    call print_char
    call print_newline
    mov si, a20_kbc_debug_label
    call print_string
    mov al, [a20_kbc_step]
    call print_char
    mov si, a20_kbc_read_label
    call print_string
    mov al, [a20_kbc_outport_read]
    call print_hex_byte
    mov si, a20_kbc_write_label
    call print_string
    mov al, [a20_kbc_outport_write]
    call print_hex_byte
    call print_newline
    mov si, a20_legend_msg
    call print_string
.halt:
    hlt
    jmp .halt

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

print_hex_nibble:
    and al, 0x0F
    cmp al, 10
    jb .digit
    add al, 'A' - 10
    jmp print_char
.digit:
    add al, '0'
    jmp print_char

print_hex_byte:
    push ax
    mov ah, al
    shr al, 4
    call print_hex_nibble
    mov al, ah
    call print_hex_nibble
    pop ax
    ret

print_hex_word:
    push ax
    mov al, ah
    call print_hex_byte
    pop ax
    call print_hex_byte
    ret

print_hex_dword:
    push eax
    shr eax, 16
    call print_hex_word
    pop eax
    call print_hex_word
    ret

align 8
gdt_start:
    dq 0
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
    dq 0x00009A000000FFFF
    dq 0x000092000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start
realmode_idtr:
    dw 0x03FF
    dd 0x00000000

kernel_name db 'KERNEL  BIN'
background_name db 'VIBEBG  BIN'
boot_audio_name db 'VIBEBOOTRAW'
disk_error_msg db 'VIBELOADER ERROR ', 0
a20_error_msg db 'VIBELOADER A20 FAIL', 0
last_label db 'LAST ', 0
trace_label db 'TRACE ', 0
a20_status_label db 'A20 P', 0
a20_status_bios_label db ' B', 0
a20_status_fast_label db ' C', 0
a20_status_kbc_label db ' D', 0
a20_alt_status_label db 'ALT P', 0
a20_alt_status_bios_label db ' B', 0
a20_alt_status_fast_label db ' C', 0
a20_alt_status_kbc_label db ' D', 0
a20_bios_label db 'BIOS ', 0
a20_cf_label db ' CF', 0
a20_port92_label db ' P92 ', 0
a20_kbc_label db ' KBC ', 0
a20_bios_state_label db '2402 ', 0
a20_bios_ax_label db ' AX', 0
a20_bios_support_label db '2403 AX', 0
a20_bx_label db ' BX', 0
a20_kbc_debug_label db 'KDBG ', 0
a20_kbc_read_label db ' R', 0
a20_kbc_write_label db ' W', 0
a20_legend_msg db 'P=PRE B=INT15 C=92 D=8042', 0
boot_drive db 0
background_available db 0
boot_audio_available db 0
boot_audio_played db 0
boot_audio_saved_port61 db 0
debug_last_code db '?'
debug_trace_len db 0
debug_trace_buf times DEBUG_TRACE_MAX db 0
a20_status_pre db '?'
a20_status_bios db '?'
a20_status_fast db '?'
a20_status_kbc db '?'
a20_alt_status_pre db '?'
a20_alt_status_bios db '?'
a20_alt_status_fast db '?'
a20_alt_status_kbc db '?'
a20_bios_cf db '?'
a20_bios_state_char db '?'
a20_bios_state_cf db '?'
a20_bios_support_cf db '?'
a20_port92_before db 0
a20_port92_value db 0
a20_kbc_path db '?'
a20_kbc_step db '?'
a20_kbc_outport_read db 0
a20_kbc_outport_write db 0
align 2
a20_bios_ax dw 0
a20_bios_state_ax dw 0
a20_bios_support_ax dw 0
a20_bios_support_bx dw 0
reserved_sectors dw 0
fat_count dw 0
fat_size_sectors dd 0
root_cluster dd 0
hidden_sectors dd 0
fat_start_lba dd 0
data_start_lba dd 0
current_cluster dd 0
cluster_lba dd 0
fat_sector_lba dd 0
fat_entry_offset dd 0
file_first_cluster dd 0
file_size dd 0
file_remaining_sectors dd 0
sectors_per_cluster db 0
load_segment dw KERNEL_LOAD_SEG
search_name_ptr dw 0
vesa_mode_list_seg dw 0
vesa_mode_list_off dw 0
boot_audio_prev_pit dw 0
align 4
boot_audio_size dd 0
catalog_area_left dd 0
catalog_area_right dd 0

dap:
    db 16
    db 0
.sectors:
    dw 1
.offset:
    dw 0
.segment:
    dw 0
.lba_low:
    dd 0
.lba_high:
    dd 0

BITS 32
debug_log_char32:
    push eax
    push ebx

    xor ebx, ebx
    mov bl, [debug_trace_len]
    cmp ebx, DEBUG_TRACE_MAX - 1
    jae .persist
    mov [debug_trace_buf + ebx], al
    inc bl
    mov [debug_trace_len], bl
    mov byte [debug_trace_buf + ebx], 0

.persist:
    cmp dword [BOOTDEBUG_ADDR], BOOTDEBUG_MAGIC
    jne .done
    cmp byte [BOOTDEBUG_ADDR + BOOTDEBUG_DIRTY], 1
    jne .done
    mov [BOOTDEBUG_ADDR + BOOTDEBUG_LAST], al
    xor ebx, ebx
    mov bl, [BOOTDEBUG_ADDR + BOOTDEBUG_LEN]
    cmp bl, BOOTDEBUG_TRACE_MAX - 1
    jae .done
    mov [BOOTDEBUG_ADDR + BOOTDEBUG_TRACE + ebx], al
    inc bl
    mov [BOOTDEBUG_ADDR + BOOTDEBUG_LEN], bl
    mov byte [BOOTDEBUG_ADDR + BOOTDEBUG_TRACE + ebx], 0

.done:
    pop ebx
    pop eax
    ret

hex_nibble_to_ascii32:
    and al, 0x0F
    cmp al, 10
    jb .digit
    add al, 'A' - 10
    ret
.digit:
    add al, '0'
    ret

bootdebug_store_key32:
    push eax
    push ebx

    cmp dword [BOOTDEBUG_ADDR], BOOTDEBUG_MAGIC
    jne .done
    cmp byte [BOOTDEBUG_ADDR + BOOTDEBUG_DIRTY], 1
    jne .done

    mov ah, al
    shr al, 4
    call hex_nibble_to_ascii32
    mov [BOOTDEBUG_ADDR + BOOTDEBUG_KEY_HI], al
    mov al, ah
    call hex_nibble_to_ascii32
    mov [BOOTDEBUG_ADDR + BOOTDEBUG_KEY_LO], al

    mov al, '0'
    test bl, bl
    jz .store_ext
    mov al, '1'
.store_ext:
    mov [BOOTDEBUG_ADDR + BOOTDEBUG_EXT], al
    mov byte [BOOTDEBUG_ADDR + BOOTDEBUG_ACT], '?'

.done:
    pop ebx
    pop eax
    ret

bootdebug_store_action32:
    cmp dword [BOOTDEBUG_ADDR], BOOTDEBUG_MAGIC
    jne .done
    cmp byte [BOOTDEBUG_ADDR + BOOTDEBUG_DIRTY], 1
    jne .done
    mov [BOOTDEBUG_ADDR + BOOTDEBUG_ACT], al
.done:
    ret

pmode_install_idt:
    push eax
    push ecx
    push esi
    push edi

    lea esi, [pm_exception_stub_ptrs]
    lea edi, [pm_exception_idt]
    mov ecx, 32
.fill:
    lodsd
    mov word [edi + 0], ax
    mov word [edi + 2], CODE_SEG
    mov byte [edi + 4], 0
    mov byte [edi + 5], 0x8E
    shr eax, 16
    mov word [edi + 6], ax
    add edi, 8
    loop .fill

    lidt [pm_exception_idtr]
    pop edi
    pop esi
    pop ecx
    pop eax
    ret

pm_exception_common:
    cli
    xor eax, eax
    mov al, [esp]
    mov [pm_exception_vector], al
    mov esp, PMODE_FAULT_STACK_TOP

    mov al, 'X'
    mov [debug_last_code], al
    call debug_log_char32

    mov al, [pm_exception_vector]
    shr al, 4
    call hex_nibble_to_ascii32
    mov [debug_last_code], al
    call debug_log_char32

    mov al, [pm_exception_vector]
    call hex_nibble_to_ascii32
    mov [debug_last_code], al
    call debug_log_char32

    jmp pmode_to_real_for_debug_halt

EXCEPTION_STUB 0
EXCEPTION_STUB 1
EXCEPTION_STUB 2
EXCEPTION_STUB 3
EXCEPTION_STUB 4
EXCEPTION_STUB 5
EXCEPTION_STUB 6
EXCEPTION_STUB 7
EXCEPTION_STUB 8
EXCEPTION_STUB 9
EXCEPTION_STUB 10
EXCEPTION_STUB 11
EXCEPTION_STUB 12
EXCEPTION_STUB 13
EXCEPTION_STUB 14
EXCEPTION_STUB 15
EXCEPTION_STUB 16
EXCEPTION_STUB 17
EXCEPTION_STUB 18
EXCEPTION_STUB 19
EXCEPTION_STUB 20
EXCEPTION_STUB 21
EXCEPTION_STUB 22
EXCEPTION_STUB 23
EXCEPTION_STUB 24
EXCEPTION_STUB 25
EXCEPTION_STUB 26
EXCEPTION_STUB 27
EXCEPTION_STUB 28
EXCEPTION_STUB 29
EXCEPTION_STUB 30
EXCEPTION_STUB 31

align 8
pm_exception_stub_ptrs:
    dd pm_exception_stub_0
    dd pm_exception_stub_1
    dd pm_exception_stub_2
    dd pm_exception_stub_3
    dd pm_exception_stub_4
    dd pm_exception_stub_5
    dd pm_exception_stub_6
    dd pm_exception_stub_7
    dd pm_exception_stub_8
    dd pm_exception_stub_9
    dd pm_exception_stub_10
    dd pm_exception_stub_11
    dd pm_exception_stub_12
    dd pm_exception_stub_13
    dd pm_exception_stub_14
    dd pm_exception_stub_15
    dd pm_exception_stub_16
    dd pm_exception_stub_17
    dd pm_exception_stub_18
    dd pm_exception_stub_19
    dd pm_exception_stub_20
    dd pm_exception_stub_21
    dd pm_exception_stub_22
    dd pm_exception_stub_23
    dd pm_exception_stub_24
    dd pm_exception_stub_25
    dd pm_exception_stub_26
    dd pm_exception_stub_27
    dd pm_exception_stub_28
    dd pm_exception_stub_29
    dd pm_exception_stub_30
    dd pm_exception_stub_31

pm_exception_idt:
    times (32 * 8) db 0
pm_exception_idt_end:

pm_exception_idtr:
    dw pm_exception_idt_end - pm_exception_idt - 1
    dd pm_exception_idt

pmode:
    cli
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x70000
    cld
    call pmode_install_idt

    mov byte [menu_initialized], 0
    DEBUG_PMODE_CHAR 'Q'
    call vibeloader_menu
    jmp pmode_to_real_for_kernel_boot

vibeloader_menu:
    DEBUG_PMODE_CHAR 'M'
    test dword [BOOTINFO_ADDR + BOOTINFO_FLAGS], BOOTINFO_FLAG_VESA_VALID
    jz vibeloader_menu_default_boot
    DEBUG_PMODE_CHAR '1'
    cmp byte [BOOTINFO_ADDR + BOOTINFO_VESA_BPP], 8
    jne vibeloader_menu_default_boot
    DEBUG_PMODE_CHAR '2'
    call graphics_bootinfo_sane32
    jc vibeloader_menu_default_boot

%ifndef VIBELOADER_SKIP_SET_PALETTE
    call set_desktop_palette
%endif
    DEBUG_PMODE_CHAR 'C'
    call menu_compute_layout
    cmp byte [menu_initialized], 0
    jne vibeloader_menu_resume
    call menu_flush_keyboard_buffer
    mov dword [menu_selection], 0
    mov byte [menu_initialized], 1
    mov byte [menu_force_full_redraw], 1
    mov byte [menu_dirty], 1
    call menu_render
    mov byte [menu_dirty], 0
    call boot_audio_maybe_play_once
    call menu_restart_timer
    mov byte [menu_dirty], 1

vibeloader_menu_resume:
    mov byte [menu_dirty], 1
vibeloader_menu_loop:
    cmp byte [menu_timeout_paused], 0
    jne vibeloader_menu_check_keys
    call pit_read_counter
    movzx ecx, word [menu_prev_pit]
    movzx edx, ax
    mov [menu_prev_pit], ax
    sub ecx, edx
    and ecx, 0xFFFF
    add dword [menu_elapsed_counts], ecx

    mov eax, [menu_elapsed_counts]
    cmp eax, MENU_TIMEOUT_COUNTS
    jae vibeloader_menu_boot_now

    xor edx, edx
    mov ecx, PIT_COUNTS_PER_SECOND
    div ecx
    mov ebx, MENU_TIMEOUT_SECONDS
    sub ebx, eax
    cmp ebx, [menu_seconds_left]
    je vibeloader_menu_check_keys
    mov [menu_seconds_left], ebx
    mov byte [menu_dirty], 1

vibeloader_menu_check_keys:
    call menu_poll_keyboard
    cmp eax, 3
    je vibeloader_menu_change_video
    cmp eax, 2
    je vibeloader_menu_boot_now
    cmp eax, 1
    jne vibeloader_menu_maybe_draw
    mov byte [menu_dirty], 1

vibeloader_menu_maybe_draw:
    cmp byte [menu_dirty], 0
    jne .draw
    cmp byte [menu_first_render_done], 0
    je vibeloader_menu_loop
    cmp byte [menu_loop_stable_logged], 0
    jne vibeloader_menu_loop
    DEBUG_PMODE_CHAR 'I'
    mov byte [menu_loop_stable_logged], 1
    jmp vibeloader_menu_loop

.draw:
    DEBUG_PMODE_CHAR 'R'
    call menu_render
    DEBUG_PMODE_CHAR 'r'
    mov byte [menu_dirty], 0
    call boot_audio_maybe_play_once
    jmp vibeloader_menu_loop

vibeloader_menu_change_video:
    DEBUG_PMODE_CHAR 'v'
    jmp pmode_to_real_for_video_change

vibeloader_menu_boot_now:
    DEBUG_PMODE_CHAR 'g'
    call menu_apply_selection
    ret

vibeloader_menu_default_boot:
    DEBUG_PMODE_CHAR 'D'
    call boot_audio_maybe_play_once
    call menu_apply_selection
    ret

pm_text_debug_menu_fallback:
    push eax
    push ebx
    push ecx
    push edx
    push esi

    call pm_text_clear

    mov esi, text_no_vesa_title
    call pm_text_puts
    call pm_text_newline

    mov esi, text_last_label
    call pm_text_puts
    mov al, [debug_last_code]
    call pm_text_putc
    call pm_text_newline

    mov esi, text_flags_label
    call pm_text_puts
    mov eax, [BOOTINFO_ADDR + BOOTINFO_FLAGS]
    call pm_text_put_hex_dword
    call pm_text_newline

    mov esi, text_mode_label
    call pm_text_puts
    xor eax, eax
    mov ax, [BOOTINFO_ADDR + BOOTINFO_VESA_MODE]
    call pm_text_put_hex_word
    call pm_text_newline

    mov esi, text_bpp_label
    call pm_text_puts
    xor eax, eax
    mov al, [BOOTINFO_ADDR + BOOTINFO_VESA_BPP]
    call pm_text_put_hex_byte
    call pm_text_newline

    mov esi, text_width_label
    call pm_text_puts
    xor eax, eax
    mov ax, [BOOTINFO_ADDR + BOOTINFO_VESA_WIDTH]
    call pm_text_put_hex_word
    mov esi, text_height_label
    call pm_text_puts
    xor eax, eax
    mov ax, [BOOTINFO_ADDR + BOOTINFO_VESA_HEIGHT]
    call pm_text_put_hex_word
    call pm_text_newline

    mov esi, text_hint_enter
    call pm_text_puts
    call pm_text_newline

    pop esi
    pop edx
    pop ecx
    pop ebx
    pop eax
    ret

pm_text_clear:
    push eax
    push ecx
    push edi

    mov edi, 0xB8000
    mov ax, 0x0720
    mov ecx, 80 * 25
    cld
    rep stosw
    mov dword [pm_text_row], 0
    mov dword [pm_text_col], 0

    pop edi
    pop ecx
    pop eax
    ret

pm_text_puts:
    cld
.next:
    lodsb
    test al, al
    jz .done
    call pm_text_putc
    jmp .next
.done:
    ret

pm_text_putc:
    push eax
    push ebx
    push edx
    push edi

    cmp al, 0x0A
    jne .not_newline
    call pm_text_newline
    jmp .done

.not_newline:
    mov edx, [pm_text_row]
    imul edx, 80
    add edx, [pm_text_col]
    lea edi, [edx * 2 + 0xB8000]
    mov ah, 0x07
    mov [edi], ax

    inc dword [pm_text_col]
    cmp dword [pm_text_col], 80
    jb .done
    call pm_text_newline

.done:
    pop edi
    pop edx
    pop ebx
    pop eax
    ret

pm_text_newline:
    mov dword [pm_text_col], 0
    inc dword [pm_text_row]
    cmp dword [pm_text_row], 25
    jb .done
    mov dword [pm_text_row], 24
.done:
    ret

pm_text_put_hex_nibble:
    and al, 0x0F
    cmp al, 10
    jb .digit
    add al, 'A' - 10
    jmp pm_text_putc
.digit:
    add al, '0'
    jmp pm_text_putc

pm_text_put_hex_byte:
    push eax
    mov ah, al
    shr al, 4
    call pm_text_put_hex_nibble
    mov al, ah
    call pm_text_put_hex_nibble
    pop eax
    ret

pm_text_put_hex_word:
    push eax
    mov al, ah
    call pm_text_put_hex_byte
    pop eax
    call pm_text_put_hex_byte
    ret

pm_text_put_hex_dword:
    push eax
    shr eax, 16
    call pm_text_put_hex_word
    pop eax
    call pm_text_put_hex_word
    ret

pm_text_halt:
    cli
.halt:
    hlt
    jmp .halt

menu_compute_layout:
    xor eax, eax
    mov ax, [BOOTINFO_ADDR + BOOTINFO_VESA_WIDTH]
    cmp eax, 640
    jbe .width_done
    sub eax, 640
    shr eax, 1
    jmp .store_width
.width_done:
    xor eax, eax
.store_width:
    mov [menu_base_x], eax

    xor eax, eax
    mov ax, [BOOTINFO_ADDR + BOOTINFO_VESA_HEIGHT]
    cmp eax, 480
    jbe .height_done
    sub eax, 480
    shr eax, 1
    jmp .store_height
.height_done:
    xor eax, eax
.store_height:
    mov [menu_base_y], eax

    xor eax, eax
    mov ax, [BOOTINFO_ADDR + BOOTINFO_VESA_WIDTH]
    xor edx, edx
    mov ecx, BACKGROUND_SOURCE_WIDTH
    div ecx
    mov ebx, eax

    xor eax, eax
    mov ax, [BOOTINFO_ADDR + BOOTINFO_VESA_HEIGHT]
    xor edx, edx
    mov ecx, BACKGROUND_SOURCE_HEIGHT
    div ecx
    cmp eax, ebx
    jae .background_scale_ready
    mov ebx, eax

.background_scale_ready:
    test ebx, ebx
    jnz .store_background_scale
    mov ebx, 1

.store_background_scale:
    mov [background_scale], ebx

    xor eax, eax
    mov ax, [BOOTINFO_ADDR + BOOTINFO_VESA_WIDTH]
    mov ecx, BACKGROUND_SOURCE_WIDTH
    imul ecx, ebx
    sub eax, ecx
    shr eax, 1
    mov [background_base_x], eax

    xor eax, eax
    mov ax, [BOOTINFO_ADDR + BOOTINFO_VESA_HEIGHT]
    mov ecx, BACKGROUND_SOURCE_HEIGHT
    imul ecx, ebx
    sub eax, ecx
    shr eax, 1
    mov [background_base_y], eax
    ret

menu_restart_timer:
    call pit_read_counter
    mov [menu_prev_pit], ax
    mov dword [menu_elapsed_counts], 0
    mov dword [menu_seconds_left], MENU_TIMEOUT_SECONDS
    mov byte [menu_timeout_paused], 0
    ret

pit_read_counter:
    xor al, al
    out 0x43, al
    in al, 0x40
    mov ah, al
    in al, 0x40
    xchg al, ah
    ret

boot_audio_maybe_play_once:
    pushad

    cmp byte [boot_audio_available], 1
    jne .done
    cmp byte [boot_audio_played], 0
    jne .done
    cmp dword [boot_audio_size], 0
    je .done

    mov byte [boot_audio_played], 1
    DEBUG_PMODE_CHAR 'S'
    call boot_audio_speaker_begin
    call pit_read_counter
    mov [boot_audio_prev_pit], ax
    mov edi, BOOT_AUDIO_LOAD_ADDR
    mov ecx, [boot_audio_size]

.sample_loop:
    test ecx, ecx
    jz .finish

.wait_sample:
    call pit_read_counter
    movzx ebx, word [boot_audio_prev_pit]
    movzx edx, ax
    sub ebx, edx
    and ebx, 0xFFFF
    cmp ebx, BOOT_AUDIO_SAMPLE_COUNTS
    jb .wait_sample
    mov [boot_audio_prev_pit], ax

    movzx eax, byte [edi]
    inc edi
    dec ecx
    call boot_audio_output_sample
    jmp .sample_loop

.finish:
    call boot_audio_speaker_end
    DEBUG_PMODE_CHAR 's'
    cmp byte [menu_initialized], 0
    je .done
    call menu_restart_timer
    mov byte [menu_dirty], 1

.done:
    popad
    ret

boot_audio_speaker_begin:
    push eax
    in al, 0x61
    mov [boot_audio_saved_port61], al
    or al, 0x03
    out 0x61, al
    pop eax
    ret

boot_audio_speaker_end:
    push eax
    mov al, [boot_audio_saved_port61]
    out 0x61, al
    pop eax
    ret

boot_audio_output_sample:
    push ebx

    imul eax, eax, BOOT_AUDIO_PWM_COUNTS
    shr eax, 8
    add eax, 1
    mov bx, ax

    mov al, 0xB0
    out 0x43, al
    mov al, bl
    out 0x42, al
    mov al, bh
    out 0x42, al

    pop ebx
    ret

menu_flush_keyboard_buffer:
.poll:
    in al, 0x64
    test al, 1
    jz .done
    in al, 0x60
    jmp .poll
.done:
    mov byte [menu_extended], 0
    ret

menu_poll_keyboard:
    xor edx, edx
.poll:
    in al, 0x64
    test al, 1
    jz .done

    in al, 0x60
    cmp al, 0xE0
    jne .not_extended
    mov byte [menu_extended], 1
    jmp .poll

.not_extended:
    mov bl, [menu_extended]
    mov byte [menu_extended], 0
    test al, 0x80
    jnz .poll
    mov [menu_last_scancode], al
    mov [menu_last_extended_flag], bl
    call bootdebug_store_key32
    mov byte [menu_dirty], 1

    cmp byte [menu_timeout_paused], 0
    jne .classify
    mov byte [menu_timeout_paused], 1
    mov edx, 1

.classify:
    cmp bl, 0
    jne .handle_extended

	    cmp al, 0x11
	    je .move_up
	    cmp al, 0x2F
	    je .toggle_video_driver
	    cmp al, 0x17
	    je .toggle_i915_experiment
	    cmp al, 0x32
	    je .toggle_smp_experiment
	    cmp al, 0x1F
	    je .move_down
    cmp al, 0x1C
    je .confirm
    jmp .poll

.handle_extended:
    cmp al, 0x48
    je .move_up
    cmp al, 0x50
    je .move_down
    cmp al, 0x4B
    je .video_prev
    cmp al, 0x4D
    je .video_next
    cmp al, 0x1C
    je .confirm
    jmp .poll

.move_up:
    mov al, 'U'
    mov [menu_last_action], al
    call bootdebug_store_action32
    mov eax, [menu_selection]
    test eax, eax
    jz .wrap_up
    dec eax
    jmp .store_selection
.wrap_up:
    mov eax, MENU_ENTRY_COUNT - 1
.store_selection:
    mov [menu_selection], eax
    mov eax, 1
    ret

.move_down:
    mov al, 'D'
    mov [menu_last_action], al
    call bootdebug_store_action32
    mov eax, [menu_selection]
    inc eax
    cmp eax, MENU_ENTRY_COUNT
    jb .store_down
    xor eax, eax
.store_down:
    mov [menu_selection], eax
    mov eax, 1
    ret

.video_prev:
    mov al, 'L'
    mov [menu_last_action], al
    call bootdebug_store_action32
    DEBUG_PMODE_CHAR '<'
    call menu_select_previous_video_mode
    cmp eax, 0
    je .poll
    mov eax, 3
    ret

.video_next:
    mov al, 'R'
    mov [menu_last_action], al
    call bootdebug_store_action32
    DEBUG_PMODE_CHAR '>'
    call menu_select_next_video_mode
    cmp eax, 0
    je .poll
    mov eax, 3
    ret

	.toggle_i915_experiment:
	    mov al, 'I'
	    mov [menu_last_action], al
	    call bootdebug_store_action32
	    xor eax, eax
	    mov eax, [BOOTINFO_ADDR + BOOTINFO_FLAGS]
	    xor eax, BOOTINFO_FLAG_EXPERIMENTAL_I915_COMMIT
	    mov [BOOTINFO_ADDR + BOOTINFO_FLAGS], eax
	    mov eax, 1
	    ret

	.toggle_smp_experiment:
	    mov al, 'M'
	    mov [menu_last_action], al
	    call bootdebug_store_action32
	    xor eax, eax
	    mov eax, [BOOTINFO_ADDR + BOOTINFO_FLAGS]
	    xor eax, BOOTINFO_FLAG_EXPERIMENTAL_SMP
	    mov [BOOTINFO_ADDR + BOOTINFO_FLAGS], eax
	    mov eax, 1
	    ret

	.toggle_video_driver:
	    mov al, 'V'
	    mov [menu_last_action], al
	    call bootdebug_store_action32
	    xor eax, eax
	    mov eax, [BOOTINFO_ADDR + BOOTINFO_FLAGS]
	    xor eax, BOOTINFO_FLAG_FORCE_LEGACY_VIDEO
	    mov [BOOTINFO_ADDR + BOOTINFO_FLAGS], eax
	    mov eax, 1
	    ret

.confirm:
    mov al, 'B'
    mov [menu_last_action], al
    call bootdebug_store_action32
    DEBUG_PMODE_CHAR '!'
    mov eax, 2
    ret

.done:
    mov eax, edx
    ret

menu_select_previous_video_mode:
    xor ecx, ecx
    mov cl, [BOOTINFO_ADDR + BOOTINFO_VIDEO_MODE_COUNT]
    cmp ecx, 1
    jbe .no_change

    movzx eax, byte [BOOTINFO_ADDR + BOOTINFO_VIDEO_SELECTED_INDEX]
    cmp al, BOOTINFO_VIDEO_INDEX_NONE
    jne .have_current
    xor eax, eax
    jmp .store

.have_current:
    test eax, eax
    jnz .decrement
    mov eax, ecx
    dec eax
    jmp .store

.decrement:
    dec eax

.store:
    mov [BOOTINFO_ADDR + BOOTINFO_VIDEO_SELECTED_INDEX], al
    mov eax, 1
    ret

.no_change:
    xor eax, eax
    ret

menu_select_next_video_mode:
    xor ecx, ecx
    mov cl, [BOOTINFO_ADDR + BOOTINFO_VIDEO_MODE_COUNT]
    cmp ecx, 1
    jbe .no_change

    movzx eax, byte [BOOTINFO_ADDR + BOOTINFO_VIDEO_SELECTED_INDEX]
    cmp al, BOOTINFO_VIDEO_INDEX_NONE
    jne .have_current
    xor eax, eax
    jmp .store

.have_current:
    inc eax
    cmp eax, ecx
    jb .store
    xor eax, eax

.store:
    mov [BOOTINFO_ADDR + BOOTINFO_VIDEO_SELECTED_INDEX], al
    mov eax, 1
    ret

.no_change:
    xor eax, eax
    ret

menu_apply_selection:
    mov eax, [BOOTINFO_ADDR + BOOTINFO_FLAGS]
    and eax, ~BOOTINFO_FLAG_BOOT_MODE_MASK
    and eax, ~BOOTINFO_FLAG_FORCE_LEGACY_VIDEO
    mov edx, [menu_selection]
    cmp edx, MENU_ENTRY_COUNT
    jae .store
    mov edx, [menu_entry_boot_flags + edx * 4]
    or eax, edx
.store:
    mov [BOOTINFO_ADDR + BOOTINFO_FLAGS], eax
    ret

menu_render:
    cmp byte [menu_first_render_done], 0
    je .draw_full_frame
    cmp byte [menu_force_full_redraw], 0
    jne .draw_full_frame
    jmp .draw_panel_only

.draw_full_frame:
    DEBUG_PMODE_CHAR 'm'
    call draw_background
    mov byte [menu_force_full_redraw], 0
    DEBUG_PMODE_CHAR '3'

.draw_panel_only:

    mov eax, 148
    add eax, [menu_base_x]
    mov ebx, 104
    add ebx, [menu_base_y]
    mov ecx, 344
    mov esi, 308
    mov dl, 1
    call draw_rect
    DEBUG_PMODE_CHAR '4'

    mov eax, 152
    add eax, [menu_base_x]
    mov ebx, 108
    add ebx, [menu_base_y]
    mov ecx, 336
    mov esi, 300
    mov dl, 8
    call draw_rect
    DEBUG_PMODE_CHAR '5'

    mov eax, 152
    add eax, [menu_base_x]
    mov ebx, 108
    add ebx, [menu_base_y]
    mov ecx, 336
    mov esi, 24
    mov dl, 3
    call draw_rect
    DEBUG_PMODE_CHAR '6'

    mov esi, vibeloader_title
    mov eax, 230
    add eax, [menu_base_x]
    mov ebx, 121
    add ebx, [menu_base_y]
    mov ecx, 3
    mov edx, 15
    call draw_text
    DEBUG_PMODE_CHAR '7'

    cmp byte [menu_first_render_done], 0
    jne .draw_entry_one
    DEBUG_PMODE_CHAR 'n'

.draw_entry_one:
    cmp byte [menu_first_render_done], 0
    jne .draw_entry_one_call
    DEBUG_PMODE_CHAR 'o'
.draw_entry_one_call:
    mov eax, 0
    mov ebx, 180
    mov esi, [menu_entry_labels + (0 * 4)]
    call draw_menu_entry

    cmp byte [menu_first_render_done], 0
    jne .draw_entry_two
    DEBUG_PMODE_CHAR 'p'

.draw_entry_two:
    cmp byte [menu_first_render_done], 0
    jne .draw_entry_two_call
    DEBUG_PMODE_CHAR 'q'
.draw_entry_two_call:
    mov eax, 1
    mov ebx, 214
    mov esi, [menu_entry_labels + (1 * 4)]
    call draw_menu_entry

    cmp byte [menu_first_render_done], 0
    jne .draw_entry_three
    DEBUG_PMODE_CHAR 'r'

.draw_entry_three:
    cmp byte [menu_first_render_done], 0
    jne .draw_entry_three_call
    DEBUG_PMODE_CHAR 's'
.draw_entry_three_call:
    mov eax, 2
    mov ebx, 248
    mov esi, [menu_entry_labels + (2 * 4)]
    call draw_menu_entry

    cmp byte [menu_first_render_done], 0
    jne .draw_entry_four
    DEBUG_PMODE_CHAR 't'

.draw_entry_four:
    mov eax, 3
    mov ebx, 282
    mov esi, [menu_entry_labels + (3 * 4)]
    call draw_menu_entry

.countdown_prepare:
    cmp byte [menu_timeout_paused], 0
    jne .draw_paused

    mov eax, [menu_seconds_left]
    add al, '0'
    mov [countdown_text + 13], al
    mov esi, countdown_text
    jmp .draw_countdown

.draw_paused:
    mov esi, countdown_paused_text

.draw_countdown:
    mov eax, 212
    add eax, [menu_base_x]
    mov ebx, 330
    add ebx, [menu_base_y]
    mov ecx, 2
    mov edx, 15
    call draw_text

    call menu_build_video_text
    mov esi, menu_video_text
    mov eax, 208
    add eax, [menu_base_x]
    mov ebx, 354
    add ebx, [menu_base_y]
    mov ecx, 2
    mov edx, 15
    call draw_text

	    call menu_build_i915_text
    mov esi, menu_i915_text
    mov eax, 208
    add eax, [menu_base_x]
    mov ebx, 366
    add ebx, [menu_base_y]
	    mov ecx, 1
	    mov edx, 15
		    call draw_text

	    call menu_build_smp_text
	    mov esi, menu_smp_text
	    mov eax, 296
	    add eax, [menu_base_x]
	    mov ebx, 366
	    add ebx, [menu_base_y]
	    mov ecx, 1
	    mov edx, 15
	    call draw_text

		    call menu_build_driver_text
    mov esi, menu_driver_text
    mov eax, 208
    add eax, [menu_base_x]
    mov ebx, 378
    add ebx, [menu_base_y]
    mov ecx, 1
    mov edx, 15
	    call draw_text
	
    mov esi, menu_hint_top
    mov eax, 192
    add eax, [menu_base_x]
    mov ebx, 390
    add ebx, [menu_base_y]
    mov ecx, 1
    mov edx, 15
	    call draw_text
	
    mov esi, menu_hint_bottom
    mov eax, 232
    add eax, [menu_base_x]
    mov ebx, 400
    add ebx, [menu_base_y]
    mov ecx, 1
    mov edx, 15
	    call draw_text
	
	    call menu_build_debug_text
    mov esi, menu_debug_text
    mov eax, 180
    add eax, [menu_base_x]
    mov ebx, 426
    add ebx, [menu_base_y]
    mov ecx, 1
    mov edx, 15
	    call draw_text

    cmp byte [menu_first_render_done], 0
    jne .done
    DEBUG_PMODE_CHAR 'u'
    mov byte [menu_first_render_done], 1

.done:
    ret

draw_menu_entry:
    push esi
    add ebx, [menu_base_y]
    cmp [menu_selection], eax
    jne .normal

    push ebx
    mov eax, 198
    add eax, [menu_base_x]
    sub ebx, 2
    mov ecx, 244
    mov esi, 34
    mov dl, 15
    call draw_rect
    pop ebx

    mov eax, 200
    add eax, [menu_base_x]
    mov ecx, 240
    mov esi, 30
    mov dl, 14
    push ebx
    call draw_rect
    pop ebx
    mov edx, 1
    jmp .label

.normal:
    push ebx
    mov eax, 198
    add eax, [menu_base_x]
    sub ebx, 2
    mov ecx, 244
    mov esi, 34
    mov dl, 15
    call draw_rect
    pop ebx

    mov eax, 200
    add eax, [menu_base_x]
    mov ecx, 240
    mov esi, 30
    mov dl, 7
    push ebx
    call draw_rect
    pop ebx
    mov edx, 1

.label:
    pop esi
    mov eax, 220
    add eax, [menu_base_x]
    add ebx, 8
    mov ecx, 2
    call draw_text
    ret

draw_background:
    cmp byte [background_available], 0
    jne .image

    xor eax, eax
    xor ebx, ebx
    movzx ecx, word [BOOTINFO_ADDR + BOOTINFO_VESA_WIDTH]
    movzx esi, word [BOOTINFO_ADDR + BOOTINFO_VESA_HEIGHT]
    mov dl, 3
    call draw_rect
    ret

.image:
    xor eax, eax
    xor ebx, ebx
    movzx ecx, word [BOOTINFO_ADDR + BOOTINFO_VESA_WIDTH]
    movzx esi, word [BOOTINFO_ADDR + BOOTINFO_VESA_HEIGHT]
    mov dl, 3
    call draw_rect

    xor ebp, ebp
    mov esi, BACKGROUND_DRAW_ADDR
.row_loop:
    xor edi, edi
.col_loop:
    mov dl, [esi]
    mov eax, edi
    imul eax, [background_scale]
    add eax, [background_base_x]
    mov ebx, ebp
    imul ebx, [background_scale]
    add ebx, [background_base_y]
    mov ecx, [background_scale]
    push esi
    mov esi, [background_scale]
    call draw_rect
    pop esi
    inc esi
    inc edi
    cmp edi, BACKGROUND_SOURCE_WIDTH
    jb .col_loop
    inc ebp
    cmp ebp, BACKGROUND_SOURCE_HEIGHT
    jb .row_loop
    ret

set_desktop_palette:
    mov dx, 0x03C8
    xor al, al
    out dx, al
    mov dx, 0x03C9

    xor ecx, ecx
.loop:
    cmp ecx, 256
    jae .done
    cmp ecx, 16
    jb .ega
    mov eax, ecx
    shr eax, 5
    and eax, 0x07
    imul eax, 63
    mov ebx, 7
    xor edx, edx
    div ebx
    mov dx, 0x03C9
    out dx, al

    mov eax, ecx
    shr eax, 2
    and eax, 0x07
    imul eax, 63
    mov ebx, 7
    xor edx, edx
    div ebx
    mov dx, 0x03C9
    out dx, al

    mov eax, ecx
    and eax, 0x03
    imul eax, 63
    mov ebx, 3
    xor edx, edx
    div ebx
    mov dx, 0x03C9
    out dx, al
    inc ecx
    jmp .loop

.ega:
    movzx eax, byte [ega_palette_6bit + ecx * 3 + 0]
    out dx, al
    movzx eax, byte [ega_palette_6bit + ecx * 3 + 1]
    out dx, al
    movzx eax, byte [ega_palette_6bit + ecx * 3 + 2]
    out dx, al
    inc ecx
    jmp .loop
.done:
    ret

draw_rect:
    push edi
    push ebp
    push edx
    cld
    test ecx, ecx
    jz .done
    test esi, esi
    jz .done

    movzx ebp, word [BOOTINFO_ADDR + BOOTINFO_VESA_WIDTH]
    cmp eax, ebp
    jae .done
    sub ebp, eax
    cmp ecx, ebp
    jbe .clip_height
    mov ecx, ebp

.clip_height:
    movzx ebp, word [BOOTINFO_ADDR + BOOTINFO_VESA_HEIGHT]
    cmp ebx, ebp
    jae .done
    sub ebp, ebx
    cmp esi, ebp
    jbe .prepare
    mov esi, ebp

.prepare:
    mov edi, [BOOTINFO_ADDR + BOOTINFO_VESA_FB]
    movzx ebp, word [BOOTINFO_ADDR + BOOTINFO_VESA_PITCH]
    imul ebx, ebp
    add edi, ebx
    add edi, eax
.row:
    push ecx
    mov al, dl
    rep stosb
    pop ecx
    mov eax, ebp
    sub eax, ecx
    add edi, eax
    dec esi
    jnz .row

.done:
    pop edx
    pop ebp
    pop edi
    ret

graphics_bootinfo_sane32:
    push eax
    push edx

    movzx eax, byte [BOOTINFO_ADDR + BOOTINFO_VESA_BPP]
    cmp eax, 8
    jne .fail

    mov eax, [BOOTINFO_ADDR + BOOTINFO_VESA_FB]
    test eax, eax
    jz .fail
    cmp eax, GRAPHICS_MIN_FB_ADDR
    jb .fail

    movzx eax, word [BOOTINFO_ADDR + BOOTINFO_VESA_WIDTH]
    cmp eax, 640
    jb .fail
    cmp eax, GRAPHICS_MAX_WIDTH
    ja .fail
    mov edx, eax

    movzx eax, word [BOOTINFO_ADDR + BOOTINFO_VESA_HEIGHT]
    cmp eax, 480
    jb .fail
    cmp eax, GRAPHICS_MAX_HEIGHT
    ja .fail

    movzx eax, word [BOOTINFO_ADDR + BOOTINFO_VESA_PITCH]
    cmp eax, edx
    jb .fail
    test eax, eax
    jz .fail

    clc
    jmp .done

.fail:
    stc

.done:
    pop edx
    pop eax
    ret

draw_text:
    push ebp
    push edi
    cld
    mov edi, eax
    mov ebp, ebx
.next_char:
    lodsb
    test al, al
    jz .done
    cmp al, ' '
    je .advance
    push esi
    push ecx
    push edx
    push edi
    push ebp
    mov ebx, edi
    mov edi, ebp
    call draw_glyph
    pop ebp
    pop edi
    pop edx
    pop ecx
    pop esi
.advance:
    mov eax, ecx
    imul eax, 6
    add edi, eax
    jmp .next_char
.done:
    pop edi
    pop ebp
    ret

menu_build_video_text:
    push eax
    push ebx
    push ecx
    push edx
    push esi
    push edi

    mov edi, menu_video_text
    mov esi, menu_video_prefix
    call menu_append_cstring

    movzx ebx, byte [BOOTINFO_ADDR + BOOTINFO_VIDEO_SELECTED_INDEX]
    cmp bl, BOOTINFO_VIDEO_INDEX_NONE
    jne .have_index
    movzx ebx, byte [BOOTINFO_ADDR + BOOTINFO_VIDEO_ACTIVE_INDEX]

.have_index:
    cmp bl, BOOTINFO_VIDEO_INDEX_NONE
    jne .have_mode
    mov esi, menu_video_unknown
    call menu_append_cstring
    jmp .finish

.have_mode:
    shl ebx, 3
    movzx eax, word [BOOTINFO_ADDR + BOOTINFO_VIDEO_MODES + ebx + BOOTINFO_VIDEO_MODE_FIELD_WIDTH]
    call menu_append_uint
    mov al, 'X'
    stosb
    movzx eax, word [BOOTINFO_ADDR + BOOTINFO_VIDEO_MODES + ebx + BOOTINFO_VIDEO_MODE_FIELD_HEIGHT]
    call menu_append_uint

.finish:
    mov al, 0
    stosb

    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    pop eax
    ret

menu_build_i915_text:
    push eax
    push ebx
    push ecx
    push edx
    push esi
    push edi

    mov edi, menu_i915_text
    mov esi, menu_i915_label
    call menu_append_cstring

    mov eax, [BOOTINFO_ADDR + BOOTINFO_FLAGS]
    test eax, BOOTINFO_FLAG_EXPERIMENTAL_I915_COMMIT
    jz .disabled
    mov esi, menu_i915_on
    call menu_append_cstring
    jmp .finish

.disabled:
    mov esi, menu_i915_off
    call menu_append_cstring

.finish:
    mov al, 0
    stosb

    pop edi
    pop esi
    pop edx
	    pop ecx
	    pop ebx
	    pop eax
	    ret

menu_build_smp_text:
    push eax
    push ebx
    push ecx
    push edx
    push esi
    push edi

    mov edi, menu_smp_text
    mov esi, menu_smp_label
    call menu_append_cstring

    mov eax, [BOOTINFO_ADDR + BOOTINFO_FLAGS]
    test eax, BOOTINFO_FLAG_EXPERIMENTAL_SMP
    jz .disabled
    mov esi, menu_smp_on
    call menu_append_cstring
    jmp .finish

.disabled:
    mov esi, menu_smp_off
    call menu_append_cstring

.finish:
    mov al, 0
    stosb

    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    pop eax
    ret

		menu_build_driver_text:
	    push eax
	    push ebx
	    push ecx
	    push edx
	    push esi
	    push edi

	    mov edi, menu_driver_text
	    mov esi, menu_driver_label
	    call menu_append_cstring

	    mov eax, [BOOTINFO_ADDR + BOOTINFO_FLAGS]
	    test eax, BOOTINFO_FLAG_FORCE_LEGACY_VIDEO
	    jz .auto
	    mov esi, menu_driver_legacy
	    call menu_append_cstring
	    jmp .finish

	.auto:
	    mov esi, menu_driver_auto
	    call menu_append_cstring

	.finish:
	    mov al, 0
	    stosb

	    pop edi
	    pop esi
	    pop edx
	    pop ecx
	    pop ebx
	    pop eax
	    ret

menu_build_debug_text:
    push eax
    push ebx
    push ecx
    push edx
    push esi
    push edi

    mov edi, menu_debug_text
    mov esi, menu_debug_prefix
    call menu_append_cstring

    mov al, [debug_last_code]
    call menu_append_debug_char

    mov al, ' '
    stosb
    mov esi, menu_key_prefix
    call menu_append_cstring

    xor eax, eax
    mov al, [menu_last_scancode]
    call menu_append_uint

    mov al, ' '
    stosb
    mov esi, menu_ext_prefix
    call menu_append_cstring

    xor eax, eax
    mov al, [menu_last_extended_flag]
    call menu_append_uint

    mov al, ' '
    stosb
    mov esi, menu_act_prefix
    call menu_append_cstring

    mov al, [menu_last_action]
    call menu_append_debug_char

    mov al, ' '
    stosb
    mov esi, menu_i915_prefix
    call menu_append_cstring

	    mov eax, [BOOTINFO_ADDR + BOOTINFO_FLAGS]
	    and eax, BOOTINFO_FLAG_EXPERIMENTAL_I915_COMMIT
	    shr eax, 20
	    call menu_append_uint

	    mov al, ' '
	    stosb
	    mov esi, menu_smp_prefix
	    call menu_append_cstring

	    mov eax, [BOOTINFO_ADDR + BOOTINFO_FLAGS]
	    and eax, BOOTINFO_FLAG_EXPERIMENTAL_SMP
	    shr eax, 22
	    call menu_append_uint

	    mov al, 0
	    stosb

    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    pop eax
    ret

menu_append_debug_char:
    cmp al, 'a'
    jb .store
    cmp al, 'z'
    ja .store
    sub al, 32

.store:
    stosb
    ret

menu_append_cstring:
    cld
.copy:
    lodsb
    test al, al
    jz .done
    stosb
    jmp .copy
.done:
    ret

menu_append_uint:
    push eax
    push ebx
    push edx
    push esi

    lea esi, [menu_number_buffer + 5]
    mov byte [esi], 0
    mov ebx, 10
    mov eax, [esp + 12]
    test eax, eax
    jnz .convert
    dec esi
    mov byte [esi], '0'
    jmp .append

.convert:
    xor edx, edx
.loop:
    div ebx
    add dl, '0'
    dec esi
    mov [esi], dl
    test eax, eax
    jnz .next
    jmp .append

.next:
    xor edx, edx
    jmp .loop

.append:
    call menu_append_cstring
    pop esi
    pop edx
    pop ebx
    pop eax
    ret

draw_glyph:
    push ebp
    push edi
    push esi
    push eax
    push ebx
    push ecx
    push edx

    call glyph_ptr_from_char
    test esi, esi
    jz .done
    test ecx, ecx
    jz .done

    mov [glyph_ptr_tmp], esi
    mov [glyph_draw_x], ebx
    mov [glyph_draw_y], edi
    mov [glyph_draw_scale], ecx
    mov [glyph_draw_color], dl
    mov eax, [BOOTINFO_ADDR + BOOTINFO_VESA_FB]
    mov [glyph_fb_ptr], eax
    movzx eax, word [BOOTINFO_ADDR + BOOTINFO_VESA_PITCH]
    mov [glyph_fb_pitch], eax
    movzx eax, word [BOOTINFO_ADDR + BOOTINFO_VESA_WIDTH]
    mov [glyph_fb_width], eax
    movzx eax, word [BOOTINFO_ADDR + BOOTINFO_VESA_HEIGHT]
    mov [glyph_fb_height], eax

    xor ebp, ebp
.row_loop:
    cmp ebp, 7
    jae .done
    mov esi, [glyph_ptr_tmp]
    mov al, [esi + ebp]
    mov [glyph_row_bits], al
    test al, al
    jz .next_row

    xor esi, esi
.vscale_loop:
    mov eax, ebp
    imul eax, [glyph_draw_scale]
    add eax, [glyph_draw_y]
    add eax, esi
    cmp eax, [glyph_fb_height]
    jae .next_vscale

    mov edi, [glyph_fb_pitch]
    imul eax, edi
    add eax, [glyph_fb_ptr]
    mov [glyph_row_ptr], eax

    xor edx, edx
.col_loop:
    cmp edx, 5
    jae .next_vscale
    mov al, [glyph_row_bits]
    mov bl, [glyph_masks + edx]
    test al, bl
    jz .advance_col

    mov ecx, edx
.run_scan:
    inc edx
    cmp edx, 5
    jae .have_run
    mov al, [glyph_row_bits]
    mov bl, [glyph_masks + edx]
    test al, bl
    jnz .run_scan

.have_run:
    mov eax, ecx
    imul eax, [glyph_draw_scale]
    add eax, [glyph_draw_x]
    cmp eax, [glyph_fb_width]
    jae .col_loop

    mov ebx, edx
    sub ebx, ecx
    imul ebx, [glyph_draw_scale]
    mov ecx, [glyph_fb_width]
    sub ecx, eax
    cmp ebx, ecx
    jbe .width_ok
    mov ebx, ecx

.width_ok:
    test ebx, ebx
    jz .col_loop
    mov edi, [glyph_row_ptr]
    add edi, eax
    mov ecx, ebx
    mov al, [glyph_draw_color]
    rep stosb
    jmp .col_loop

.advance_col:
    inc edx
    jmp .col_loop

.next_vscale:
    inc esi
    cmp esi, [glyph_draw_scale]
    jb .vscale_loop

.next_row:
    inc ebp
    jmp .row_loop

.done:
    pop edx
    pop ecx
    pop ebx
    pop eax
    pop esi
    pop edi
    pop ebp
    ret

glyph_ptr_from_char:
    cmp al, 'A'
    jb .digit_or_space
    cmp al, 'Z'
    ja .digit_or_space
    movzx eax, al
    sub eax, 'A'
    imul eax, 7
    lea esi, [font_glyphs + eax]
    ret

.digit_or_space:
    cmp al, '0'
    jb .space
    cmp al, '9'
    ja .space
    movzx eax, al
    sub eax, '0'
    add eax, 26
    imul eax, 7
    lea esi, [font_glyphs + eax]
    ret

.space:
    mov esi, font_space
    ret

pmode_to_real_for_video_change:
    DEBUG_PMODE_CHAR 'v'
    cli
    jmp CODE16_SEG:pmode16_to_real

pmode_to_real_for_kernel_boot:
    DEBUG_PMODE_CHAR 'g'
    cli
    jmp CODE16_SEG:pmode16_to_real_for_kernel_boot

pmode_to_real_for_debug_halt:
    DEBUG_PMODE_CHAR 'd'
    cli
    jmp CODE16_SEG:pmode16_to_real_for_debug_halt

BITS 16
pmode16_to_real:
    DEBUG_CHAR 'u'
    mov ax, DATA16_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov sp, REALMODE_STACK_TOP
    mov eax, cr0
    and eax, 0xFFFFFFFE
    mov cr0, eax
    jmp 0x0000:realmode_apply_video_change

pmode16_to_real_for_kernel_boot:
    DEBUG_CHAR 'h'
    mov ax, DATA16_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov sp, REALMODE_STACK_TOP
    mov eax, cr0
    and eax, 0xFFFFFFFE
    mov cr0, eax
    jmp 0x0000:realmode_boot_selected_kernel

pmode16_to_real_for_debug_halt:
    DEBUG_CHAR 'e'
    mov ax, DATA16_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov sp, REALMODE_STACK_TOP
    mov eax, cr0
    and eax, 0xFFFFFFFE
    mov cr0, eax
    jmp 0x0000:realmode_debug_menu_fallback

realmode_apply_video_change:
    DEBUG_CHAR 'w'
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, REALMODE_STACK_TOP
    lidt [realmode_idtr]

    call apply_selected_video_mode
.resume:
    DEBUG_CHAR 'z'
    call enable_a20
    xor ax, ax
    mov ds, ax
    mov es, ax
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp CODE_SEG:pmode_video_resume

realmode_boot_selected_kernel:
    DEBUG_BOOT_CHAR 'K'
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, REALMODE_STACK_TOP
    cld
    lidt [realmode_idtr]

    call apply_selected_video_mode

    call load_kernel_file
    jc disk_error

    DEBUG_BOOT_CHAR 'k'
    call enable_a20
    xor ax, ax
    mov ds, ax
    mov es, ax
    call a20_is_enabled_any
    cmp ax, 1
    jne a20_error

.a20_ready:
    DEBUG_BOOT_CHAR 'P'
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp CODE_SEG:pmode_kernel_resume

apply_selected_video_mode:
    mov al, [BOOTINFO_ADDR + BOOTINFO_VIDEO_SELECTED_INDEX]
    cmp al, BOOTINFO_VIDEO_INDEX_NONE
    je .done
    cmp al, [BOOTINFO_ADDR + BOOTINFO_VIDEO_ACTIVE_INDEX]
    je .done

    xor bx, bx
    mov bl, al
    shl bx, 3
    mov dx, [BOOTINFO_ADDR + BOOTINFO_VIDEO_MODES + bx + BOOTINFO_VIDEO_MODE_FIELD_MODE]
    DEBUG_CHAR 'x'
    call vesa_set_mode_and_store_bootinfo
    xor ax, ax
    mov ds, ax
    mov es, ax
    jc .restore_selection
    DEBUG_CHAR 'y'
    call populate_legacy_vesa_info
    ret

.restore_selection:
    mov al, [BOOTINFO_ADDR + BOOTINFO_VIDEO_ACTIVE_INDEX]
    mov [BOOTINFO_ADDR + BOOTINFO_VIDEO_SELECTED_INDEX], al
.done:
    ret

realmode_debug_menu_fallback:
    DEBUG_BOOT_CHAR 'T'
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, REALMODE_STACK_TOP
    cld
    lidt [realmode_idtr]

    mov ax, 0x0003
    int 0x10

    mov si, text_no_vesa_title
    call print_string
    call print_newline

    mov si, last_label
    call print_string
    mov al, [debug_last_code]
    call print_char
    call print_newline

    mov si, trace_label
    call print_string
    mov si, debug_trace_buf
    call print_string
    call print_newline

    cmp byte [pm_exception_vector], 0xFF
    je .print_flags
    mov si, text_exc_label
    call print_string
    mov al, [pm_exception_vector]
    call print_hex_byte
    call print_newline

.print_flags:

    mov si, text_flags_label
    call print_string
    mov eax, [BOOTINFO_ADDR + BOOTINFO_FLAGS]
    call print_hex_dword
    call print_newline

    mov si, text_mode_label
    call print_string
    mov ax, [BOOTINFO_ADDR + BOOTINFO_VESA_MODE]
    call print_hex_word
    call print_newline

    mov si, text_bpp_label
    call print_string
    mov al, [BOOTINFO_ADDR + BOOTINFO_VESA_BPP]
    call print_hex_byte
    call print_newline

    mov si, text_width_label
    call print_string
    mov ax, [BOOTINFO_ADDR + BOOTINFO_VESA_WIDTH]
    call print_hex_word
    mov si, text_height_label
    call print_string
    mov ax, [BOOTINFO_ADDR + BOOTINFO_VESA_HEIGHT]
    call print_hex_word
    call print_newline

    mov si, text_hint_enter
    call print_string
.halt:
    hlt
    jmp .halt

BITS 32
pmode_video_resume:
    DEBUG_PMODE_CHAR 'W'
    cli
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x70000
    cld
    call pmode_install_idt
    mov byte [menu_extended], 0
    call set_desktop_palette
    call menu_compute_layout
    mov byte [menu_force_full_redraw], 1
    mov byte [menu_dirty], 1
    call vibeloader_menu
    jmp pmode_to_real_for_kernel_boot

pmode_kernel_resume:
    DEBUG_PMODE_CHAR 'G'
    cli
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ; Kernel boot stack. 0x70000 is NOT safe anymore: the loaded kernel
    ; image (.text/.rodata/.data) grows past it and the boot stack trashed
    ; kernel .rodata (observed: the arena PDE snapshot spilled over
    ; g_backend_fast_lfb_ops -> ops->activate garbage -> jump into the app
    ; arena). 0x800000 stays inside boot/core reserved [0x100000,0x807000),
    ; above kernel .bss end, below the heap start. linker/kernel.ld asserts
    ; __bss_end never reaches this stack region.
    mov esp, 0x800000
    cld
    jmp CODE_SEG:0x10000

menu_selection dd 0
menu_elapsed_counts dd 0
menu_seconds_left dd MENU_TIMEOUT_SECONDS
menu_prev_pit dw 0
menu_base_x dd 0
menu_base_y dd 0
background_base_x dd 0
background_base_y dd 0
background_scale dd 1
menu_dirty db 0
menu_initialized db 0
menu_timeout_paused db 0
menu_first_render_done db 0
menu_force_full_redraw db 0
menu_loop_stable_logged db 0
menu_extended db 0
menu_last_scancode db 0
menu_last_extended_flag db 0
menu_last_action db ' '
glyph_row_bits db 0
glyph_draw_color db 0
glyph_ptr_tmp dd 0
glyph_draw_x dd 0
glyph_draw_y dd 0
glyph_draw_scale dd 0
glyph_fb_ptr dd 0
glyph_fb_pitch dd 0
glyph_fb_width dd 0
glyph_fb_height dd 0
glyph_row_ptr dd 0
pm_exception_vector db 0xFF

vibeloader_title db 'VIBELOADER', 0
menu_entry_vibeos db 'VIBEOS', 0
menu_entry_safe db 'SAFE MODE', 0
menu_entry_rescue db 'RESCUE SHELL', 0
menu_entry_legacy_video db 'LEGACY VIDEO', 0
menu_entry_labels dd menu_entry_vibeos, menu_entry_safe, menu_entry_rescue, menu_entry_legacy_video
menu_entry_boot_flags dd BOOTINFO_FLAG_BOOT_TO_DESKTOP, BOOTINFO_FLAG_BOOT_SAFE_MODE, BOOTINFO_FLAG_BOOT_RESCUE_SHELL, (BOOTINFO_FLAG_BOOT_TO_DESKTOP | BOOTINFO_FLAG_FORCE_LEGACY_VIDEO)
countdown_text db 'AUTO BOOT IN 0S', 0
countdown_paused_text db 'AUTO BOOT PAUSED', 0
menu_video_prefix db 'VIDEO ', 0
menu_video_unknown db 'UNKNOWN', 0
menu_hint_top db 'UP/DOWN SELECT  ENTER BOOT', 0
menu_hint_bottom db 'LEFT/RIGHT VIDEO  V DRIVER  I I915 EXP  M SMP EXP', 0
menu_video_text times 24 db 0
menu_i915_label db 'I915 EXP ', 0
menu_i915_on db 'ON', 0
menu_i915_off db 'OFF', 0
menu_i915_text times 16 db 0
menu_smp_label db 'SMP EXP ', 0
menu_smp_on db 'ON', 0
menu_smp_off db 'OFF', 0
menu_smp_text times 16 db 0
menu_driver_label db 'DRIVER ', 0
menu_driver_auto db 'AUTO', 0
menu_driver_legacy db 'LEGACY', 0
menu_driver_text times 20 db 0
menu_debug_prefix db 'DBG ', 0
menu_key_prefix db 'KEY ', 0
menu_ext_prefix db 'EXT ', 0
menu_act_prefix db 'ACT ', 0
menu_i915_prefix db 'I915 ', 0
menu_smp_prefix db 'SMP ', 0
menu_debug_text times 48 db 0
menu_number_buffer times 6 db 0
text_no_vesa_title db 'VIBELOADER DEBUG HALT', 0
text_last_label db 'LAST ', 0
text_exc_label db 'EXC ', 0
text_flags_label db 'FLAGS ', 0
text_mode_label db 'MODE ', 0
text_bpp_label db 'BPP ', 0
text_width_label db 'W ', 0
text_height_label db ' H ', 0
text_hint_enter db 'HALTED FOR DEBUG', 0

glyph_masks db 16, 8, 4, 2, 1

pm_text_row dd 0
pm_text_col dd 0

ega_palette_6bit:
    db 0, 0, 0
    db 0, 0, 42
    db 0, 42, 0
    db 0, 42, 42
    db 42, 0, 0
    db 42, 0, 42
    db 42, 21, 0
    db 42, 42, 42
    db 21, 21, 21
    db 21, 21, 63
    db 21, 63, 21
    db 21, 63, 63
    db 63, 21, 21
    db 63, 21, 63
    db 63, 63, 21
    db 63, 63, 63

font_glyphs:
    ; A-Z
    db 0x0E,0x11,0x11,0x1F,0x11,0x11,0x11
    db 0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E
    db 0x0E,0x11,0x10,0x10,0x10,0x11,0x0E
    db 0x1E,0x11,0x11,0x11,0x11,0x11,0x1E
    db 0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F
    db 0x1F,0x10,0x10,0x1E,0x10,0x10,0x10
    db 0x0E,0x11,0x10,0x10,0x13,0x11,0x0F
    db 0x11,0x11,0x11,0x1F,0x11,0x11,0x11
    db 0x0E,0x04,0x04,0x04,0x04,0x04,0x0E
    db 0x01,0x01,0x01,0x01,0x11,0x11,0x0E
    db 0x11,0x12,0x14,0x18,0x14,0x12,0x11
    db 0x10,0x10,0x10,0x10,0x10,0x10,0x1F
    db 0x11,0x1B,0x15,0x15,0x11,0x11,0x11
    db 0x11,0x19,0x15,0x13,0x11,0x11,0x11
    db 0x0E,0x11,0x11,0x11,0x11,0x11,0x0E
    db 0x1E,0x11,0x11,0x1E,0x10,0x10,0x10
    db 0x0E,0x11,0x11,0x11,0x15,0x12,0x0D
    db 0x1E,0x11,0x11,0x1E,0x14,0x12,0x11
    db 0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E
    db 0x1F,0x04,0x04,0x04,0x04,0x04,0x04
    db 0x11,0x11,0x11,0x11,0x11,0x11,0x0E
    db 0x11,0x11,0x11,0x11,0x11,0x0A,0x04
    db 0x11,0x11,0x11,0x15,0x15,0x15,0x0A
    db 0x11,0x11,0x0A,0x04,0x0A,0x11,0x11
    db 0x11,0x11,0x0A,0x04,0x04,0x04,0x04
    db 0x1F,0x01,0x02,0x04,0x08,0x10,0x1F
    ; 0-9
    db 0x0E,0x11,0x13,0x15,0x19,0x11,0x0E
    db 0x04,0x0C,0x04,0x04,0x04,0x04,0x0E
    db 0x0E,0x11,0x01,0x02,0x04,0x08,0x1F
    db 0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E
    db 0x02,0x06,0x0A,0x12,0x1F,0x02,0x02
    db 0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E
    db 0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E
    db 0x1F,0x01,0x02,0x04,0x08,0x08,0x08
    db 0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E
    db 0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E
font_space:
    db 0x00,0x00,0x00,0x00,0x00,0x00,0x00
