// ============================================================================
// IRQ.h - IRQ Handler Header for AMQ OS
// ============================================================================

#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>

// IRQ handler function pointer type
typedef void (*irq_handler_t)(void);

// ============================================================================
// PIC FUNCTIONS
// ============================================================================

// Send End-Of-Interrupt to PIC
void pic_send_eoi(int irq);

// Mask/unmask IRQ lines
void pic_set_mask(uint8_t irq);
void pic_clear_mask(uint8_t irq);
uint8_t pic_get_mask(void);

// ============================================================================
// IRQ HANDLER REGISTRATION
// ============================================================================

// Install/uninstall IRQ handlers
void irq_install_handler(int irq, irq_handler_t handler);
void irq_uninstall_handler(int irq);

// Common IRQ handler (called from assembly stubs)
void irq_common_handler(int irq_num);

// ============================================================================
// TIMER (PIT) FUNCTIONS
// ============================================================================

// Initialize Programmable Interval Timer
void pit_init(uint32_t frequency);

// Timer IRQ handler
void timer_irq_handler(void);

// Assembly wrapper for timer IRQ (use this in IDT)
void timer_handler_asm(void);

// ============================================================================
// IRQ SYSTEM INITIALIZATION
// ============================================================================

// Initialize entire IRQ system
void irq_init(void);

// ============================================================================
// TIMER INFORMATION
// ============================================================================

// Get current timer ticks
uint64_t get_timer_ticks(void);

// Get uptime in seconds
uint64_t get_uptime_seconds(void);

// Display timer information
void show_timer_info(void);

void irq_init(void);

#endif // IRQ_H