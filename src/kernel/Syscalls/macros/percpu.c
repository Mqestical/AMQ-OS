
#include "percpu.h"
#include "print.h"
#include "memory.h"

static percpu_t boot_cpu_data __attribute__((aligned(64)));

#define MSR_GS_BASE     0xC0000101

static inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = value & WHITE;
    uint32_t high = value >> 32;
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

void percpu_init(void) {
    char msg[64];
    char s1[] = "[PERCPU] Initializing per-CPU data...\n";
    int i = 0;
    while (s1[i] && i < 63) { msg[i] = s1[i]; i++; }
    msg[i] = '\0';
    printk(WHITE, BLACK, msg);
    
    for (int j = 0; j < sizeof(percpu_t); j++) {
        ((uint8_t*)&boot_cpu_data)[j] = 0;
    }
    
    boot_cpu_data.cpu_id = 0;
    boot_cpu_data.current_tid = 0;
    
    extern uint64_t kernel_stack_top;
    boot_cpu_data.kernel_stack = kernel_stack_top;
    
    uint64_t percpu_addr = (uint64_t)&boot_cpu_data;
    wrmsr(MSR_GS_BASE, percpu_addr);
    
    char s2[] = "[PERCPU] Set GS base to 0x%llx\n";
    i = 0;
    while (s2[i] && i < 63) { msg[i] = s2[i]; i++; }
    msg[i] = '\0';
    printk(MAGENTA, BLACK, msg, percpu_addr);
    
    char s3[] = "[PERCPU] Kernel stack at 0x%llx\n";
    i = 0;
    while (s3[i] && i < 63) { msg[i] = s3[i]; i++; }
    msg[i] = '\0';
    printk(MAGENTA, BLACK, msg, boot_cpu_data.kernel_stack);
    
    uint64_t gs_test;
    __asm__ volatile("mov %%gs:0, %0" : "=r"(gs_test));
    
    if (gs_test == boot_cpu_data.kernel_stack) {
        char s4[] = "[PERCPU] GS base verification PASSED\n";
        i = 0;
        while (s4[i] && i < 63) { msg[i] = s4[i]; i++; }
        msg[i] = '\0';
        printk(MAGENTA, BLACK, msg);
    } else {
        char s5[] = "[PERCPU] GS base verification FAILED!\n";
        i = 0;
        while (s5[i] && i < 63) { msg[i] = s5[i]; i++; }
        msg[i] = '\0';
        printk(YELLOW, BLACK, msg);
    }
}

percpu_t* get_percpu_data(void) {
    return &boot_cpu_data;
}

void set_kernel_stack(uint64_t stack) {
    boot_cpu_data.kernel_stack = stack;
    
    char msg[64];
    char s[] = "[PERCPU] Updated kernel stack to 0x%llx\n";
    int i = 0;
    while (s[i] && i < 63) { msg[i] = s[i]; i++; }
    msg[i] = '\0';
    printk(WHITE, BLACK, msg, stack);
}