#include <efi.h>
#include <efilib.h>
#include "IO.h"
#include "serial.h"
#include "print.h"

#define IDT_ENTRIES 256
#define GDT_ENTRIES 5
#define INPUT_BUFFER_SIZE 256

// GDT Entry Structure
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct gdt_entry gdt[GDT_ENTRIES];
struct gdt_ptr gdtp;
struct idt_entry idt[IDT_ENTRIES];
struct idt_ptr idtp;

void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].granularity = (limit >> 16) & 0x0F;
    gdt[num].granularity |= gran & 0xF0;
    gdt[num].access = access;
}

void gdt_install() {
    gdtp.limit = (sizeof(struct gdt_entry) * GDT_ENTRIES) - 1;
    gdtp.base = (uint64_t)&gdt;
    
    gdt_set_gate(0, 0, 0, 0, 0);
    gdt_set_gate(1, 0, 0, 0x9A, 0x20);
    gdt_set_gate(2, 0, 0, 0x92, 0x00);
    gdt_set_gate(3, 0, 0, 0xFA, 0x20);
    gdt_set_gate(4, 0, 0, 0xF2, 0x00);
    
    __asm__ volatile("lgdt %0" : : "m"(gdtp));
    
    __asm__ volatile(
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
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

void generic_handler_tracked(void);
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
    
    for(int i = 0; i < IDT_ENTRIES; i++) {
        idt[i].offset_low = 0;
        idt[i].selector = 0;
        idt[i].ist = 0;
        idt[i].type_attr = 0;
        idt[i].offset_mid = 0;
        idt[i].offset_high = 0;
        idt[i].zero = 0;
    }
    
    for(int i = 0; i < 256; i++) {
        idt_set_gate(i, (uint64_t)generic_handler, 0x08, 0x8E);
    }
    
    idt_set_gate(32, (uint64_t)generic_handler_tracked, 0x08, 0x8E);
    idt_set_gate(33, (uint64_t)keyboard_handler, 0x08, 0x8E);

    __asm__ volatile("lidt %0" : : "m"(idtp));
}

#include "keyboard.h"

// Keyboard scancode buffer
volatile uint8_t scancode_buffer[256];
volatile uint8_t scancode_read_pos = 0;
volatile uint8_t scancode_write_pos = 0;

// Debug counters
volatile uint32_t interrupt_counter = 0;
volatile uint8_t last_scancode = 0;
volatile uint8_t last_status = 0;
volatile uint32_t interrupt_vector = 0;

// INPUT BUFFER - stores user's typed line
char input_buffer[INPUT_BUFFER_SIZE];
volatile int input_pos = 0;
volatile int input_ready = 0;  // Set to 1 when user presses Enter

void generic_handler_tracked(void) {
    __asm__ volatile(
        "push %rax\n"
        "push %rbx\n"
        "lea interrupt_vector(%rip), %rbx\n"
        "incl (%rbx)\n"
        "movb $0x20, %al\n"
        "outb %al, $0x20\n"
        "pop %rbx\n"
        "pop %rax\n"
        "iretq"
    );
}

__attribute__((naked))
void keyboard_handler(void) {
    __asm__ volatile(
        "push %rax\n"
        "push %rbx\n"
        "push %rcx\n"
        
        "lea interrupt_counter(%rip), %rbx\n"
        "incl (%rbx)\n"
        
        "inb $0x60, %al\n"
        
        "lea last_scancode(%rip), %rbx\n"
        "movb %al, (%rbx)\n"
        
        "lea scancode_buffer(%rip), %rbx\n"
        "lea scancode_write_pos(%rip), %rcx\n"
        "movzbl (%rcx), %ecx\n"
        "movb %al, (%rbx,%rcx,1)\n"
        
        "lea scancode_write_pos(%rip), %rbx\n"
        "incb (%rbx)\n"
        
        "movb $0x20, %al\n"
        "outb %al, $0x20\n"
        
        "pop %rcx\n"
        "pop %rbx\n"
        "pop %rax\n"
        "iretq\n"
    );
}

// Scancode to ASCII conversion
static char scancode_to_ascii(uint8_t scancode, int shifted) {
    if (!shifted) {
        switch(scancode) {
            case 0x02: return '1';
            case 0x03: return '2';
            case 0x04: return '3';
            case 0x05: return '4';
            case 0x06: return '5';
            case 0x07: return '6';
            case 0x08: return '7';
            case 0x09: return '8';
            case 0x0A: return '9';
            case 0x0B: return '0';
            case 0x0C: return '-';
            case 0x0D: return '=';
            case 0x0E: return '\b';  // Backspace
            case 0x1C: return '\n';  // Enter
            case 0x39: return ' ';   // Space
            case 0x1E: return 'a';
            case 0x1F: return 's';
            case 0x20: return 'd';
            case 0x21: return 'f';
            case 0x22: return 'g';
            case 0x23: return 'h';
            case 0x24: return 'j';
            case 0x25: return 'k';
            case 0x26: return 'l';
            case 0x10: return 'q';
            case 0x11: return 'w';
            case 0x12: return 'e';
            case 0x13: return 'r';
            case 0x14: return 't';
            case 0x15: return 'y';
            case 0x16: return 'u';
            case 0x17: return 'i';
            case 0x18: return 'o';
            case 0x19: return 'p';
            case 0x2C: return 'z';
            case 0x2D: return 'x';
            case 0x2E: return 'c';
            case 0x2F: return 'v';
            case 0x30: return 'b';
            case 0x31: return 'n';
            case 0x32: return 'm';
            case 0x33: return ',';
            case 0x34: return '.';
            case 0x35: return '/';
            default: return 0;
        }
    } else {
        // Shifted characters
        switch(scancode) {
            case 0x02: return '!';
            case 0x03: return '@';
            case 0x04: return '#';
            case 0x05: return '$';
            case 0x06: return '%';
            case 0x07: return '^';
            case 0x08: return '&';
            case 0x09: return '*';
            case 0x0A: return '(';
            case 0x0B: return ')';
            case 0x0C: return '_';
            case 0x0D: return '+';
            case 0x1E: return 'A';
            case 0x1F: return 'S';
            case 0x20: return 'D';
            case 0x21: return 'F';
            case 0x22: return 'G';
            case 0x23: return 'H';
            case 0x24: return 'J';
            case 0x25: return 'K';
            case 0x26: return 'L';
            case 0x10: return 'Q';
            case 0x11: return 'W';
            case 0x12: return 'E';
            case 0x13: return 'R';
            case 0x14: return 'T';
            case 0x15: return 'Y';
            case 0x16: return 'U';
            case 0x17: return 'I';
            case 0x18: return 'O';
            case 0x19: return 'P';
            case 0x2C: return 'Z';
            case 0x2D: return 'X';
            case 0x2E: return 'C';
            case 0x2F: return 'V';
            case 0x30: return 'B';
            case 0x31: return 'N';
            case 0x32: return 'M';
            case 0x33: return '<';
            case 0x34: return '>';
            case 0x35: return '?';
            default: return 0;
        }
    }
}

static int shift_pressed = 0;

// Handle backspace by erasing last character on screen
void handle_backspace(void) {
    if (input_pos > 0) {
        input_pos--;
        input_buffer[input_pos] = '\0';
        
        // Move cursor back and erase character
        if (cursor.x >= 8) {
            cursor.x -= 8;
        } else if (cursor.y >= 8) {
            // Wrap to previous line
            cursor.y -= 8;
            cursor.x = (fb.width - 8);
        }
        
        // Draw space to erase the character
        draw_char(cursor.x, cursor.y, ' ', cursor.fg_color, cursor.bg_color);
    }
}

// Process keyboard buffer with input storage
void process_keyboard_buffer(void) {
    while (scancode_read_pos != scancode_write_pos) {
        uint8_t scancode = scancode_buffer[scancode_read_pos++];
        
        // Handle shift keys
        if (scancode == 0x2A || scancode == 0x36) {
            shift_pressed = 1;
            continue;
        }
        if (scancode == 0xAA || scancode == 0xB6) {
            shift_pressed = 0;
            continue;
        }
        
        // Ignore key releases
        if (scancode & 0x80) continue;
        
        // Convert to ASCII
        char ascii = scancode_to_ascii(scancode, shift_pressed);
        
        if (ascii) {
            // Handle special keys
            if (ascii == '\b') {
                // BACKSPACE
                handle_backspace();
                serial_write_byte(COM1, '\b');
            } else if (ascii == '\n') {
                // ENTER - finalize input
                input_buffer[input_pos] = '\0';
                input_ready = 1;  // Signal that input is ready
                
                // Echo newline to screen and serial
                printc('\n');
                serial_write_byte(COM1, '\r');
                serial_write_byte(COM1, '\n');
            } else {
                // Regular character
                if (input_pos < INPUT_BUFFER_SIZE - 1) {
                    input_buffer[input_pos++] = ascii;
                    printc(ascii);
                    serial_write_byte(COM1, ascii);
                }
            }
        }
    }
}

// Function to get user input (blocking)
char* get_input_line(void) {
    // Wait for Enter key
    input_ready = 0;
    input_pos = 0;
    
    while (!input_ready) {
        process_keyboard_buffer();
        for (volatile int i = 0; i < 1000; i++);
    }
    
    return input_buffer;
}

// Function to check if input is ready (non-blocking)
int input_available(void) {
    return input_ready;
}

// Get the input and reset for next line
char* get_input_and_reset(void) {
    if (input_ready) {
        input_ready = 0;
        input_pos = 0;
        return input_buffer;
    }
    return NULL;
}