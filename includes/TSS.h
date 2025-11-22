#ifndef TSS_H
#define TSS_H

#include <stdint.h>

// Task State Segment structure for x86-64
struct __attribute__((packed)) TSS64 {
    uint32_t reserved0;
    uint64_t rsp0;          // Stack pointer for ring 0
    uint64_t rsp1;          // Stack pointer for ring 1
    uint64_t rsp2;          // Stack pointer for ring 2
    uint64_t reserved1;
    uint64_t ist1;          // Interrupt Stack Table 1
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base;   // I/O map base address
};

// Function prototypes
void tss_init(void);
void tss_load(void);

#endif // TSS_H