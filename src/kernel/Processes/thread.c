// thread.c - FIXED with proper context initialization and switching

#include "process.h"
#include "memory.h"
#include "print.h"

extern void switch_to_thread(cpu_context_t *old_ctx, cpu_context_t *new_ctx);

// Global thread table
thread_t thread_table[MAX_THREADS_GLOBAL];

// Ready queue for round-robin scheduling
static thread_t *ready_queue_head = NULL;
static thread_t *current_thread = NULL;

// Counter for TID generation
static uint32_t next_tid = 1;

// Current time (in nanoseconds, incremented by timer)
static uint64_t current_time_ns = 0;

// Flag to enable/disable scheduling
static volatile int scheduler_enabled = 0;

// Initialize scheduler
void scheduler_init(void) {
    // Clear thread table
    for (int i = 0; i < MAX_THREADS_GLOBAL; i++) {
        thread_table[i].used = 0;
        thread_table[i].tid = 0;
        thread_table[i].parent = NULL;
        thread_table[i].state = THREAD_STATE_TERMINATED;
        thread_table[i].next = NULL;
        thread_table[i].stack_base = NULL;
        thread_table[i].stack_pointer = 0;
        thread_table[i].stack_size = 0;
        thread_table[i].private_data = NULL;
    }
    
    ready_queue_head = NULL;
    current_thread = NULL;
    scheduler_enabled = 0;  // DISABLED by default
    
    char msg[] = "[THREAD] Scheduler initialized (DISABLED)\n";
    printk(0xFF00FF00, 0x000000, msg);
}

// Enable the scheduler
void scheduler_enable(void) {
    scheduler_enabled = 1;
    char msg[] = "[THREAD] Scheduler ENABLED\n";
    printk(0xFF00FF00, 0x000000, msg);
}

// Find free thread slot
static int find_free_thread(void) {
    for (int i = 0; i < MAX_THREADS_GLOBAL; i++) {
        if (!thread_table[i].used) {
            return i;
        }
    }
    return -1;
}

// Insert thread into ready queue (simple FIFO)
void insert_ready_queue(thread_t *thread) {
    thread->next = NULL;
    
    if (!ready_queue_head) {
        ready_queue_head = thread;
        return;
    }
    
    thread_t *current = ready_queue_head;
    while (current->next) {
        current = current->next;
    }
    current->next = thread;
}

// Remove thread from ready queue
void remove_ready_queue(thread_t *thread) {
    if (!ready_queue_head) return;
    
    if (ready_queue_head == thread) {
        ready_queue_head = thread->next;
        thread->next = NULL;
        return;
    }
    
    thread_t *current = ready_queue_head;
    while (current->next) {
        if (current->next == thread) {
            current->next = thread->next;
            thread->next = NULL;
            return;
        }
        current = current->next;
    }
}

// Thread entry wrapper - this is where threads actually start
static void thread_entry_wrapper(void) {
    thread_t *current = get_current_thread();
    if (!current) {
        char err[] = "[THREAD] No current thread in wrapper!\n";
        printk(0xFFFF0000, 0x000000, err);
        while(1) __asm__ volatile("hlt");
    }
    
    char msg[] = "[THREAD] TID=%u starting execution\n";
    printk(0xFF00FFFF, 0x000000, msg, current->tid);
    
    // Call the actual thread function
    void (*entry)(void) = (void (*)(void))current->entry_point;
    if (entry) {
        entry();
    }
    
    // Thread returned, exit it
    char done[] = "[THREAD] TID=%u returned, exiting\n";
    printk(0xFFFFFF00, 0x000000, done, current->tid);
    
    thread_exit();
}

// CRITICAL: Setup initial thread stack frame
static void setup_thread_stack(thread_t *thread, void (*entry)(void)) {
    // Stack grows downward, so we start at the top
    uint64_t *stack = (uint64_t*)((uint8_t*)thread->stack_base + thread->stack_size);
    
    // Align to 16 bytes
    stack = (uint64_t*)((uint64_t)stack & ~0xF);
    
    // Push fake return address (we'll never return here, but stack walkers expect it)
    *(--stack) = 0;  // Return address = NULL
    
    // Now set RSP to point here
    thread->context.rsp = (uint64_t)stack;
    thread->context.rbp = 0;  // No frame pointer yet
    thread->context.rip = (uint64_t)entry;  // Will jump to wrapper
    
    // Initialize other registers to safe values
    thread->context.rbx = 0;
    thread->context.r12 = 0;
    thread->context.r13 = 0;
    thread->context.r14 = 0;
    thread->context.r15 = 0;
}

