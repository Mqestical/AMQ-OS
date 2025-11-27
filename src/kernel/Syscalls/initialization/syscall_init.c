
#include "syscall.h"
#include "print.h"

// Forward declaration of dispatcher
extern uint64_t syscall_handler(uint64_t syscall_num, 
                                uint64_t arg1, uint64_t arg2, 
                                uint64_t arg3, uint64_t arg4,
                                uint64_t arg5, uint64_t arg6);

// ============================================================================
// SYSCALL ENTRY POINT (inline assembly)
// ============================================================================

// This is the actual entry point when userspace executes SYSCALL instruction
__attribute__((naked))
void syscall_entry(void) {
    __asm__ volatile(
        // At entry:
        // RCX = return RIP
        // R11 = saved RFLAGS
        // RAX = syscall number
        // RDI = arg1, RSI = arg2, RDX = arg3
        // R10 = arg4, R8 = arg5, R9 = arg6
        
        // Save user RSP and switch to kernel stack
        "swapgs\n"                      // Swap GS base (for per-CPU data)
        "mov %%rsp, %%gs:8\n"           // Save user RSP
        "mov %%gs:0, %%rsp\n"           // Load kernel RSP
        
        // Save user state
        "push %%r11\n"                  // RFLAGS
        "push %%rcx\n"                  // Return RIP
        "push %%rbp\n"
        "push %%rbx\n"
        "push %%r15\n"
        "push %%r14\n"
        "push %%r13\n"
        "push %%r12\n"
        
        // Setup arguments for C handler
        // C calling convention: RDI, RSI, RDX, RCX, R8, R9, stack
        "mov %%rax, %%rdi\n"            // arg0: syscall number
        // RSI already has arg1
        // RDX already has arg2
        "mov %%r10, %%rcx\n"            // arg3 (R10 → RCX for C ABI)
        // R8 already has arg4
        // R9 already has arg5
        
        // Align stack to 16 bytes and enable interrupts
        "and $-16, %%rsp\n"
        "sti\n"
        
        // Call C handler
        "call syscall_handler\n"
        
        // Disable interrupts before returning
        "cli\n"
        
        // Restore user state
        "pop %%r12\n"
        "pop %%r13\n"
        "pop %%r14\n"
        "pop %%r15\n"
        "pop %%rbx\n"
        "pop %%rbp\n"
        "pop %%rcx\n"                   // Return RIP
        "pop %%r11\n"                   // RFLAGS
        
        // Restore user stack
        "mov %%gs:8, %%rsp\n"
        "swapgs\n"
        
        // Return to userspace
        "sysretq\n"
        ::: "memory"
    );
}

// ============================================================================
// INITIALIZE SYSCALL SUPPORT
// ============================================================================

void syscall_init(void) {
    char msg[] = "[SYSCALL] Initializing syscall interface...\n";
    printk(0xFFFFFF00, 0x000000, msg);
    
    // 1. Enable SYSCALL/SYSRET in EFER
    uint64_t efer = rdmsr(MSR_EFER);
    efer |= EFER_SCE;
    wrmsr(MSR_EFER, efer);
    
    char msg1[] = "[SYSCALL] Enabled SCE bit in EFER\n";
    printk(0xFF00FF00, 0x000000, msg1);
    
    // 2. Set up STAR (segment selectors)
    // STAR[63:48] = Kernel CS (for SYSRET)
    // STAR[47:32] = User CS (for SYSCALL)
    // Kernel CS = 0x08 (GDT entry 1)
    // User CS = 0x18 (GDT entry 3, with RPL=3 → 0x1B)
    uint64_t star = 0;
    star |= ((uint64_t)0x08 << 32);     // Kernel CS
    star |= ((uint64_t)0x18 << 48);     // User CS (SYSRET will add 16 for user SS)
    wrmsr(MSR_STAR, star);
    
    char msg2[] = "[SYSCALL] Configured STAR (CS selectors)\n";
    printk(0xFF00FF00, 0x000000, msg2);
    
    // 3. Set up LSTAR (syscall entry point)
    uint64_t entry_addr = (uint64_t)syscall_entry;
    wrmsr(MSR_LSTAR, entry_addr);
    
    char msg3[] = "[SYSCALL] Set LSTAR to 0x%llx\n";
    printk(0xFF00FF00, 0x000000, msg3, entry_addr);
    
    // 4. Set up SFMASK (RFLAGS mask)
    // These flags will be CLEARED on syscall entry
    uint64_t sfmask = RFLAGS_IF | RFLAGS_TF | RFLAGS_DF;
    wrmsr(MSR_SFMASK, sfmask);
    
    char msg4[] = "[SYSCALL] Configured SFMASK\n";
    printk(0xFF00FF00, 0x000000, msg4);
    
    char success[] = "[SYSCALL] Syscall interface ready\n";
    printk(0xFF00FF00, 0x000000, success);
}