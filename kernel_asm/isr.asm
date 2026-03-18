BITS 32

; central ISR assembly for kernel only

global irq0_stub
global irq1_stub
global irq12_stub
global syscall_stub

global divide_error_stub
global invalid_opcode_stub
global general_protection_stub
global page_fault_stub
global double_fault_stub

extern kernel_timer_irq_handler
extern kernel_keyboard_irq_handler
extern kernel_mouse_irq_handler
extern syscall_dispatch_internal

extern divide_error_handler
extern invalid_opcode_handler
extern general_protection_handler
extern page_fault_handler
extern double_fault_handler

irq0_stub:
    pusha
    cld
    call kernel_timer_irq_handler
    popa
    iretd

irq1_stub:
    pusha
    cld
    call kernel_keyboard_irq_handler
    popa
    iretd

irq12_stub:
    pusha
    cld
    call kernel_mouse_irq_handler
    popa
    iretd

; syscall_stub: pass registers as function arguments to syscall_dispatch_internal
; syscall_dispatch_internal(uint32_t num, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
; eax = syscall num
; ebx = arg1, ecx = arg2, edx = arg3, esi = arg4, edi = arg5
syscall_stub:
    pusha
    cld
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
    popa
    iretd

; exception stubs - invoke C handler and halt (via iretd)
divide_error_stub:
    pusha
    cld
    call divide_error_handler
    popa
    iretd

invalid_opcode_stub:
    pusha
    cld
    mov eax, [esp + 32]    ; saved EIP (after pusha)
    push eax
    call invalid_opcode_handler
    add esp, 4
    popa
    iretd

general_protection_stub:
    pusha
    cld
    call general_protection_handler
    popa
    iretd

page_fault_stub:
    pusha
    cld
    call page_fault_handler
    popa
    iretd

double_fault_stub:
    pusha
    cld
    call double_fault_handler
    popa
    iretd
