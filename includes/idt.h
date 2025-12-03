#ifndef IDT_H
#define IDT_H

#include <stdint.h>

void gdt_install(void);
void idt_install(void);
void idt_set_gate(int num, uint64_t handler, uint16_t selector, uint8_t flags);

#endif
