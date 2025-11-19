void generic_handler();
void idt_set_gate(int num, uint64_t handler, uint16_t selector, uint8_t flags);
void idt_install();