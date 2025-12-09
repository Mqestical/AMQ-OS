; ISR stub macros for x86-64
; These push the interrupt number and call the C handlers

%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    push qword 0           ; Push dummy error code
    push qword %1          ; Push interrupt number
    jmp isr_common_stub
%endmacro

%macro ISR_ERRCODE 1
global isr%1
isr%1:
    push qword %1          ; Push interrupt number (error code already pushed)
    jmp isr_common_stub
%endmacro

; Define all 32 exception ISRs
ISR_NOERRCODE 0    ; Divide by zero
ISR_NOERRCODE 1    ; Debug
ISR_NOERRCODE 2    ; NMI
ISR_NOERRCODE 3    ; Breakpoint
ISR_NOERRCODE 4    ; Overflow
ISR_NOERRCODE 5    ; Bound Range Exceeded
ISR_NOERRCODE 6    ; Invalid Opcode
ISR_NOERRCODE 7    ; Device Not Available
ISR_ERRCODE   8    ; Double Fault (has error code)
ISR_NOERRCODE 9    ; Coprocessor Segment Overrun
ISR_ERRCODE   10   ; Invalid TSS (has error code)
ISR_ERRCODE   11   ; Segment Not Present (has error code)
ISR_ERRCODE   12   ; Stack Segment Fault (has error code)
ISR_ERRCODE   13   ; General Protection Fault (has error code)
ISR_ERRCODE   14   ; Page Fault (has error code)
ISR_NOERRCODE 15   ; Reserved
ISR_NOERRCODE 16   ; x87 FPU Error
ISR_ERRCODE   17   ; Alignment Check (has error code)
ISR_NOERRCODE 18   ; Machine Check
ISR_NOERRCODE 19   ; SIMD Floating Point
ISR_NOERRCODE 20   ; Virtualization
ISR_NOERRCODE 21   ; Reserved
ISR_NOERRCODE 22   ; Reserved
ISR_NOERRCODE 23   ; Reserved
ISR_NOERRCODE 24   ; Reserved
ISR_NOERRCODE 25   ; Reserved
ISR_NOERRCODE 26   ; Reserved
ISR_NOERRCODE 27   ; Reserved
ISR_NOERRCODE 28   ; Reserved
ISR_NOERRCODE 29   ; Reserved
ISR_ERRCODE   30   ; Security Exception (has error code)
ISR_NOERRCODE 31   ; Reserved

extern isr_handler

isr_common_stub:
    ; Save all registers
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    
    ; Pass pointer to register structure as argument
    mov rdi, rsp
    
    ; Call C handler
    call isr_handler
    
    ; Restore all registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    
    ; Clean up error code and interrupt number
    add rsp, 16
    
    ; Return from interrupt
    iretq