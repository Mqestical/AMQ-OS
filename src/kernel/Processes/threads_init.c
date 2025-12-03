#include "process.h"
#include "print.h"
#include "string_helpers.h"

static volatile uint64_t thread_tick_counter = 0;

void idle_thread_entry(void) {
    PRINT(MAGENTA, BLACK, "[IDLE] Started directly (no wrapper)\n");
    
    volatile uint64_t counter = 0;
    while (1) {
        counter++;
        
        if (counter % 100000000 == 0) {
            PRINT(WHITE, BLACK, "[IDLE] %llu\n", counter / 100000000);
        }
        
        if (counter % 10000000 == 0) {
            thread_yield();
        }
    }
    
    PRINT(YELLOW, BLACK, "[IDLE] Exited!\n");
    while(1) __asm__ volatile("hlt");
}

void test_thread_entry(void) {
    PRINT(YELLOW, BLACK, "[TEST] Thread started!\n");
    
    volatile uint64_t counter = 0;
    while (1) {
        counter++;
        
        if (counter % 100000000 == 0) {
            PRINT(MAGENTA, BLACK, "[TEST] %llu\n", counter / 100000000);
        }
        
        if (counter % 10000000 == 0) {
            thread_yield();
        }
    }
}
void periodic_task_entry(void) {
    uint64_t iteration = 0;
    
    while (1) {
        for (volatile int i = 0; i < 100000; i++) {
        }
        
        iteration++;
        
        thread_yield();
    }
}

void high_priority_entry(void) {
    uint64_t critical_events = 0;
    
    while (1) {
        critical_events++;
        
        for (volatile int i = 0; i < 50000; i++);
        
        thread_yield();
    }
}

void background_worker_entry(void) {
    uint64_t tasks_completed = 0;
    
    while (1) {
        for (volatile int i = 0; i < 500000; i++) {
        }
        
        tasks_completed++;
        
        thread_yield();
    }
}

void monitor_thread_entry(void) {
    uint64_t monitor_cycles = 0;
    
    while (1) {
        monitor_cycles++;
        
        for (volatile int i = 0; i < 200000; i++);
        
        thread_yield();
    }
}

void init_kernel_threads(void) {
    PRINT(MAGENTA, BLACK, "\n=== Initializing Threads ===\n");
    
    int init_pid = process_create("init", YELLOW);
    if (init_pid < 0) {
        PRINT(YELLOW, BLACK, "[ERROR] Failed to create init process\n");
        return;
    }
    
    const uint32_t THREAD_STACK_SIZE = 65536;
    
    int idle_tid = thread_create(init_pid, idle_thread_entry, 
                                  THREAD_STACK_SIZE, 10000000, 1000000000, 1000000000);
    PRINT(MAGENTA, BLACK, "[OK] Idle thread TID=%d\n", idle_tid);
    
    int test_tid = thread_create(init_pid, test_thread_entry,
                                  THREAD_STACK_SIZE, 10000000, 1000000000, 1000000000);
    PRINT(MAGENTA, BLACK, "[OK] Test thread TID=%d\n", test_tid);
    
    PRINT(MAGENTA, BLACK, "[INFO] 2 threads created\n");
}