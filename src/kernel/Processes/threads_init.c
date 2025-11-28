#include "process.h"
#include "print.h"
#include "string_helpers.h"

// Thread entry point counter for demonstration
static volatile uint64_t thread_tick_counter = 0;

// ============================================================================
// IDLE THREAD - Runs when no other threads are ready
// ============================================================================
void idle_thread_entry(void) {
    PRINT(0xFF00FF00, 0x000000, "[IDLE] Started directly (no wrapper)\n");
    
    volatile uint64_t counter = 0;
    while (1) {
        counter++;
        
        if (counter % 100000000 == 0) {
            PRINT(0xFFFFFF00, 0x000000, "[IDLE] %llu\n", counter / 100000000);
        }
        
        if (counter % 10000000 == 0) {
            thread_yield();
        }
    }
    
    // Should never exit, but just in case
    PRINT(0xFFFF0000, 0x000000, "[IDLE] Exited!\n");
    while(1) __asm__ volatile("hlt");
}

void test_thread_entry(void) {
    PRINT(0xFFFF00FF, 0x000000, "[TEST] Thread started!\n");
    
    volatile uint64_t counter = 0;
    while (1) {
        counter++;
        
        if (counter % 100000000 == 0) {
            PRINT(0xFF00FFFF, 0x000000, "[TEST] %llu\n", counter / 100000000);
        }
        
        if (counter % 10000000 == 0) {
            thread_yield();
        }
    }
}
// ============================================================================
// PERIODIC TASK THREAD - Real-time periodic behavior
// ============================================================================
void periodic_task_entry(void) {
    uint64_t iteration = 0;
    
    while (1) {
        // Simulate some work
        for (volatile int i = 0; i < 100000; i++) {
            // Work simulation
        }
        
        iteration++;
        
        // Yield to scheduler (allows preemption)
        thread_yield();
    }
}

// ============================================================================
// HIGH PRIORITY THREAD - Short deadline, critical task
// ============================================================================
void high_priority_entry(void) {
    uint64_t critical_events = 0;
    
    while (1) {
        // Simulate critical event processing
        critical_events++;
        
        // Short work burst
        for (volatile int i = 0; i < 50000; i++);
        
        thread_yield();
    }
}

// ============================================================================
// BACKGROUND WORKER - Low priority, long deadline
// ============================================================================
void background_worker_entry(void) {
    uint64_t tasks_completed = 0;
    
    while (1) {
        // Simulate background processing (more work than others)
        for (volatile int i = 0; i < 500000; i++) {
            // Heavy computation simulation
        }
        
        tasks_completed++;
        
        thread_yield();
    }
}

// ============================================================================
// MONITORING THREAD - Watches system state
// ============================================================================
void monitor_thread_entry(void) {
    uint64_t monitor_cycles = 0;
    
    while (1) {
        monitor_cycles++;
        
        // Lighter work than background tasks
        for (volatile int i = 0; i < 200000; i++);
        
        thread_yield();
    }
}

// ============================================================================
// INITIALIZE ALL THREADS
// ============================================================================
void init_kernel_threads(void) {
    PRINT(0xFF00FFFF, 0x000000, "\n=== Initializing Threads ===\n");
    
    int init_pid = process_create("init", 0xFFFF000000000000);
    if (init_pid < 0) {
        PRINT(0xFFFF0000, 0x000000, "[ERROR] Failed to create init process\n");
        return;
    }
    
    const uint32_t THREAD_STACK_SIZE = 65536;
    
    int idle_tid = thread_create(init_pid, idle_thread_entry, 
                                  THREAD_STACK_SIZE, 10000000, 1000000000, 1000000000);
    PRINT(0xFF00FF00, 0x000000, "[OK] Idle thread TID=%d\n", idle_tid);
    
    int test_tid = thread_create(init_pid, test_thread_entry,
                                  THREAD_STACK_SIZE, 10000000, 1000000000, 1000000000);
    PRINT(0xFF00FF00, 0x000000, "[OK] Test thread TID=%d\n", test_tid);
    
    PRINT(0xFF00FFFF, 0x000000, "[INFO] 2 threads created\n");
}