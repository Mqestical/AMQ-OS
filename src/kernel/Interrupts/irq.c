// ============================================================================
// IRQ.c - MINIMAL SAFE VERSION
// ============================================================================

#include <efi.h>
#include <efilib.h>
#include "IO.h"
#include "print.h"
#include "irq.h"
#include "process.h"
#include "fg.h"

// PIC ports
#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1
#define PIC_EOI      0x20

// PIT (Programmable Interval Timer) ports
#define PIT_CHANNEL0 0x40
#define PIT_CHANNEL1 0x41
#define PIT_CHANNEL2 0x42
#define PIT_COMMAND  0x43

// Timer frequency (ticks per second)
#define TIMER_FREQ 1000  // 1000 Hz = 1ms per tick

// Global timer tick counter (incremented by IRQ0)
volatile uint64_t timer_ticks = 0;
volatile uint64_t timer_seconds = 0;

// IRQ handler table (16 IRQs total)
static irq_handler_t irq_handlers[16] = {NULL};

// ============================================================================
// PIC FUNCTIONS
// ============================================================================

void pic_send_eoi(int irq) {
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}

void pic_clear_mask(uint8_t irq) {
    if (irq >= 16) return;
    
    uint16_t port;
    uint8_t bit;
    
    if (irq < 8) {
        port = PIC1_DATA;
        bit = irq;
    } else {
        port = PIC2_DATA;
        bit = irq - 8;
        
        // Ensure IRQ2 (cascade) is unmasked on master
        uint8_t master_mask = inb(PIC1_DATA);
        if (master_mask & 0x04) {
            master_mask &= ~0x04;
            outb(PIC1_DATA, master_mask);
            for (volatile int i = 0; i < 1000; i++);
        }
    }
    
    // Read, modify, write
    uint8_t mask = inb(port);
    mask &= ~(1 << bit);
    outb(port, mask);
    
    // I/O delay
    for (volatile int i = 0; i < 1000; i++);
}

void pic_set_mask(uint8_t irq) {
    uint16_t port;
    uint8_t value;
    
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    
    value = inb(port) | (1 << irq);
    outb(port, value);
}

uint8_t pic_get_mask(void) {
    return inb(PIC1_DATA);
}

// ============================================================================
// IRQ HANDLER REGISTRATION
// ============================================================================

void irq_install_handler(int irq, irq_handler_t handler) {
    if (irq >= 0 && irq < 16) {
        irq_handlers[irq] = handler;
    }
}

void irq_uninstall_handler(int irq) {
    if (irq >= 0 && irq < 16) {
        irq_handlers[irq] = NULL;
    }
}

// ============================================================================
// COMMON IRQ HANDLER (called from assembly stubs)
// ============================================================================

void irq_common_handler(int irq_num) {
    if (irq_handlers[irq_num] != NULL) {
        irq_handler_t handler = irq_handlers[irq_num];
        handler();
    }
    
    pic_send_eoi(irq_num);
}

// ============================================================================
// TIMER IRQ HANDLER (IRQ0) - ULTRA MINIMAL VERSION
// ============================================================================

void timer_irq_handler(void) {
    // Just increment counter - NOTHING ELSE
    timer_ticks++;
    
    // Update seconds counter
    if (timer_ticks % TIMER_FREQ == 0) {
        timer_seconds++;
    }
    
    // ONLY call update_jobs if we're past early boot
    // This is checked every 10ms (10 ticks at 1000Hz)
    if (timer_ticks % 10 == 0) {
        // SAFETY: Only call if jobs are initialized
        extern void update_jobs_safe(void);
        update_jobs_safe();
    }
    
    // Send EOI LAST
    pic_send_eoi(0);
}

// ============================================================================
// TIMER HANDLER ASSEMBLY WRAPPER
// ============================================================================

