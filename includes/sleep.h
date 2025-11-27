// ============================================================================
// sleep.h - Sleep Functions Header for AMQ OS
// ============================================================================

#ifndef SLEEP_H
#define SLEEP_H

#include <stdint.h>

// ============================================================================
// SLEEP FUNCTIONS (using timer interrupts)
// ============================================================================

// Sleep for specified number of timer ticks
void sleep_ticks(uint64_t ticks);

// Sleep for milliseconds (1ms = 1 tick at 1000 Hz)
void sleep_ms(uint64_t milliseconds);

// Sleep for seconds
void sleep_seconds(uint32_t seconds);

// Sleep for microseconds (best-effort, uses milliseconds)
void sleep_us(uint64_t microseconds);

// ============================================================================
// BUSY-WAIT DELAYS (for use when interrupts are disabled)
// ============================================================================

// Busy-wait for CPU cycles
void delay_busy_cycles(uint64_t cycles);

// Busy-wait for microseconds (VERY approximate, CPU-dependent)
void delay_busy(uint64_t microseconds);

// ============================================================================
// TIMING UTILITIES
// ============================================================================

// Get uptime in milliseconds
uint64_t get_uptime_ms(void);

// Measure execution time of a function in milliseconds
uint64_t measure_time_ms(void (*func)(void));

// Sleep with periodic callback
void sleep_with_callback(uint32_t seconds, void (*callback)(void), uint32_t callback_interval_ms);

// ============================================================================
// TIMEOUT UTILITIES
// ============================================================================

typedef struct {
    uint64_t start_ticks;
    uint64_t timeout_ticks;
} timeout_t;

// Initialize a timeout
void timeout_init(timeout_t *timeout, uint64_t milliseconds);

// Check if timeout has expired
int timeout_expired(timeout_t *timeout);

// Get remaining time in milliseconds
uint64_t timeout_remaining_ms(timeout_t *timeout);

// ============================================================================
// TEST FUNCTIONS
// ============================================================================

// Run sleep function tests
void sleep_test(void);

#endif // SLEEP_H