// Create a new thread
int thread_create(uint32_t pid, void (*entry_point)(void), uint32_t stack_size,
                  uint64_t runtime, uint64_t deadline, uint64_t period) {
    process_t *proc = get_process(pid);
    if (!proc) {
        char err[] = "[THREAD] Process PID=%u not found\n";
        printk(0xFFFF0000, 0x000000, err, pid);
        return -1;
    }
    
    if (proc->thread_count >= MAX_THREADS_PER_PROCESS) {
        char err[] = "[THREAD] Max threads reached for PID=%u\n";
        printk(0xFFFF0000, 0x000000, err, pid);
        return -1;
    }
    
    int idx = find_free_thread();
    if (idx < 0) {
        char err[] = "[THREAD] No free thread slots\n";
        printk(0xFFFF0000, 0x000000, err);
        return -1;
    }
    
    thread_t *thread = &thread_table[idx];
    thread->tid = next_tid++;
    thread->parent = proc;
    thread->state = THREAD_STATE_READY;
    thread->used = 1;
    thread->next = NULL;
    thread->private_data = NULL;
    thread->entry_point = (uint64_t)entry_point;  // Save for wrapper
    
    // Allocate stack
    thread->stack_size = stack_size;
    thread->stack_base = kmalloc(stack_size);
    if (!thread->stack_base) {
        char err[] = "[THREAD] Failed to allocate stack\n";
        printk(0xFFFF0000, 0x000000, err);
        thread->used = 0;
        return -1;
    }
    
    // Zero the stack for safety
    for (uint32_t i = 0; i < stack_size; i++) {
        ((uint8_t*)thread->stack_base)[i] = 0;
    }
    
    // Initialize CPU context - this is CRITICAL
    for (int i = 0; i < sizeof(cpu_context_t); i++) {
        ((uint8_t*)&thread->context)[i] = 0;
    }
    
    // Setup the initial stack frame properly
    setup_thread_stack(thread, thread_entry_wrapper);
    
    // Scheduling parameters (not really used in simple round-robin)
    thread->sched.runtime = runtime;
    thread->sched.deadline = deadline;
    thread->sched.period = period;
    thread->sched.absolute_deadline = current_time_ns + deadline;
    thread->sched.remaining_runtime = runtime;
    thread->last_scheduled = current_time_ns;
    
    // Add to process thread list
    proc->threads[proc->thread_count++] = thread;
    
    // Add to ready queue
    insert_ready_queue(thread);
    
    char msg[] = "[THREAD] Created TID=%u for PID=%u (entry=0x%llx, stack=0x%llx, size=%u)\n";
    printk(0xFF00FF00, 0x000000, msg, thread->tid, proc->pid, 
           (uint64_t)entry_point, thread->context.rsp, stack_size);
    return thread->tid;
}

// Get current running thread
thread_t* get_current_thread(void) {
    return current_thread;
}

// Get thread by TID
thread_t* get_thread(uint32_t tid) {
    for (int i = 0; i < MAX_THREADS_GLOBAL; i++) {
        if (thread_table[i].used && thread_table[i].tid == tid) {
            return &thread_table[i];
        }
    }
    return NULL;
}

// FIXED: Actual scheduler with context switch
void schedule(void) {
    // Don't schedule if disabled
    if (!scheduler_enabled) {
        return;
    }
    
    thread_t *prev_thread = current_thread;
    
    // Put current thread back if it was running
    if (current_thread && current_thread->state == THREAD_STATE_RUNNING) {
        current_thread->state = THREAD_STATE_READY;
        insert_ready_queue(current_thread);
    }
    
    // Pick next thread (first in queue)
    thread_t *next_thread = ready_queue_head;
    
    if (!next_thread) {
        // No threads ready - stay in current or idle
        return;
    }
    
    // Remove from ready queue
    ready_queue_head = next_thread->next;
    next_thread->next = NULL;
    
    // Switch to this thread
    next_thread->state = THREAD_STATE_RUNNING;
    next_thread->last_scheduled = current_time_ns;
    current_thread = next_thread;
    
    // Update parent process state
    if (next_thread->parent) {
        next_thread->parent->state = PROCESS_STATE_RUNNING;
    }
    
    // CRITICAL: Perform actual context switch if we're switching threads
    if (prev_thread && prev_thread != next_thread) {
        // This saves prev_thread context and loads next_thread context
        switch_to_thread(&prev_thread->context, &next_thread->context);
    } else if (!prev_thread && next_thread) {
        // First thread ever - just load its context
        char msg[] = "[SCHED] Starting first thread: TID=%u\n";
        printk(0xFF00FF00, 0x000000, msg, next_thread->tid);
        
        // Jump to the thread's saved context
        __asm__ volatile(
            "mov %0, %%rsp\n"
            "mov %1, %%rbp\n"
            "jmp *%2\n"
            : 
            : "r"(next_thread->context.rsp),
              "r"(next_thread->context.rbp),
              "r"(next_thread->context.rip)
            : "memory"
        );
    }
}

