BITS 32

; central ISR assembly for kernel only

%define KERNEL_DS_SELECTOR 0x10
%define USER_DS_SELECTOR   0x23

global irq0_stub
global irq1_stub
global irq2_stub
global irq3_stub
global irq4_stub
global irq5_stub
global irq6_stub
global irq7_stub
global irq8_stub
global irq9_stub
global irq10_stub
global irq11_stub
global irq12_stub
global irq13_stub
global irq14_stub
global irq15_stub
global syscall_stub
global yield_stub
global smp_wakeup_stub
global smp_timer_stub

global divide_error_stub
global debug_stub
global int3_stub
global invalid_opcode_stub
global invalid_tss_stub
global segment_not_present_stub
global stack_fault_stub
global general_protection_stub
global page_fault_stub
global double_fault_stub

extern kernel_timer_irq_handler
extern kernel_keyboard_irq_handler
extern kernel_mouse_irq_handler
extern kernel_irq_dispatch
extern scheduler_schedule_frame
extern syscall_dispatch_internal
extern smp_wakeup_ipi_irq_handler
extern smp_timer_irq_handler

extern divide_error_handler
extern debug_handler
extern int3_handler
extern invalid_opcode_handler
extern invalid_tss_handler
extern segment_not_present_handler
extern stack_fault_handler
extern general_protection_handler
extern page_fault_handler
extern double_fault_handler

%macro LOAD_KERNEL_DATA_SEGMENTS 0
    mov ax, KERNEL_DS_SELECTOR
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
%endmacro

%macro LOAD_RETURN_DATA_SEGMENTS 0
    mov ax, [esp + 36]      ; saved CS in the resumed trap frame
    test al, 0x3
    jz %%kernel_segments
    mov ax, USER_DS_SELECTOR
    jmp %%install_segments
%%kernel_segments:
    mov ax, KERNEL_DS_SELECTOR
%%install_segments:
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
%endmacro

irq0_stub:
    pusha
    cld
    LOAD_KERNEL_DATA_SEGMENTS
    mov eax, esp
    push eax
    call kernel_timer_irq_handler
    add esp, 4
    mov esp, eax
    LOAD_RETURN_DATA_SEGMENTS
    popa
    iretd

irq1_stub:
    pusha
    cld
    LOAD_KERNEL_DATA_SEGMENTS
    call kernel_keyboard_irq_handler
    LOAD_RETURN_DATA_SEGMENTS
    popa
    iretd

%macro IRQ_STUB 2
irq%1_stub:
    pusha
    cld
    LOAD_KERNEL_DATA_SEGMENTS
    push dword %2
    call kernel_irq_dispatch
    add esp, 4
    LOAD_RETURN_DATA_SEGMENTS
    popa
    iretd
%endmacro

IRQ_STUB 2, 2
IRQ_STUB 3, 3
IRQ_STUB 4, 4
IRQ_STUB 5, 5
IRQ_STUB 6, 6
IRQ_STUB 7, 7
IRQ_STUB 8, 8
IRQ_STUB 9, 9
IRQ_STUB 10, 10
IRQ_STUB 11, 11

irq12_stub:
    pusha
    cld
    LOAD_KERNEL_DATA_SEGMENTS
    call kernel_mouse_irq_handler
    LOAD_RETURN_DATA_SEGMENTS
    popa
    iretd

IRQ_STUB 13, 13
IRQ_STUB 14, 14
IRQ_STUB 15, 15

yield_stub:
    pusha
    cld
    LOAD_KERNEL_DATA_SEGMENTS
    mov eax, esp
    push dword 0
    push eax
    call scheduler_schedule_frame
    add esp, 8
    mov esp, eax
    LOAD_RETURN_DATA_SEGMENTS
    popa
    iretd

; Wake/reschedule IPI (vector 0x82). Adendo 45: same frame-swap pattern as
; smp_timer_stub — the handler may return a DIFFERENT frame (context switch)
; when a boosted wake lands on a running CPU. Before this change the IPI
; only broke hlt; a CPU executing a task waited for yield/tick.
smp_wakeup_stub:
    pusha
    cld
    LOAD_KERNEL_DATA_SEGMENTS
    mov eax, esp
    push eax
    call smp_wakeup_ipi_irq_handler
    add esp, 4
    mov esp, eax
    LOAD_RETURN_DATA_SEGMENTS
    popa
    iretd

