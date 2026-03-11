BITS 32

; central ISR assembly; shared between stage2 and new kernel

global irq0_stub
global irq1_stub
global irq12_stub
global syscall_stub

global divide_error_stub
global invalid_opcode_stub
global general_protection_stub
global page_fault_stub
global double_fault_stub

extern timer_irq_handler_c
extern keyboard_irq_handler_c
extern mouse_irq_handler_c
extern syscall_dispatch

extern divide_error_handler
extern invalid_opcode_handler
extern general_protection_handler
extern page_fault_handler
extern double_fault_handler

irq0_stub:
    pusha
    cld
    call timer_irq_handler_c
    popa
    iretd

irq1_stub:
    pusha
    cld
    call keyboard_irq_handler_c
    popa
    iretd

irq12_stub:
    pusha
    cld
    call mouse_irq_handler_c
    popa
    iretd

syscall_stub:
    pusha
    cld
    push esp
    call syscall_dispatch
    add esp, 4
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
    call invalid_opcode_handler
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
