#include <efi.h>
#include <efilib.h>
#include "IO.h"
void pic_remap() {
    // Start initialization
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    
    // Set vector offsets
    outb(0x21, 0x20);  // Master PIC starts at 32
    outb(0xA1, 0x28);  // Slave PIC starts at 40
    
    // Tell PICs about each other
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    
    // Set 8086 mode
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    
    // MASK ALL interrupts initially (0xFF = all masked)
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}