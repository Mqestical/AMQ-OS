#include "handler.h"
#include "print.h"
#include "string_helpers.h"

// This function is called from assembly with a pointer to the register state
void isr_handler(registers_t* r) {
    switch(r->int_no) {
        case 0:
            PRINT(RED, BLACK, "Divide-by-zero exception!\n");
            PRINT(RED, BLACK, "Faulting instruction at RIP = %p\n", r->rip);
            PRINT(RED, BLACK, "RAX = %llx, RBX = %llx, RCX = %llx, RDX = %llx\n",
                  r->rax, r->rbx, r->rcx, r->rdx);
            break;
            
        case 1:
            PRINT(RED, BLACK, "Debug exception!\n");
            PRINT(RED, BLACK, "RIP = %p, RFLAGS = %llx\n", r->rip, r->rflags);
            break;
            
        case 2:
            PRINT(RED, BLACK, "Non-Maskable Interrupt (NMI)!\n");
            PRINT(RED, BLACK, "RIP = %p, RFLAGS = %llx\n", r->rip, r->rflags);
            break;
            
        case 3:
            PRINT(RED, BLACK, "Breakpoint Exception (INT3)!\n");
            PRINT(RED, BLACK, "RIP = %p, RFLAGS = %llx\n", r->rip, r->rflags);
            break;
            
        case 4:
            PRINT(RED, BLACK, "Overflow Exception (INTO)!\n");
            PRINT(RED, BLACK, "RIP = %p, RFLAGS = %llx\n", r->rip, r->rflags);
            break;
            
        case 5:
            PRINT(RED, BLACK, "BOUND Range Exceeded Exception!\n");
            PRINT(RED, BLACK, "RIP = %p\n", r->rip);
            break;
            
        case 6:
            PRINT(RED, BLACK, "Invalid Opcode Exception!\n");
            PRINT(RED, BLACK, "RIP = %p\n", r->rip);
            break;
            
        case 7:
            PRINT(RED, BLACK, "Device Not Available Exception (FPU)!\n");
            PRINT(RED, BLACK, "RIP = %p\n", r->rip);
            break;
            
        case 8:
            PRINT(RED, BLACK, "Double Fault!\n");
            PRINT(RED, BLACK, "Error code = %llx, RIP = %p\n", r->err_code, r->rip);
            break;
            
        case 9:
            PRINT(RED, BLACK, "Coprocessor Segment Overrun Exception!\n");
            PRINT(RED, BLACK, "RIP = %p\n", r->rip);
            break;
            
        case 10:
            PRINT(RED, BLACK, "Invalid TSS Exception!\n");
            PRINT(RED, BLACK, "Error code = %llx, RIP = %p\n", r->err_code, r->rip);
            break;
            
        case 11:
            PRINT(RED, BLACK, "Segment Not Present Exception!\n");
            PRINT(RED, BLACK, "Error code = %llx, RIP = %p\n", r->err_code, r->rip);
            break;
            
        case 12:
            PRINT(RED, BLACK, "Stack Segment Fault Exception!\n");
            PRINT(RED, BLACK, "Error code = %llx, RIP = %p\n", r->err_code, r->rip);
            break;
            
        case 13:
            PRINT(RED, BLACK, "General Protection Fault!\n");
            PRINT(RED, BLACK, "Error code = %llx, RIP = %p\n", r->err_code, r->rip);
            break;
            
        case 14: {
            uint64_t cr2;
            asm volatile("mov %%cr2, %0" : "=r"(cr2));
            PRINT(RED, BLACK, "Page Fault!\n");
            PRINT(RED, BLACK, "Error code = %llx, RIP = %p\n", r->err_code, r->rip);
            PRINT(RED, BLACK, "Faulting address = %p\n", cr2);
            break;
        }
            
        case 16:
            PRINT(RED, BLACK, "x87 FPU Error!\n");
            PRINT(RED, BLACK, "RIP = %p\n", r->rip);
            break;
            
        case 17:
            PRINT(RED, BLACK, "Alignment Check Exception!\n");
            PRINT(RED, BLACK, "Error code = %llx, RIP = %p\n", r->err_code, r->rip);
            break;
            
        case 18:
            PRINT(RED, BLACK, "Machine Check Exception!\n");
            PRINT(RED, BLACK, "RIP = %p\n", r->rip);
            break;
            
        case 19:
            PRINT(RED, BLACK, "SIMD Floating Point Exception!\n");
            PRINT(RED, BLACK, "RIP = %p\n", r->rip);
            break;
            
        default:
            if (r->int_no >= 15 && r->int_no <= 31) {
                PRINT(RED, BLACK, "Reserved Exception (%d)!\n", r->int_no);
                PRINT(RED, BLACK, "RIP = %p\n", r->rip);
            } else {
                PRINT(RED, BLACK, "FATAL ERROR: UNKNOWN VECTOR %d!\n", r->int_no);
                PRINT(RED, BLACK, "RIP = %p\n", r->rip);
            }
            break;
    }
    
    // Halt the system
    for(;;) asm volatile("hlt");
}