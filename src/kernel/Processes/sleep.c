
#include <efi.h>
#include <efilib.h>
#include "sleep.h"
#include "irq.h"
#include "print.h"
#include "process.h"
#include "fg.h"
#include "string_helpers.h"
#define TIMER_FREQ 1000

void sleep_ticks(uint64_t ticks) {
    if (ticks == 0) return;
    
    thread_t *current = get_current_thread();
    
    if (!current) {
        uint64_t start = get_timer_ticks();
        uint64_t target = start + ticks;
        
        while (get_timer_ticks() < target) {
            __asm__ volatile("hlt");
        }
        return;
    }
    
    uint64_t start = get_timer_ticks();
    uint64_t target = start + ticks;
    uint64_t wake_time_ms = (target * 1000) / TIMER_FREQ;
    
    int job_found = 0;
    for (int i = 0; i < MAX_JOBS; i++) {
        extern job_t fg_table[MAX_JOBS];
        if (fg_table[i].used && fg_table[i].tid == current->tid) {
            fg_table[i].state = JOB_SLEEPING;
            fg_table[i].sleep_until = wake_time_ms;
            job_found = 1;
            
            PRINT(WHITE, BLACK, "[SLEEP TID=%u JOB=%d] Sleeping until %llu ms\n",
                  current->tid, fg_table[i].job_id, wake_time_ms);
            break;
        }
    }
    
    if (!job_found) {
        PRINT(WHITE, BLACK, "[SLEEP TID=%u] WARNING: No job found for this thread\n",
              current->tid);
    }
    
    thread_block(current->tid);
    
    while (get_timer_ticks() < target) {
        __asm__ volatile("hlt");
    }
    
    PRINT(MAGENTA, BLACK, "[SLEEP TID=%u] Woke up at %llu ticks\n",
          current->tid, get_timer_ticks());
}

void sleep_ms(uint64_t milliseconds) {
    if (milliseconds == 0) return;
    uint64_t ticks = (milliseconds * TIMER_FREQ) / 1000;
    if (ticks == 0) ticks = 1;
    sleep_ticks(ticks);
}

void sleep_seconds(uint32_t seconds) {
    if (seconds == 0) return;
    
    thread_t *current = get_current_thread();
    
    uint64_t start_ticks = get_timer_ticks();
    uint64_t target_ticks = start_ticks + (seconds * TIMER_FREQ);
    
    if (current) {
        PRINT(WHITE, BLACK, "[SLEEP TID=%u] Sleeping for %u seconds (%llu ticks)\n",
              current->tid, seconds, target_ticks - start_ticks);
    }
    
    sleep_ticks(seconds * TIMER_FREQ);
    
    uint64_t end_ticks = get_timer_ticks();
    uint64_t elapsed = end_ticks - start_ticks;
    
    if (current) {
        PRINT(MAGENTA, BLACK, "[SLEEP TID=%u] Awake! Slept for %llu ticks (%llu ms)\n",
              current->tid, elapsed, (elapsed * 1000) / TIMER_FREQ);
    }
}

void sleep_us(uint64_t microseconds) {
    if (microseconds == 0) return;
    uint64_t milliseconds = (microseconds + 999) / 1000;
    if (milliseconds == 0) milliseconds = 1;
    sleep_ms(milliseconds);
}


void delay_busy_cycles(uint64_t cycles) {
    volatile uint64_t count = cycles;
    while (count--) {
        __asm__ volatile("nop");
    }
}

void delay_busy(uint64_t microseconds) {
    volatile uint64_t count = microseconds * 1000;
    while (count--) {
        __asm__ volatile("nop");
    }
}


uint64_t get_uptime_ms(void) {
    return (get_timer_ticks() * 1000) / TIMER_FREQ;
}

uint64_t measure_time_ms(void (*func)(void)) {
    uint64_t start = get_timer_ticks();
    func();
    uint64_t end = get_timer_ticks();
    return ((end - start) * 1000) / TIMER_FREQ;
}

void sleep_with_callback(uint32_t seconds, void (*callback)(void), uint32_t callback_interval_ms) {
    uint64_t start = get_timer_ticks();
    uint64_t target = start + (seconds * TIMER_FREQ);
    uint64_t next_callback = start + (callback_interval_ms * TIMER_FREQ / 1000);
    
    while (get_timer_ticks() < target) {
        uint64_t current = get_timer_ticks();
        
        if (callback && current >= next_callback) {
            callback();
            next_callback = current + (callback_interval_ms * TIMER_FREQ / 1000);
        }
        
        __asm__ volatile("hlt");
    }
}


void timeout_init(timeout_t *timeout, uint64_t milliseconds) {
    timeout->start_ticks = get_timer_ticks();
    timeout->timeout_ticks = (milliseconds * TIMER_FREQ) / 1000;
}

int timeout_expired(timeout_t *timeout) {
    uint64_t elapsed = get_timer_ticks() - timeout->start_ticks;
    return elapsed >= timeout->timeout_ticks;
}

uint64_t timeout_remaining_ms(timeout_t *timeout) {
    uint64_t elapsed = get_timer_ticks() - timeout->start_ticks;
    
    if (elapsed >= timeout->timeout_ticks) {
        return 0;
    }
    
    uint64_t remaining_ticks = timeout->timeout_ticks - elapsed;
    return (remaining_ticks * 1000) / TIMER_FREQ;
}


void sleep_test(void) {
    PRINT(WHITE, BLACK, "\n=== Sleep Function Tests ===\n");
    
    PRINT(WHITE, BLACK, "Test 1: sleep_seconds(1)...\n");
    uint64_t start1 = get_timer_ticks();
    sleep_seconds(1);
    uint64_t end1 = get_timer_ticks();
    PRINT(MAGENTA, BLACK, "  Result: %llu ticks elapsed\n", end1 - start1);
    
    PRINT(WHITE, BLACK, "Test 2: sleep_ms(500)...\n");
    uint64_t start2 = get_timer_ticks();
    sleep_ms(500);
    uint64_t end2 = get_timer_ticks();
    PRINT(MAGENTA, BLACK, "  Result: %llu ticks elapsed\n", end2 - start2);
    
    PRINT(MAGENTA, BLACK, "\nSleep tests completed!\n");
}
