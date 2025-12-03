
#ifndef SLEEP_H
#define SLEEP_H

#include <stdint.h>


void sleep_ticks(uint64_t ticks);

void sleep_ms(uint64_t milliseconds);

void sleep_seconds(uint32_t seconds);

void sleep_us(uint64_t microseconds);


void delay_busy_cycles(uint64_t cycles);

void delay_busy(uint64_t microseconds);


uint64_t get_uptime_ms(void);

uint64_t measure_time_ms(void (*func)(void));

void sleep_with_callback(uint32_t seconds, void (*callback)(void), uint32_t callback_interval_ms);


typedef struct {
    uint64_t start_ticks;
    uint64_t timeout_ticks;
} timeout_t;

void timeout_init(timeout_t *timeout, uint64_t milliseconds);

int timeout_expired(timeout_t *timeout);

uint64_t timeout_remaining_ms(timeout_t *timeout);


void sleep_test(void);

#endif