// Called by timer interrupt (~10ms)
void scheduler_tick(void) {
    if (!scheduler_enabled) {
        return;  // Don't schedule if disabled
    }
    
    current_time_ns += 10000000;  // 10ms
    
    // Simple time slice: reschedule every tick
    schedule();
}

// Thread voluntarily yields CPU
void thread_yield(void) {
    if (!scheduler_enabled) {
        return;  // Just return if scheduler disabled
    }
    schedule();
}

// Block a thread
void thread_block(uint32_t tid) {
    thread_t *thread = get_thread(tid);
    if (!thread) return;
    
    thread->state = THREAD_STATE_BLOCKED;
    remove_ready_queue(thread);
    
    // If it's the current thread, schedule another
    if (thread == current_thread) {
        current_thread = NULL;
        schedule();
    }
    
    char msg[] = "[THREAD] Blocked TID=%u\n";
    printk(0xFFFFFF00, 0x000000, msg, tid);
}

// Unblock a thread
void thread_unblock(uint32_t tid) {
    thread_t *thread = get_thread(tid);
    if (!thread) return;
    
    if (thread->state == THREAD_STATE_BLOCKED) {
        thread->state = THREAD_STATE_READY;
        insert_ready_queue(thread);
        
        char msg[] = "[THREAD] Unblocked TID=%u\n";
        printk(0xFFFFFF00, 0x000000, msg, tid);
    }
}

// Exit current thread
void thread_exit(void) {
    if (!current_thread) return;
    
    char msg[] = "[THREAD] Exiting TID=%u\n";
    printk(0xFFFFFF00, 0x000000, msg, current_thread->tid);
    
    current_thread->state = THREAD_STATE_TERMINATED;
    current_thread->used = 0;
    
    // Free stack
    if (current_thread->stack_base) {
        kfree(current_thread->stack_base);
        current_thread->stack_base = NULL;
    }
    
    // Remove from parent process
    process_t *proc = current_thread->parent;
    if (proc) {
        for (int i = 0; i < proc->thread_count; i++) {
            if (proc->threads[i] == current_thread) {
                for (int j = i; j < proc->thread_count - 1; j++) {
                    proc->threads[j] = proc->threads[j + 1];
                }
                proc->thread_count--;
                proc->threads[proc->thread_count] = NULL;
                break;
            }
        }
        
        if (proc->thread_count == 0) {
            proc->state = PROCESS_STATE_TERMINATED;
        }
    }
    
    current_thread = NULL;
    
    // Schedule next thread - THIS NEVER RETURNS
    schedule();
    
    // Should never reach here
    while (1) {
        __asm__ volatile("hlt");
    }
}

// =============================================================================
// ASSEMBLY CONTEXT SWITCH - Simplified and Fixed
// =============================================================================

__asm__(
".global switch_to_thread\n"
"switch_to_thread:\n"
"    # rdi = old context pointer\n"
"    # rsi = new context pointer\n"
"    \n"
"    # Save old context\n"
"    mov %rsp, 0(%rdi)\n"       //# Save RSP
"    mov %rbp, 8(%rdi)\n"       //# Save RBP
"    mov %rbx, 16(%rdi)\n"      //# Save RBX
"    mov %r12, 24(%rdi)\n"      //# Save R12
"    mov %r13, 32(%rdi)\n"      //# Save R13
"    mov %r14, 40(%rdi)\n"      //# Save R14
"    mov %r15, 48(%rdi)\n"      //# Save R15
"    \n"
"    # Save return address as RIP\n"
"    lea 0f(%rip), %rax\n"
"    mov %rax, 56(%rdi)\n"      //# Save RIP
"    \n"
"    # Load new context\n"
"    mov 0(%rsi), %rsp\n"       //# Restore RSP
"    mov 8(%rsi), %rbp\n"       //# Restore RBP
"    mov 16(%rsi), %rbx\n"      //# Restore RBX
"    mov 24(%rsi), %r12\n"      //# Restore R12
"    mov 32(%rsi), %r13\n"      //# Restore R13
"    mov 40(%rsi), %r14\n"      //# Restore R14
"    mov 48(%rsi), %r15\n"      //# Restore R15
"    \n"
"    # Jump to saved RIP\n"
"    jmp *56(%rsi)\n"
"    \n"
"0:  # Return point\n"
"    ret\n"
);