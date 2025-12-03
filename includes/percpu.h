
#ifndef PERCPU_H
#define PERCPU_H

#include <stdint.h>

typedef struct {
    uint64_t kernel_stack;
    uint64_t user_stack;
    uint32_t cpu_id;
    uint32_t current_tid;
    void *scratch;
} __attribute__((packed)) percpu_t;

void percpu_init(void);
percpu_t* get_percpu_data(void);
void set_kernel_stack(uint64_t stack);

#endif
