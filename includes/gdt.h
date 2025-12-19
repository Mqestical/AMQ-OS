#ifndef GDT_H
#define GDT_H

#include <stdint.h>

struct __attribute__((packed)) gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
};

struct __attribute__((packed)) gdt_ptr {
    uint16_t limit;
    uint64_t base;
};

// TSS structure for 64-bit
struct __attribute__((packed)) TSS64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];  // IST 1-7
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base;
};

// GDT indices
#define GDT_NULL_ENTRY     0
#define GDT_KERNEL_CODE    1
#define GDT_KERNEL_DATA    2
#define GDT_USER_CODE      3
#define GDT_USER_DATA      4
#define GDT_TSS_ENTRY      5

// Selector values
#define KERNEL_CS  (GDT_KERNEL_CODE * 8)
#define KERNEL_DS  (GDT_KERNEL_DATA * 8)
#define USER_CS    (GDT_USER_CODE * 8)
#define USER_DS    (GDT_USER_DATA * 8)
#define TSS_SEL    (GDT_TSS_ENTRY * 8)

void gdt_init(void);
void gdt_set_tss(struct TSS64 *tss_ptr);
void tss_load(void);
struct gdt_entry* get_gdt(void);  // For debugging

#endif // GDT_H