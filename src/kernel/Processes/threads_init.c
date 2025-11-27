#include "process.h"
#include "print.h"
#include "string_helpers.h"

// Thread entry point counter for demonstration
static volatile uint64_t thread_tick_counter = 0;

// ============================================================================
// IDLE THREAD - Runs when no other threads are ready
// ============================================================================
void idle_thread_entry(void) {
    while (1) {
        // Low-power idle loop
        __asm__ volatile("hlt");  // Wait for interrupt
        
        // Occasionally yield to allow scheduler to run
        if ((thread_tick_counter % 10000) == 0) {
            thread_yield();
        }
        thread_tick_counter++;
    }
}

// ============================================================================
// PERIODIC TASK THREAD - Demonstrates real-time periodic behavior
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
    PRINT(0xFF00FFFF, 0x000000, "\n=== Initializing Kernel Threads ===\n");
    
    // Create init process (if not already created)
    int init_pid = process_create("init", 0xFFFF000000000000);
    if (init_pid < 0) {
        PRINT(0xFFFF0000, 0x000000, "[ERROR] Failed to create init process\n");
        return;
    }
    
    const uint32_t THREAD_STACK_SIZE = 16384;  // 16KB per thread
    
    // 1. Idle thread
    PRINT(0xFFFFFF00, 0x000000, "[THREAD] Creating idle thread...\n");
    int idle_tid = thread_create(init_pid, idle_thread_entry, 
                                  THREAD_STACK_SIZE, 10000000, 1000000000, 1000000000);
    if (idle_tid > 0) {
        PRINT(0xFF00FF00, 0x000000, "[OK] Created idle thread (TID=%d)\n", idle_tid);
    } else {
        PRINT(0xFFFF0000, 0x000000, "[ERROR] Failed to create idle thread\n");
        return;
    }
    
    for (volatile int i = 0; i < 1000000; i++);
    
    // 2. High priority thread
    PRINT(0xFFFFFF00, 0x000000, "[THREAD] Creating high-priority thread...\n");
    int high_tid = thread_create(init_pid, high_priority_entry,
                                  THREAD_STACK_SIZE, 5000000, 50000000, 100000000);
    if (high_tid > 0) {
        PRINT(0xFF00FF00, 0x000000, "[OK] Created high-priority thread (TID=%d)\n", high_tid);
    } else {
        PRINT(0xFFFF0000, 0x000000, "[ERROR] Failed to create high-priority thread\n");
        return;
    }
    
    for (volatile int i = 0; i < 1000000; i++);
    
    // 3. Periodic task thread
    PRINT(0xFFFFFF00, 0x000000, "[THREAD] Creating periodic thread...\n");
    int periodic_tid = thread_create(init_pid, periodic_task_entry,
                                      THREAD_STACK_SIZE, 10000000, 200000000, 200000000);
    if (periodic_tid > 0) {
        PRINT(0xFF00FF00, 0x000000, "[OK] Created periodic thread (TID=%d)\n", periodic_tid);
    } else {
        PRINT(0xFFFF0000, 0x000000, "[ERROR] Failed to create periodic thread\n");
        return;
    }
    
    for (volatile int i = 0; i < 1000000; i++);
    
    // 4. Background worker
    PRINT(0xFFFFFF00, 0x000000, "[THREAD] Creating background worker...\n");
    int bg_tid = thread_create(init_pid, background_worker_entry,
                                THREAD_STACK_SIZE, 20000000, 500000000, 500000000);
    if (bg_tid > 0) {
        PRINT(0xFF00FF00, 0x000000, "[OK] Created background worker (TID=%d)\n", bg_tid);
    } else {
        PRINT(0xFFFF0000, 0x000000, "[ERROR] Failed to create background worker\n");
        return;
    }
    
    for (volatile int i = 0; i < 1000000; i++);
    
    // 5. System monitor thread
    PRINT(0xFFFFFF00, 0x000000, "[THREAD] Creating monitor thread...\n");
    int monitor_tid = thread_create(init_pid, monitor_thread_entry,
                                     THREAD_STACK_SIZE, 8000000, 300000000, 300000000);
    if (monitor_tid > 0) {
        PRINT(0xFF00FF00, 0x000000, "[OK] Created monitor thread (TID=%d)\n", monitor_tid);
    } else {
        PRINT(0xFFFF0000, 0x000000, "[ERROR] Failed to create monitor thread\n");
        return;
    }
    
    PRINT(0xFF00FFFF, 0x000000, "\n[INFO] Thread initialization complete\n");
    PRINT(0xFF00FFFF, 0x000000, "[INFO] All threads created but scheduler NOT started\n");
    PRINT(0xFF00FFFF, 0x000000, "[INFO] Threads will remain dormant until scheduler is activated\n\n");
}