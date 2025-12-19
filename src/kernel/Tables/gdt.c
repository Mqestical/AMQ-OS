#include "gdt.h"

#define GDT_ENTRIES 7  // Null, KernelCode, KernelData, UserCode, UserData, TSS(2 entries in 64-bit)

static struct gdt_entry gdt[GDT_ENTRIES];
static struct gdt_ptr gdtp;

static void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].granularity = (limit >> 16) & 0x0F;
    gdt[num].granularity |= gran & 0xF0;
    gdt[num].access = access;
}

static void gdt_load_asm(void) {
    gdtp.limit = (sizeof(struct gdt_entry) * GDT_ENTRIES) - 1;
    gdtp.base = (uint64_t)&gdt;
    
    __asm__ volatile("lgdt %0" : : "m"(gdtp));
}

static void gdt_reload_segments(void) {
    __asm__ volatile(
        "mov $0x10, %%ax\n"        // Load data segment selector
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        "pushq $0x08\n"            // Push code segment selector
        "lea 1f(%%rip), %%rax\n"   // Load address of next instruction
        "pushq %%rax\n"
        "lretq\n"                  // Far return to reload CS
        "1:\n"
        : : : "rax"
    );
}

void gdt_init(void) {
    // Clear the GDT
    for (int i = 0; i < GDT_ENTRIES; i++) {
        gdt[i].limit_low = 0;
        gdt[i].base_low = 0;
        gdt[i].base_middle = 0;
        gdt[i].access = 0;
        gdt[i].granularity = 0;
        gdt[i].base_high = 0;
    }

    // Entry 0: Null descriptor (required)
    gdt_set_gate(0, 0, 0, 0, 0);

    // Entry 1: Kernel Code (64-bit)
    // Access: 0x9A = Present, Ring 0, Code, Executable, Readable
    // Gran: 0x20 = Long mode (bit 5)
    gdt_set_gate(1, 0, 0, 0x9A, 0x20);

    // Entry 2: Kernel Data
    // Access: 0x92 = Present, Ring 0, Data, Writable
    gdt_set_gate(2, 0, 0, 0x92, 0x00);

    // Entry 3: User Code (64-bit)
    // Access: 0xFA = Present, Ring 3, Code, Executable, Readable
    gdt_set_gate(3, 0, 0, 0xFA, 0x20);

    // Entry 4: User Data
    // Access: 0xF2 = Present, Ring 3, Data, Writable
    gdt_set_gate(4, 0, 0, 0xF2, 0x00);

    // Entry 5-6: TSS will be set later via gdt_set_tss()
    
    gdt_load_asm();
    gdt_reload_segments();
}

void gdt_set_tss(struct TSS64 *tss_ptr) {
    uint64_t base = (uint64_t)tss_ptr;
    uint32_t limit = sizeof(struct TSS64) - 1;

    // TSS descriptor in 64-bit mode takes 2 GDT entries (16 bytes)
    // First entry (entry 5)
    gdt[5].limit_low    = limit & 0xFFFF;
    gdt[5].base_low     = base & 0xFFFF;
    gdt[5].base_middle  = (base >> 16) & 0xFF;
    gdt[5].access       = 0x89;  // Present, Ring 0, TSS Available
    gdt[5].granularity  = (limit >> 16) & 0x0F;
    gdt[5].base_high    = (base >> 24) & 0xFF;

    // Second entry (entry 6) - upper 32 bits of base address
    gdt[6].limit_low    = (base >> 32) & 0xFFFF;
    gdt[6].base_low     = (base >> 48) & 0xFFFF;
    gdt[6].base_middle  = 0;
    gdt[6].access       = 0;
    gdt[6].granularity  = 0;
    gdt[6].base_high    = 0;

    // Reload GDT to include TSS
    gdt_load_asm();
}

void tss_load(void) {
    __asm__ volatile("ltr %0" : : "r"((uint16_t)TSS_SEL));
}

struct gdt_entry* get_gdt(void) {
    return gdt;
}