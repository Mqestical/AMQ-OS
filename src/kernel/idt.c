#include <efi.h>
#include <efilib.h>
#include "in_out_b.h"

#define IDT_ENTRIES 256
#define GDT_ENTRIES 5

// GDT Entry Structure
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

// GDT Pointer
struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

// IDT Entry Structure
struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

// IDT Pointer
struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct gdt_entry gdt[GDT_ENTRIES];
struct gdt_ptr gdtp;
struct idt_entry idt[IDT_ENTRIES];
struct idt_ptr idtp;

// Set GDT entry
void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].granularity = (limit >> 16) & 0x0F;
    gdt[num].granularity |= gran & 0xF0;
    gdt[num].access = access;
}

// Install GDT
void gdt_install() {
    gdtp.limit = (sizeof(struct gdt_entry) * GDT_ENTRIES) - 1;
    gdtp.base = (uint64_t)&gdt;
    
    // Null descriptor
    gdt_set_gate(0, 0, 0, 0, 0);
    
    // Code segment (selector 0x08) - 64-bit
    // Base=0, Limit=0, Access=0x9A (present, ring 0, code, executable, readable)
    // Granularity=0x20 (64-bit mode, no other flags needed)
    gdt_set_gate(1, 0, 0, 0x9A, 0x20);
    
    // Data segment (selector 0x10) - 64-bit
    // Base=0, Limit=0, Access=0x92 (present, ring 0, data, writable)
    // Granularity=0x00
    gdt_set_gate(2, 0, 0, 0x92, 0x00);
    
    // User mode code segment (selector 0x18)
    gdt_set_gate(3, 0, 0, 0xFA, 0x20);
    
    // User mode data segment (selector 0x20)
    gdt_set_gate(4, 0, 0, 0xF2, 0x00);
    
    // Load GDT
    __asm__ volatile("lgdt %0" : : "m"(gdtp));
    
    // Reload segment registers
    __asm__ volatile(
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        
        // Far jump to reload CS
        "pushq $0x08\n"
        "lea 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        : : : "rax"
    );
}

void generic_handler(void) {
    __asm__ volatile(
        "push %rax\n"
        "movb $0x20, %al\n"
        "outb %al, $0x20\n"
        "pop %rax\n"
        "iretq"
    );
}

void keyboard_handler(void);

void idt_set_gate(int num, uint64_t handler, uint16_t selector, uint8_t flags) {
    idt[num].offset_low = handler & 0xFFFF;
    idt[num].selector = selector;
    idt[num].ist = 0;
    idt[num].type_attr = flags;
    idt[num].offset_mid = (handler >> 16) & 0xFFFF;
    idt[num].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[num].zero = 0;
}

void idt_install() {
    idtp.limit = (sizeof(struct idt_entry) * IDT_ENTRIES) - 1;
    idtp.base = (uint64_t)&idt;
    
    // Clear IDT
    for(int i = 0; i < IDT_ENTRIES; i++) {
        idt[i].offset_low = 0;
        idt[i].selector = 0;
        idt[i].ist = 0;
        idt[i].type_attr = 0;
        idt[i].offset_mid = 0;
        idt[i].offset_high = 0;
        idt[i].zero = 0;
    }
    
    // Point everything to generic handler
    for(int i = 0; i < 256; i++) {
        idt_set_gate(i, (uint64_t)generic_handler, 0x08, 0x8E);
    }
    
    // Override keyboard (IRQ1 = interrupt 33)
    idt_set_gate(33, (uint64_t)keyboard_handler, 0x08, 0x8E);
    
    __asm__ volatile("lidt %0" : : "m"(idtp));
}

__attribute__((naked))
void keyboard_handler(void) {
    __asm__ volatile(
        "push %rax\n"
        "inb $0x60, %al\n"
        "movb $0x20, %al\n"
        "outb %al, $0x20\n"
        "pop %rax\n"
        "iretq\n"
    );
}

// Usage in your main function:
// 1. gdt_install();     // Install GDT FIRST
// 2. idt_install();     // Then install IDT
// 3. ExitBootServices();
// 4. pic_remap();       // Remap PIC
// 5. __asm__ volatile("sti");  // Enable interrupts