#include "TSS.h"

// Simple GDT entry for TSS
struct __attribute__((packed)) GDTEntry64 {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
};

// Minimal GDT pointer
struct __attribute__((packed)) GDTPtr {
    uint16_t limit;
    uint64_t base;
};

// Declare GDT and TSS
#define GDT_SIZE 10
static struct GDTEntry64 gdt[GDT_SIZE];
static struct GDTPtr gdt_ptr;
static struct TSS64 tss;

// Helper to set a TSS descriptor in GDT
static void set_tss_entry(int index, struct TSS64 *tss_ptr) {
    uint64_t base = (uint64_t)tss_ptr;
    uint32_t limit = sizeof(struct TSS64) - 1;

    gdt[index].limit_low    = limit & 0xFFFF;
    gdt[index].base_low     = base & 0xFFFF;
    gdt[index].base_middle  = (base >> 16) & 0xFF;
    gdt[index].access       = 0x89; // present + type=64-bit TSS
    gdt[index].granularity  = (limit >> 16) & 0x0F;
    gdt[index].base_high    = (base >> 24) & 0xFF;
}

// Load GDT
static inline void gdt_load(void) {
    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base  = (uint64_t)&gdt;
    __asm__ volatile("lgdt %0" : : "m"(gdt_ptr));
}

// Load TSS
static inline void tss_load_sel(uint16_t sel) {
    __asm__ volatile("ltr %0" : : "r"(sel));
}

// Public init function
void tss_init(void) {
    // Zero TSS
    for (int i = 0; i < sizeof(tss)/8; i++) ((uint64_t*)&tss)[i] = 0;

    // I/O bitmap: all ports allowed
    tss.io_map_base = sizeof(struct TSS64);

    // Clear GDT
    for (int i = 0; i < GDT_SIZE; i++) {
        ((uint64_t*)&gdt[i])[0] = 0;
        ((uint64_t*)&gdt[i])[1] = 0;
    }

    // Create TSS descriptor at GDT index 5
    set_tss_entry(5, &tss);

    // Load GDT
    gdt_load();

    // Load TSS (selector = index 5 * 8)
    tss_load_sel(5 * 8);
}

// Optional: separate call if you want to reload later
void tss_load(void) {
    tss_load_sel(5 * 8);
}
