#include "handler.h"
#include "print.h"

void fatal_error(registers_t* r);

// Forward declarations for CPU exception handlers
void isr0(registers_t* r);
void isr1(registers_t* r);
void isr2(registers_t* r);
void isr3(registers_t* r);
void isr4(registers_t* r);
void isr5(registers_t* r);
void isr6(registers_t* r);
void isr7(registers_t* r);
void isr8(registers_t* r);
void isr9(registers_t* r);
void isr10(registers_t* r);
void isr11(registers_t* r);
void isr12(registers_t* r);
void isr13(registers_t* r);
void isr14(registers_t* r);
void isr15(registers_t* r);
void isr16(registers_t* r);
void isr17(registers_t* r);
void isr18(registers_t* r);
void isr19(registers_t* r);
void isr20(registers_t* r);
void isr21(registers_t* r);
void isr22(registers_t* r);
void isr23(registers_t* r);
void isr24(registers_t* r);
void isr25(registers_t* r);
void isr26(registers_t* r);
void isr27(registers_t* r);
void isr28(registers_t* r);
void isr29(registers_t* r);
void isr30(registers_t* r);
void isr31(registers_t* r);

void isr0(registers_t* r) {
    char m1[] = "Divide-by-zero exception!\n";
    char m2[] = "Faulting instruction at RIP = %p\n";
    char m3[] = "RAX = %llx, RBX = %llx, RCX = %llx, RDX = %llx\n";
    if (r->int_no == 0) {  // Divide-by-zero
        printk(0xFF0000, 0x000000, m1);
        printk(0xFF0000, 0x000000, m2, r->rip);
        printk(0xFF0000, 0x000000, m3,
               r->rax, r->rbx, r->rcx, r->rdx);
    } else {
        fatal_error(r);
    }

    for(;;) asm volatile("hlt");
}

void isr1(registers_t* r) {
    char m1[] = "Debug exception!\n";
    char m2[] = "RIP = %p, RFLAGS = %llx\n";
    if (r->int_no == 1) {
    printk(0xFF0000, 0x000000, m1);
    printk(0xFF0000, 0x000000, m2, r->rip, r->rflags);
}

else {
    fatal_error(r);
}
    }

