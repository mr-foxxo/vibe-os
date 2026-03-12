; simple cooperative context switch routine
; arguments (cdecl):
;   [esp]   return address
;   [esp+4] pointer to old task (regs block) or NULL
;   [esp+8] pointer to new task (regs block)
;
; the layout of the regs block is defined in kernel/include/process.h.
; because regs_t is the first member of process_t the two pointers
; are interchangeable as long as we keep the struct order.

    global context_switch
context_switch:
    ; grab parameters
    mov ebx, [esp+4]    ; old regs pointer (may be NULL)
    mov esi, [esp+8]    ; new regs pointer

    cmp ebx, 0
    je .load_new        ; nothing to save if old==NULL

    ; save user registers into *ebx
    mov eax, [esp]      ; caller's eip (return address)
    pushad              ; push eax,ecx,edx,ebx,esp,ebp,esi,edi
    ; esp now points to saved EAX
    mov [ebx + 0], eax  ; regs.eip
    mov ecx, [esp+16]   ; original ESP saved by pushad
    mov [ebx + 4], ecx  ; regs.esp
    mov ecx, [esp+20]   ; EBP
    mov [ebx + 8], ecx  ; regs.ebp
    mov ecx, [esp]      ; EAX
    mov [ebx +12], ecx  ; regs.eax
    mov ecx, [esp+12]   ; EBX
    mov [ebx +16], ecx  ; regs.ebx
    mov ecx, [esp+4]    ; ECX
    mov [ebx +20], ecx  ; regs.ecx
    mov ecx, [esp+8]    ; EDX
    mov [ebx +24], ecx  ; regs.edx
    mov ecx, [esp+24]   ; ESI
    mov [ebx +28], ecx  ; regs.esi
    mov ecx, [esp+28]   ; EDI
    mov [ebx +32], ecx  ; regs.edi
    popad               ; restore registers to original state

.load_new:
    ; load new context pointed to by esi
    mov ebx, esi        ; ebx := new regs
    mov eax, [ebx +12]  ; eax
    mov ecx, [ebx +20]  ; ecx
    mov edx, [ebx +24]  ; edx
    mov ebp, [ebx + 8]  ; ebp
    mov esi, [ebx +28]  ; esi
    mov edi, [ebx +32]  ; edi
    mov esp, [ebx + 4]  ; esp
    jmp [ebx + 0]       ; jump to new eip
