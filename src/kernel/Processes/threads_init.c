#include "process.h"
#include "print.h"

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
    char header[] = "\n=== Initializing Kernel Threads ===\n";
    printk(0xFF00FFFF, 0x000000, header);
    
    // Create init process (if not already created)
    char init[] = "init";
    int init_pid = process_create(init, 0xFFFF000000000000);
    if (init_pid < 0) {
        char err[] = "[ERROR] Failed to create init process\n";
        printk(0xFFFF0000, 0x000000, err);
        return;
    }
    
    // CRITICAL FIX: Use MUCH larger stacks (16KB minimum)
    // The 8KB stacks were overflowing with all the printk calls
    const uint32_t THREAD_STACK_SIZE = 16384;  // 16KB per thread
    
    // Thread parameters (runtime, deadline, period) in nanoseconds
    // SCHED_DEADLINE uses: runtime <= deadline <= period
    
    // 1. Idle thread - lowest priority (longest deadline)
    //    Runtime: 10ms, Deadline: 1000ms, Period: 1000ms
    char msg1[] = "[THREAD] Creating idle thread...\n";
    printk(0xFFFFFF00, 0x000000, msg1);
    
    int idle_tid = thread_create(init_pid, idle_thread_entry, 
                                  THREAD_STACK_SIZE,       // 16KB stack
                                  10000000,                // 10ms runtime
                                  1000000000,              // 1000ms deadline
                                  1000000000);             // 1000ms period
    if (idle_tid > 0) {
        char msg[] = "[OK] Created idle thread (TID=%d)\n";
        printk(0xFF00FF00, 0x000000, msg, idle_tid);
    } else {
        char err[] = "[ERROR] Failed to create idle thread\n";
        printk(0xFFFF0000, 0x000000, err);
        return;
    }
    
    // Small delay to let things settle
    for (volatile int i = 0; i < 1000000; i++);
    
    // 2. High priority thread - shortest deadline
    //    Runtime: 5ms, Deadline: 50ms, Period: 100ms
    char msg2[] = "[THREAD] Creating high-priority thread...\n";
    printk(0xFFFFFF00, 0x000000, msg2);
    
    int high_tid = thread_create(init_pid, high_priority_entry,
                                  THREAD_STACK_SIZE,       // 16KB stack
                                  5000000,                 // 5ms runtime
                                  50000000,                // 50ms deadline
                                  100000000);              // 100ms period
    
    if (high_tid > 0) {
        char msg[] = "[OK] Created high-priority thread (TID=%d)\n";
        printk(0xFF00FF00, 0x000000, msg, high_tid);
    } else {
        char err[] = "[ERROR] Failed to create high-priority thread\n";
        printk(0xFFFF0000, 0x000000, err);
        return;
    }
    
    // Small delay
    for (volatile int i = 0; i < 1000000; i++);
    
    // 3. Periodic task - medium priority
    //    Runtime: 10ms, Deadline: 200ms, Period: 200ms
    char msg3[] = "[THREAD] Creating periodic thread...\n";
    printk(0xFFFFFF00, 0x000000, msg3);
    
    int periodic_tid = thread_create(init_pid, periodic_task_entry,
                                      THREAD_STACK_SIZE,   // 16KB stack
                                      10000000,            // 10ms runtime
                                      200000000,           // 200ms deadline
                                      200000000);          // 200ms period
    if (periodic_tid > 0) {
        char msg[] = "[OK] Created periodic thread (TID=%d)\n";
        printk(0xFF00FF00, 0x000000, msg, periodic_tid);
    } else {
        char err[] = "[ERROR] Failed to create periodic thread\n";
        printk(0xFFFF0000, 0x000000, err);
        return;
    }
    
    // Small delay
    for (volatile int i = 0; i < 1000000; i++);
    
    // 4. Background worker - low priority
    //    Runtime: 20ms, Deadline: 500ms, Period: 500ms
    char msg4[] = "[THREAD] Creating background worker...\n";
    printk(0xFFFFFF00, 0x000000, msg4);
    
    int bg_tid = thread_create(init_pid, background_worker_entry,
                                THREAD_STACK_SIZE,         // 16KB stack
                                20000000,                  // 20ms runtime
                                500000000,                 // 500ms deadline
                                500000000);                // 500ms period
    if (bg_tid > 0) {
        char msg[] = "[OK] Created background worker (TID=%d)\n";
        printk(0xFF00FF00, 0x000000, msg, bg_tid);
    } else {
        char err[] = "[ERROR] Failed to create background worker\n";
        printk(0xFFFF0000, 0x000000, err);
        return;
    }
    
    // Small delay
    for (volatile int i = 0; i < 1000000; i++);
    
    // 5. System monitor - medium priority
    //    Runtime: 8ms, Deadline: 300ms, Period: 300ms
    char msg5[] = "[THREAD] Creating monitor thread...\n";
    printk(0xFFFFFF00, 0x000000, msg5);
    
    int monitor_tid = thread_create(init_pid, monitor_thread_entry,
                                     THREAD_STACK_SIZE,    // 16KB stack
                                     8000000,              // 8ms runtime
                                     300000000,            // 300ms deadline
                                     300000000);           // 300ms period
    if (monitor_tid > 0) {
        char msg[] = "[OK] Created monitor thread (TID=%d)\n";
        printk(0xFF00FF00, 0x000000, msg, monitor_tid);
    } else {
        char err[] = "[ERROR] Failed to create monitor thread\n";
        printk(0xFFFF0000, 0x000000, err);
        return;
    }
    
    char summary[] = "\n[INFO] Thread initialization complete\n";
    char summary2[] = "[INFO] All threads created but scheduler NOT started\n";
    char summary3[] = "[INFO] Threads will remain dormant until scheduler is activated\n\n";
    
    printk(0xFF00FFFF, 0x000000, summary);
    printk(0xFF00FFFF, 0x000000, summary2);
    printk(0xFF00FFFF, 0x000000, summary3);
}