; legacy interrupt stub file used by original stage2 kernel
; the new kernel has moved all ISR/IRQ stubs into kernel_asm/isr.asm
; which is compiled and linked instead.  this file is kept for
; historical reference and is no longer built or linked.
;
; if you need to re-enable the old stubs for some reason, restore the
; NASM rule in the Makefile and uncomment the definitions below.
;
;BITS 32
;
;global irq0_stub
;global irq1_stub
;global irq12_stub
;global syscall_stub
;extern timer_irq_handler_c
;extern keyboard_irq_handler_c
;extern mouse_irq_handler_c
;extern syscall_dispatch
;
;irq0_stub:
;    pusha
;    cld
;    call timer_irq_handler_c
;    popa
;    iretd
;
;irq1_stub:
;    pusha
;    cld
;    call keyboard_irq_handler_c
;    popa
;    iretd
;
;irq12_stub:
;    pusha
;    cld
;    call mouse_irq_handler_c
;    popa
;    iretd
;
;syscall_stub:
;    pusha
;    cld
;    push esp
;    call syscall_dispatch
;    add esp, 4
;    popa
;    iretd
