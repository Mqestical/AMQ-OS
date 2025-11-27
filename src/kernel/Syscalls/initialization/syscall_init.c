#include "string_helpers.h"
#include "syscall.h"
#include "print.h"

extern uint64_t syscall_handler(uint64_t syscall_num, 
                                uint64_t arg1, uint64_t arg2, 
                                uint64_t arg3, uint64_t arg4,
                                uint64_t arg5, uint64_t arg6);

__attribute__((naked))
void syscall_entry(void) {
    __asm__ volatile(
        "swapgs\n"
        "mov %%rsp, %%gs:8\n"
        "mov %%gs:0, %%rsp\n"
        "push %%r11\n"
        "push %%rcx\n"
        "push %%rbp\n"
        "push %%rbx\n"
        "push %%r15\n"
        "push %%r14\n"
        "push %%r13\n"
        "push %%r12\n"
        "mov %%rax, %%rdi\n"
        "mov %%r10, %%rcx\n"
        "and $-16, %%rsp\n"
        "sti\n"
        "call syscall_handler\n"
        "cli\n"
        "pop %%r12\n"
        "pop %%r13\n"
        "pop %%r14\n"
        "pop %%r15\n"
        "pop %%rbx\n"
        "pop %%rbp\n"
        "pop %%rcx\n"
        "pop %%r11\n"
        "mov %%gs:8, %%rsp\n"
        "swapgs\n"
        "sysretq\n"
        ::: "memory"
    );
}

void syscall_init(void) {
    PRINT(0xFFFFFF00, 0x000000, "[SYSCALL] Initializing syscall interface...\n");
    uint64_t efer = rdmsr(MSR_EFER);
    efer |= EFER_SCE;
    wrmsr(MSR_EFER, efer);
    PRINT(0xFF00FF00, 0x000000, "[SYSCALL] Enabled SCE bit in EFER\n");

    uint64_t star = 0;
    star |= ((uint64_t)0x08 << 32);
    star |= ((uint64_t)0x18 << 48);
    wrmsr(MSR_STAR, star);
    PRINT(0xFF00FF00, 0x000000, "[SYSCALL] Configured STAR (CS selectors)\n");

    uint64_t entry_addr = (uint64_t)syscall_entry;
    wrmsr(MSR_LSTAR, entry_addr);
    PRINT(0xFF00FF00, 0x000000, "[SYSCALL] Set LSTAR to 0x%llx\n", entry_addr);

    uint64_t sfmask = RFLAGS_IF | RFLAGS_TF | RFLAGS_DF;
    wrmsr(MSR_SFMASK, sfmask);
    PRINT(0xFF00FF00, 0x000000, "[SYSCALL] Configured SFMASK\n");

    PRINT(0xFF00FF00, 0x000000, "[SYSCALL] Syscall interface ready\n");
}
