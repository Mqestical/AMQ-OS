
#ifndef PERCPU_H
#define PERCPU_H

#include <stdint.h>

// Per-CPU data structure
typedef struct {
    uint64_t kernel_stack;      // Offset 0: Kernel stack pointer
    uint64_t user_stack;        // Offset 8: User stack pointer (saved on syscall)
    uint32_t cpu_id;            // Offset 16: CPU ID
    uint32_t current_tid;       // Offset 20: Current thread ID
    void *scratch;              // Offset 24: Scratch pointer
} __attribute__((packed)) percpu_t;

void percpu_init(void);
percpu_t* get_percpu_data(void);
void set_kernel_stack(uint64_t stack);

#endif // PERCPU_H