void isr2(registers_t* r) {  // NMI
    char m1[] = "Non-Maskable Interrupt (NMI)!\n";
    char m2[] = "RIP = %p, RFLAGS = %llx\n";
    if (r->int_no == 2) {
        printk(0xFF0000, 0x000000, m1);
        printk(0xFF0000, 0x000000, m2, r->rip, r->rflags);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr3(registers_t* r) {  // Breakpoint
    char m1[] = "Breakpoint Exception (INT3)!\n";
    char m2[] = "RIP = %p, RFLAGS = %llx\n";
    if (r->int_no == 3) {
        printk(0xFF0000, 0x000000, m1);
        printk(0xFF0000, 0x000000, m2, r->rip, r->rflags);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr4(registers_t* r) {  // Overflow
    char m1[] = "Overflow Exception (INTO)!\n";
    char m2[] = "RIP = %p, RFLAGS = %llx\n";
    if (r->int_no == 4) {
        printk(0xFF0000, 0x000000, m1);
        printk(0xFF0000, 0x000000, m2, r->rip, r->rflags);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr5(registers_t* r) {  // Bound Range Exceeded
    char m1[] = "BOUND Range Exceeded Exception!\n";
    char m2[] = "RIP = %p\n";
    if (r->int_no == 5) {
        printk(0xFF0000, 0x000000, m1);
        printk(0xFF0000, 0x000000, m2, r->rip);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr6(registers_t* r) {  // Invalid Opcode
    char m1[] = "Invalid Opcode Exception!\n";
    char m2[] = "RIP = %p\n";
    if (r->int_no == 6) {
        printk(0xFF0000, 0x000000, m1);
        printk(0xFF0000, 0x000000, m2, r->rip);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr7(registers_t* r) {  // Device Not Available (FPU)
    char m1[] = "Device Not Available Exception (FPU)!\n";
    char m2[] = "RIP = %p\n";
    if (r->int_no == 7) {
        printk(0xFF0000, 0x000000, m1);
        printk(0xFF0000, 0x000000, m2, r->rip);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr8(registers_t* r) {  // Double Fault
    char m1[] = "Double Fault!\n";
    char m2[] = "Error code = %llx, RIP = %p\n";
    if (r->int_no == 8) {
        printk(0xFF0000, 0x000000, m1);
        printk(0xFF0000, 0x000000, m2, r->err_code, r->rip);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr9(registers_t* r) {  // Coprocessor Segment Overrun (historical)
    char m1[] = "Coprocessor Segment Overrun Exception!\n";
    char m2[] = "RIP = %p\n";
    if (r->int_no == 9) {
        printk(0xFF0000, 0x000000, m1);
        printk(0xFF0000, 0x000000, m2, r->rip);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr10(registers_t* r) {  // Invalid TSS
    char m1[] = "Invalid TSS Exception!\n";
    char m2[] = "Error code = %llx, RIP = %p\n";
    if (r->int_no == 10) {
        printk(0xFF0000, 0x000000, m1);
        printk(0xFF0000, 0x000000, m2, r->err_code, r->rip);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr11(registers_t* r) {  // Segment Not Present
    char m1[] = "Segment Not Present Exception!\n";
    char m2[] = "Error code = %llx, RIP = %p\n";
    if (r->int_no == 11) {
        printk(0xFF0000, 0x000000, m1);
        printk(0xFF0000, 0x000000, m2, r->err_code, r->rip);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr12(registers_t* r) {  // Stack Segment Fault
    char m1[] = "Stack Segment Fault Exception!\n";
    char m2[] = "Error code = %llx, RIP = %p\n";
    if (r->int_no == 12) {
        printk(0xFF0000, 0x000000, m1);
        printk(0xFF0000, 0x000000, m2, r->err_code, r->rip);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr13(registers_t* r) {  // General Protection Fault
    char m1[] = "General Protection Fault!\n";
    char m2[] = "Error code = %llx, RIP = %p\n";
    if (r->int_no == 13) {
        printk(0xFF0000, 0x000000, m1);
        printk(0xFF0000, 0x000000, m2, r->err_code, r->rip);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr14(registers_t* r) {  // Page Fault
    char m1[] = "Page Fault!\n";
    char m2[] = "Error code = %llx, RIP = %p\n";
    char m3[] = "Faulting address = %p\n";
    if (r->int_no == 14) {
        uint64_t cr2;
        asm volatile("mov %%cr2, %0" : "=r"(cr2));
        printk(0xFF0000, 0x000000, m1);
        printk(0xFF0000, 0x000000, m2, r->err_code, r->rip);
        printk(0xFF0000, 0x000000, m3, cr2);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr15(registers_t* r) {  // Reserved
    char m1[] = "Reserved Exception!\n";
    char m2[] = "RIP = %p\n";
    if (r->int_no == 15) {
        printk(0xFF0000, 0x000000, m1);
        printk(0xFF0000, 0x000000, m2, r->rip);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr16(registers_t* r) {  // x87 Floating-Point Exception
    char m1[] = "x87 Floating-Point Exception!\n";
    char m2[] = "RIP = %p\n";
    if (r->int_no == 16) {
        printk(0xFF0000, 0x000000, m1);
        printk(0xFF0000, 0x000000, m2, r->rip);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr17(registers_t* r) {  // Alignment Check
    char m1[] = "Alignment Check Exception!\n";
    char m2[] = "Error code = %llx, RIP = %p\n";
    if (r->int_no == 17) {
        printk(0xFF0000, 0x000000, m1);
        printk(0xFF0000, 0x000000, m2, r->err_code, r->rip);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr18(registers_t* r) {  // Machine Check
    char m1[] = "Machine Check Exception!\n";
    char m2[] = "RIP = %p\n";
    if (r->int_no == 18) {
        printk(0xFF0000, 0x000000, m1);
        printk(0xFF0000, 0x000000, m2, r->rip);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr19(registers_t* r) {  // SIMD Floating-Point Exception
    char m1[] = "SIMD Floating-Point Exception!\n";
    char m2[] = "RIP = %p\n";
    if (r->int_no == 19) {
        printk(0xFF0000, 0x000000, m1);
        printk(0xFF0000, 0x000000, m2, r->rip);
    } else {
       fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr20(registers_t* r) {  // Virtualization Exception
    char m1[] = "Virtualization Exception!\n";
    char m2[] = "RIP = %p\n";
    if (r->int_no == 20) {
        printk(0xFF0000, 0x000000, m1);
        printk(0xFF0000, 0x000000, m2, r->rip);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

// Vectors 21–25 are mostly reserved on older CPUs, but just in case:
void isr21(registers_t* r) {  // Reserved
    char m1[] = "Reserved Exception (21)!\n";
    char m2[] = "RIP = %p\n";
    if (r->int_no == 21) {
        printk(0xFF0000, 0x000000, m1);
        printk(0xFF0000, 0x000000, m2, r->rip);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr22(registers_t* r) {  // Reserved
    char m1[] = "Reserved Exception (22)!\n";
    char m2[] = "RIP = %p\n";
    if (r->int_no == 22) {
        printk(0xFF0000, 0x000000, m1);
        printk(0xFF0000, 0x000000, m2, r->rip);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr23(registers_t* r) {  // Reserved
    char m1[] = "Reserved Exception (23)!\n";
    char m2[] = "RIP = %p\n";
    if (r->int_no == 23) {
        printk(0xFF0000, 0x000000, m1);
        printk(0xFF0000, 0x000000, m2, r->rip);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr24(registers_t* r) {  // Reserved
    char m1[] = "Reserved Exception (24)!\n";
    char m2[] = "RIP = %p\n";
    if (r->int_no == 24) {
        printk(0xFF0000, 0x000000, m1);
        printk(0xFF0000, 0x000000, m2, r->rip);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr25(registers_t* r) {  // Reserved
    char m1[] = "Reserved Exception (25)!\n";
    char m2[] = "RIP = %p\n";
    if (r->int_no == 25) {
        printk(0xFF0000, 0x000000, m1);
        printk(0xFF0000, 0x000000, m2, r->rip);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr26(registers_t* r) {  // Reserved
    char m1[] = "Reserved Exception (26)!\n";
    char m2[] = "RIP = %p\n";
    if (r->int_no == 26) {
        printk(0xFF0000, 0x000000, m1);
        printk(0xFF0000, 0x000000, m2, r->rip);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr27(registers_t* r) {  // Reserved
    char m1[] = "Reserved Exception (27)!\n";
    char m2[] = "RIP = %p\n";
    if (r->int_no == 27) {
        printk(0xFF0000, 0x000000, m1);
        printk(0xFF0000, 0x000000, m2, r->rip);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr28(registers_t* r) {  // Reserved
    char m1[] = "Reserved Exception (28)!\n";
    char m2[] = "RIP = %p\n";
    if (r->int_no == 28) {
        printk(0xFF0000, 0x000000, m1);
        printk(0xFF0000, 0x000000, m2, r->rip);
    } else {
       fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr29(registers_t* r) {  // Reserved
    char m1[] = "Reserved Exception (29)!\n";
    char m2[] = "RIP = %p\n";
    if (r->int_no == 29) {
        printk(0xFF0000, 0x000000, m1);
        printk(0xFF0000, 0x000000, m2, r->rip);
    } else {
       fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr30(registers_t* r) {  // Security Exception
    char m1[] = "Security Exception!\n";
    char m2[] = "Error code = %llx, RIP = %p\n";
    if (r->int_no == 30) {
        printk(0xFF0000, 0x000000, m1);
        printk(0xFF0000, 0x000000, m2, r->err_code, r->rip);
    } else {
       fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void isr31(registers_t* r) {  // Reserved
    char m1[] = "Reserved Exception (31)!\n";
    char m2[] = "RIP = %p\n";
    if (r->int_no == 31) {
        printk(0xFF0000, 0x000000, m1);
        printk(0xFF0000, 0x000000, m2, r->rip);
    } else {
        fatal_error(r);
    }
    for(;;) asm volatile("hlt");
}

void fatal_error(registers_t* r) {
    char msg[] = "FATAL ERROR: UNKNOWN VECTOR!\n";
    printk(0xFF0000, 0x000000, msg);
    for(;;) asm volatile("hlt");
}