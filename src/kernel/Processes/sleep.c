// sleep.c - Fixed sleep implementation

#include "sleep.h"
#include "irq.h"
#include "print.h"
#include "string_helpers.h"
#include "process.h"
#include "fg.h"

#define TIMER_FREQ 1000  // 1000 Hz = 1ms per tick

void sleep_ticks(uint64_t ticks) {
    if (ticks == 0) return;
    
    thread_t *current = get_current_thread();
    
    // No threading? Busy wait
    if (!current) {
        uint64_t start = get_timer_ticks();
        uint64_t target = start + ticks;
        
        while (get_timer_ticks() < target) {
            __asm__ volatile("hlt");
        }
        return;
    }
    
    // Calculate wake time
    uint64_t target_ticks = get_timer_ticks() + ticks;
    uint64_t wake_time_ms = (target_ticks * 1000) / TIMER_FREQ;
    
    PRINT(WHITE, BLACK, "[SLEEP TID=%u] Sleeping for %llu ticks (until %llu ms)\n",
          current->tid, ticks, wake_time_ms);
    
    // Find job for this thread
    int found_job = 0;
    for (int i = 0; i < MAX_JOBS; i++) {
        extern job_t job_table[];  // From fg.c
        job_t *job = &job_table[i];
        
        if (job->used && job->tid == current->tid) {
            job->state = JOB_SLEEPING;
            job->sleep_until = wake_time_ms;
            found_job = 1;
            PRINT(WHITE, BLACK, "[SLEEP] Set job %d to wake at %llu ms\n",
                  job->job_id, wake_time_ms);
            break;
        }
    }
    
    if (!found_job) {
        PRINT(YELLOW, BLACK, "[SLEEP] WARNING: No job for TID=%u\n", current->tid);
    }
    
    // Block thread
    thread_block(current->tid);
    
    // When we return here, we've been unblocked
    PRINT(MAGENTA, BLACK, "[SLEEP TID=%u] Woke up at %llu ms\n",
          current->tid, get_uptime_ms());
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
    if (current) {
        PRINT(WHITE, BLACK, "[SLEEP TID=%u] Sleeping %u seconds\n",
              current->tid, seconds);
    }
    
    sleep_ms(seconds * 1000);
    
    if (current) {
        PRINT(MAGENTA, BLACK, "[SLEEP TID=%u] Awake!\n", current->tid);
    }
}

void sleep_us(uint64_t microseconds) {
    if (microseconds == 0) return;
    
    uint64_t milliseconds = (microseconds + 999) / 1000;
    if (milliseconds == 0) milliseconds = 1;
    
    sleep_ms(milliseconds);
}

// Busy-wait functions (no threading)
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

// Utility functions
uint64_t get_uptime_ms(void) {
    return (get_timer_ticks() * 1000) / TIMER_FREQ;
}

uint64_t measure_time_ms(void (*func)(void)) {
    uint64_t start = get_timer_ticks();
    func();
    uint64_t end = get_timer_ticks();
    return ((end - start) * 1000) / TIMER_FREQ;
}

// Timeout helpers
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