__attribute__((naked))
void timer_handler_asm(void) {
    __asm__ volatile(
        // Save ALL registers
        "push %rax\n"
        "push %rbx\n"
        "push %rcx\n"
        "push %rdx\n"
        "push %rsi\n"
        "push %rdi\n"
        "push %rbp\n"
        "push %r8\n"
        "push %r9\n"
        "push %r10\n"
        "push %r11\n"
        "push %r12\n"
        "push %r13\n"
        "push %r14\n"
        "push %r15\n"
        
        // Save original RSP
        "mov %rsp, %rbp\n"
        
        // CRITICAL: Align stack to 16 bytes
        // System V ABI requires RSP to be 16-byte aligned before 'call'
        "and $-16, %rsp\n"
        
        // Subtract 8 to account for the 'call' instruction push
        // This ensures that when we enter the function, RSP is 16-byte aligned
        "sub $8, %rsp\n"
        
        // Call the C handler
        "call timer_irq_handler\n"
        
        // Restore original stack pointer
        "mov %rbp, %rsp\n"
        
        // Restore registers in reverse order
        "pop %r15\n"
        "pop %r14\n"
        "pop %r13\n"
        "pop %r12\n"
        "pop %r11\n"
        "pop %r10\n"
        "pop %r9\n"
        "pop %r8\n"
        "pop %rbp\n"
        "pop %rdi\n"
        "pop %rsi\n"
        "pop %rdx\n"
        "pop %rcx\n"
        "pop %rbx\n"
        "pop %rax\n"
        
        // Return from interrupt
        "iretq\n"
    );
}

// ============================================================================
// PIT INITIALIZATION
// ============================================================================

void pit_init(uint32_t frequency) {
    uint32_t divisor = 1193182 / frequency;
    
    // Command byte: 0x36 = Channel 0, lobyte/hibyte, square wave, binary
    outb(PIT_COMMAND, 0x36);
    
    // Delay
    for (volatile int i = 0; i < 1000; i++);
    
    // Send divisor (low byte, then high byte)
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    for (volatile int i = 0; i < 1000; i++);
    
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));
    for (volatile int i = 0; i < 1000; i++);
}

// ============================================================================
// IRQ SYSTEM INITIALIZATION
// ============================================================================

void irq_init(void) {
    char msg[] = "[IRQ] Initializing IRQ system...\n";
    printk(0xFFFFFF00, 0x000000, msg);
    
    // Clear handler table
    for (int i = 0; i < 16; i++) {
        irq_handlers[i] = NULL;
    }
    
    // Reset tick counters
    timer_ticks = 0;
    timer_seconds = 0;
    
    // Initialize PIT at 1000 Hz (1ms resolution)
    pit_init(TIMER_FREQ);
    
    char msg2[] = "[IRQ] Unmasking IRQ0 (timer)...\n";
    printk(0xFFFFFF00, 0x000000, msg2);
    
    // Unmask IRQ0 at the PIC
    pic_clear_mask(0);
    
    // Small delay
    for (volatile int i = 0; i < 10000; i++);
    
    // Verify mask
    uint8_t mask = inb(0x21);
    char mask_msg[] = "[IRQ] PIC1 mask after unmask: 0x%x\n";
    printk(0xFFFFFF00, 0x000000, mask_msg, mask);
    
    if (mask & 0x01) {
        char warn[] = "[WARNING] IRQ0 still masked!\n";
        printk(0xFFFF0000, 0x000000, warn);
    } else {
        char ok[] = "[OK] IRQ0 is unmasked\n";
        printk(0xFF00FF00, 0x000000, ok);
    }
    
    char msg3[] = "[IRQ] Enabling interrupts...\n";
    printk(0xFFFFFF00, 0x000000, msg3);
    
    // Enable interrupts
    __asm__ volatile("sti");
    
    char msg4[] = "[IRQ] IRQ system ready\n";
    printk(0xFF00FF00, 0x000000, msg4);
}

// ============================================================================
// TIMER INFO
// ============================================================================

uint64_t get_timer_ticks(void) {
    return timer_ticks;
}

uint64_t get_uptime_seconds(void) {
    return timer_seconds;
}

void show_timer_info(void) {
    char msg[] = "\n=== Timer Information ===\n";
    printk(0xFFFFFFFF, 0x000000, msg);
    
    char msg1[] = "Ticks: %llu\n";
    printk(0xFFFFFFFF, 0x000000, msg1, timer_ticks);
    
    char msg2[] = "Uptime: %llu seconds\n";
    printk(0xFFFFFFFF, 0x000000, msg2, timer_seconds);
    
    uint64_t ms = (timer_ticks * 1000) / TIMER_FREQ;
    char msg3[] = "Milliseconds: %llu\n";
    printk(0xFFFFFFFF, 0x000000, msg3, ms);
    
    uint8_t mask = pic_get_mask();
    char msg4[] = "PIC1 mask: 0x%x\n";
    printk(0xFFFFFFFF, 0x000000, msg4, mask);
}