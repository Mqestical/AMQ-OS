
#include <efi.h>
#include <efilib.h>
#include "IO.h"
#include "print.h"
#include "irq.h"
#include "process.h"
#include "fg.h"
#include "string_helpers.h"

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1
#define PIC_EOI      0x20

#define PIT_CHANNEL0 0x40
#define PIT_CHANNEL1 0x41
#define PIT_CHANNEL2 0x42
#define PIT_COMMAND  0x43

#define TIMER_FREQ 1000

volatile uint64_t timer_ticks = 0;
volatile uint64_t timer_seconds = 0;

static irq_handler_t irq_handlers[16] = {NULL};


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

        uint8_t master_mask = inb(PIC1_DATA);
        if (master_mask & 0x04) {
            master_mask &= ~0x04;
            outb(PIC1_DATA, master_mask);
            for (volatile int i = 0; i < 1000; i++);
        }
    }

    uint8_t mask = inb(port);
    mask &= ~(1 << bit);
    outb(port, mask);

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


void irq_common_handler(int irq_num) {
    if (irq_handlers[irq_num] != NULL) {
        irq_handler_t handler = irq_handlers[irq_num];
        handler();
    }

    pic_send_eoi(irq_num);
}


void timer_irq_handler(void) {
    timer_ticks++;

    update_jobs_safe();

    extern void scheduler_tick(void);
    scheduler_tick();

    outb(0x20, 0x20);
}




void pit_init(uint32_t frequency) {
    uint32_t divisor = 1193182 / frequency;

    outb(PIT_COMMAND, 0x36);
    for (volatile int i = 0; i < 1000; i++);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    for (volatile int i = 0; i < 1000; i++);
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));
    for (volatile int i = 0; i < 1000; i++);
}


void irq_init(void) {
    PRINT(WHITE, BLACK, "[IRQ] Initializing IRQ system...\n");

    for (int i = 0; i < 16; i++) {
        irq_handlers[i] = NULL;
    }

    timer_ticks = 0;
    timer_seconds = 0;

    pit_init(TIMER_FREQ);

    PRINT(WHITE, BLACK, "[IRQ] Unmasking IRQ0 (timer)...\n");
    pic_clear_mask(0);
    for (volatile int i = 0; i < 10000; i++);

    uint8_t mask = inb(0x21);
    PRINT(WHITE, BLACK, "[IRQ] PIC1 mask after unmask: 0x%x\n", mask);

    if (mask & 0x01) {
        PRINT(YELLOW, BLACK, "[WARNING] IRQ0 still masked!\n");
    } else {
        PRINT(MAGENTA, BLACK, "[OK] IRQ0 is unmasked\n");
    }

    PRINT(WHITE, BLACK, "[IRQ] Enabling interrupts...\n");
    __asm__ volatile("sti");

    PRINT(MAGENTA, BLACK, "[IRQ] IRQ system ready\n");
}

uint64_t get_uptime_seconds(void) {
    return timer_seconds;
}

void show_timer_info(void) {
    PRINT(WHITE, BLACK, "\n=== Timer Information ===\n");
    PRINT(WHITE, BLACK, "Ticks: %llu\n", timer_ticks);
    PRINT(WHITE, BLACK, "Uptime: %llu seconds\n", timer_seconds);
    PRINT(WHITE, BLACK, "Milliseconds: %llu\n", (timer_ticks * 1000) / TIMER_FREQ);
    PRINT(WHITE, BLACK, "PIC1 mask: 0x%x\n", pic_get_mask());
}

void timer_handler_c(void) {
    timer_ticks++;
    
    // Update job sleep timers
    update_jobs();
    
    // Scheduler tick (if enabled)
    scheduler_tick();
    
    // Send EOI
    outb(0x20, 0x20);
}

__attribute__((naked))
void timer_handler_asm(void) {
    __asm__ volatile(
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
        
        "call timer_handler_c\n"
        
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
        "iretq\n"
    );
}

uint64_t get_timer_ticks(void) {
    return timer_ticks;
}