; Per-CPU LAPIC periodic timer (AP preemption tick): same frame-swap pattern
; as irq0_stub, but EOI is local-APIC side and the tick does not advance the
; global PIT tick counter.
smp_timer_stub:
    pusha
    cld
    LOAD_KERNEL_DATA_SEGMENTS
    mov eax, esp
    push eax
    call smp_timer_irq_handler
    add esp, 4
    mov esp, eax
    LOAD_RETURN_DATA_SEGMENTS
    popa
    iretd

; syscall_stub: pass registers as function arguments to syscall_dispatch_internal
; syscall_dispatch_internal(uint32_t num, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
; eax = syscall num
; ebx = arg1, ecx = arg2, edx = arg3, esi = arg4, edi = arg5
syscall_stub:
    pusha
    cld
    LOAD_KERNEL_DATA_SEGMENTS
    ; Saved registers after pusha:
    ; [esp + 0]  = edi
    ; [esp + 4]  = esi
    ; [esp + 8]  = ebp
    ; [esp + 12] = original esp
    ; [esp + 16] = ebx
    ; [esp + 20] = edx
    ; [esp + 24] = ecx
    ; [esp + 28] = eax
    mov eax, [esp + 28]
    mov ebx, [esp + 16]
    mov ecx, [esp + 24]
    mov edx, [esp + 20]
    mov esi, [esp + 4]
    mov edi, [esp + 0]
    push edi
    push esi
    push edx
    push ecx
    push ebx
    push eax
    call syscall_dispatch_internal
    add esp, 24
    mov [esp + 28], eax
    LOAD_RETURN_DATA_SEGMENTS
    popa
    iretd

; exception stubs - invoke C handler and halt (via iretd)
divide_error_stub:
    pusha
    cld
    LOAD_KERNEL_DATA_SEGMENTS
    mov eax, [esp + 32]    ; saved EIP
    mov edx, [esp + 36]    ; saved CS
    push edx
    push eax
    call divide_error_handler
    add esp, 8
    popa
    iretd

int3_stub:
    pusha
    cld
    LOAD_KERNEL_DATA_SEGMENTS
    mov eax, [esp + 32]    ; saved EIP (no error code for INT3)
    mov edx, [esp + 36]    ; saved CS
    push edx
    push eax
    call int3_handler
    add esp, 8
    popa
    iretd

invalid_opcode_stub:
    pusha
    cld
    LOAD_KERNEL_DATA_SEGMENTS
    mov eax, [esp + 32]    ; saved EIP (after pusha)
    mov edx, [esp + 36]    ; saved CS
    push edx
    push eax
    call invalid_opcode_handler
    add esp, 8
    popa
    iretd

%macro EXCEPTION_ERROR_CODE_STUB 2
%1:
    pusha
    cld
    LOAD_KERNEL_DATA_SEGMENTS
    mov eax, [esp + 32]    ; CPU-pushed error code
    mov edx, [esp + 36]    ; saved EIP
    mov ecx, [esp + 40]    ; saved CS
    push ecx
    push edx
    push eax
    call %2
    add esp, 12
    popa
    add esp, 4             ; discard error code before returning
    iretd
%endmacro

EXCEPTION_ERROR_CODE_STUB invalid_tss_stub, invalid_tss_handler
EXCEPTION_ERROR_CODE_STUB segment_not_present_stub, segment_not_present_handler
EXCEPTION_ERROR_CODE_STUB stack_fault_stub, stack_fault_handler
EXCEPTION_ERROR_CODE_STUB debug_stub, debug_handler
EXCEPTION_ERROR_CODE_STUB general_protection_stub, general_protection_handler
EXCEPTION_ERROR_CODE_STUB page_fault_stub, page_fault_handler
EXCEPTION_ERROR_CODE_STUB double_fault_stub, double_fault_handler
