#include "handler.h"
#include "print.h"
#include "string_helpers.h"

void fatal_error(registers_t* r);

// Forward declarations for CPU exception handlers
void isr0(registers_t* r);  void isr1(registers_t* r);  void isr2(registers_t* r);
void isr3(registers_t* r);  void isr4(registers_t* r);  void isr5(registers_t* r);
void isr6(registers_t* r);  void isr7(registers_t* r);  void isr8(registers_t* r);
void isr9(registers_t* r);  void isr10(registers_t* r); void isr11(registers_t* r);
void isr12(registers_t* r); void isr13(registers_t* r); void isr14(registers_t* r);
void isr15(registers_t* r); void isr16(registers_t* r); void isr17(registers_t* r);
void isr18(registers_t* r); void isr19(registers_t* r); void isr20(registers_t* r);
void isr21(registers_t* r); void isr22(registers_t* r); void isr23(registers_t* r);
void isr24(registers_t* r); void isr25(registers_t* r); void isr26(registers_t* r);
void isr27(registers_t* r); void isr28(registers_t* r); void isr29(registers_t* r);
void isr30(registers_t* r); void isr31(registers_t* r);

void isr0(registers_t* r) {
    if (r->int_no == 0) {
        PRINT(RED, BLACK, "Divide-by-zero exception!\n");
        PRINT(RED, BLACK, "Faulting instruction at RIP = %p\n", r->rip);
        PRINT(RED, BLACK, "RAX = %llx, RBX = %llx, RCX = %llx, RDX = %llx\n",
              r->rax, r->rbx, r->rcx, r->rdx);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr1(registers_t* r) {
    if (r->int_no == 1) {
        PRINT(RED, BLACK, "Debug exception!\n");
        PRINT(RED, BLACK, "RIP = %p, RFLAGS = %llx\n", r->rip, r->rflags);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr2(registers_t* r) {
    if (r->int_no == 2) {
        PRINT(RED, BLACK, "Non-Maskable Interrupt (NMI)!\n");
        PRINT(RED, BLACK, "RIP = %p, RFLAGS = %llx\n", r->rip, r->rflags);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr3(registers_t* r) {
    if (r->int_no == 3) {
        PRINT(RED, BLACK, "Breakpoint Exception (INT3)!\n");
        PRINT(RED, BLACK, "RIP = %p, RFLAGS = %llx\n", r->rip, r->rflags);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr4(registers_t* r) {
    if (r->int_no == 4) {
        PRINT(RED, BLACK, "Overflow Exception (INTO)!\n");
        PRINT(RED, BLACK, "RIP = %p, RFLAGS = %llx\n", r->rip, r->rflags);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr5(registers_t* r) {
    if (r->int_no == 5) {
        PRINT(RED, BLACK, "BOUND Range Exceeded Exception!\n");
        PRINT(RED, BLACK, "RIP = %p\n", r->rip);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr6(registers_t* r) {
    if (r->int_no == 6) {
        PRINT(RED, BLACK, "Invalid Opcode Exception!\n");
        PRINT(RED, BLACK, "RIP = %p\n", r->rip);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr7(registers_t* r) {
    if (r->int_no == 7) {
        PRINT(RED, BLACK, "Device Not Available Exception (FPU)!\n");
        PRINT(RED, BLACK, "RIP = %p\n", r->rip);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr8(registers_t* r) {
    if (r->int_no == 8) {
        PRINT(RED, BLACK, "Double Fault!\n");
        PRINT(RED, BLACK, "Error code = %llx, RIP = %p\n", r->err_code, r->rip);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr9(registers_t* r) {
    if (r->int_no == 9) {
        PRINT(RED, BLACK, "Coprocessor Segment Overrun Exception!\n");
        PRINT(RED, BLACK, "RIP = %p\n", r->rip);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr10(registers_t* r) {
    if (r->int_no == 10) {
        PRINT(RED, BLACK, "Invalid TSS Exception!\n");
        PRINT(RED, BLACK, "Error code = %llx, RIP = %p\n", r->err_code, r->rip);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr11(registers_t* r) {
    if (r->int_no == 11) {
        PRINT(RED, BLACK, "Segment Not Present Exception!\n");
        PRINT(RED, BLACK, "Error code = %llx, RIP = %p\n", r->err_code, r->rip);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr12(registers_t* r) {
    if (r->int_no == 12) {
        PRINT(RED, BLACK, "Stack Segment Fault Exception!\n");
        PRINT(RED, BLACK, "Error code = %llx, RIP = %p\n", r->err_code, r->rip);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr13(registers_t* r) {
    if (r->int_no == 13) {
        PRINT(RED, BLACK, "General Protection Fault!\n");
        PRINT(RED, BLACK, "Error code = %llx, RIP = %p\n", r->err_code, r->rip);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr14(registers_t* r) {
    if (r->int_no == 14) {
        uint64_t cr2;
        asm volatile("mov %%cr2, %0" : "=r"(cr2));
        PRINT(RED, BLACK, "Page Fault!\n");
        PRINT(RED, BLACK, "Error code = %llx, RIP = %p\n", r->err_code, r->rip);
        PRINT(RED, BLACK, "Faulting address = %p\n", cr2);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

// ISR15–ISR31 follow same pattern as above; all use PRINT(...) instead of printk
// Reserved exceptions simply report vector and RIP

#define DEFINE_RESERVED_ISR(n) \
void isr##n(registers_t* r) { \
    if (r->int_no == n) { \
        PRINT(RED, BLACK, "Reserved Exception (" #n ")!\n"); \
        PRINT(RED, BLACK, "RIP = %p\n", r->rip); \
    } else { \
        fatal_error(r); \
    } \
    for(;;) asm volatile("hlt"); \
}

DEFINE_RESERVED_ISR(15)
DEFINE_RESERVED_ISR(16)
DEFINE_RESERVED_ISR(17)
DEFINE_RESERVED_ISR(18)
DEFINE_RESERVED_ISR(19)
DEFINE_RESERVED_ISR(20)
DEFINE_RESERVED_ISR(21)
DEFINE_RESERVED_ISR(22)
DEFINE_RESERVED_ISR(23)
DEFINE_RESERVED_ISR(24)
DEFINE_RESERVED_ISR(25)
DEFINE_RESERVED_ISR(26)
DEFINE_RESERVED_ISR(27)
DEFINE_RESERVED_ISR(28)
DEFINE_RESERVED_ISR(29)
DEFINE_RESERVED_ISR(30)
DEFINE_RESERVED_ISR(31)

void fatal_error(registers_t* r) {
    PRINT(RED, BLACK, "FATAL ERROR: UNKNOWN VECTOR!\n");
    for(;;) asm volatile("hlt");
}
