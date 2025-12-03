
#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>

typedef void (*irq_handler_t)(void);


void pic_send_eoi(int irq);

void pic_set_mask(uint8_t irq);
void pic_clear_mask(uint8_t irq);
uint8_t pic_get_mask(void);


void irq_install_handler(int irq, irq_handler_t handler);
void irq_uninstall_handler(int irq);

void irq_common_handler(int irq_num);


void pit_init(uint32_t frequency);

void timer_irq_handler(void);

void timer_handler_asm(void);


void irq_init(void);


uint64_t get_timer_ticks(void);

uint64_t get_uptime_seconds(void);

void show_timer_info(void);

void irq_init(void);

#endif
