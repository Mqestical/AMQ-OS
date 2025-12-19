// scheduler.c - Complete scheduler reimplementation

#include "process.h"
#include "memory.h"
#include "print.h"
#include "string_helpers.h"
#include "TSS.h"

// Thread table
thread_t thread_table[MAX_THREADS_GLOBAL];

// Scheduler state
static thread_t *current_thread = NULL;
static thread_t *idle_thread = NULL;
static uint32_t next_tid = 1;
static volatile int scheduler_enabled = 0;
static volatile int in_scheduler = 0;

// Simple ready queue
static thread_t *ready_queue_head = NULL;
static thread_t *ready_queue_tail = NULL;

void scheduler_init(void) {
    // Zero out thread table
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
        thread_table[i].entry_point = 0;
    }

    ready_queue_head = NULL;
    ready_queue_tail = NULL;
    current_thread = NULL;
    idle_thread = NULL;
    scheduler_enabled = 0;
    in_scheduler = 0;

    PRINT(MAGENTA, BLACK, "[SCHED] Scheduler initialized (DISABLED)\n");
}

void scheduler_enable(void) {
    scheduler_enabled = 1;
    PRINT(MAGENTA, BLACK, "[SCHED] Scheduler ENABLED\n");
}

void scheduler_disable(void) {
    scheduler_enabled = 0;
    PRINT(MAGENTA, BLACK, "[SCHED] Scheduler DISABLED\n");
}

static int find_free_thread_slot(void) {
    for (int i = 0; i < MAX_THREADS_GLOBAL; i++) {
        if (!thread_table[i].used) {
            return i;
        }
    }
    return -1;
}

void ready_queue_add(thread_t *thread) {
    if (!thread) return;
    
    thread->next = NULL;
    
    if (!ready_queue_head) {
        ready_queue_head = thread;
        ready_queue_tail = thread;
    } else {
        ready_queue_tail->next = thread;
        ready_queue_tail = thread;
    }
}

void ready_queue_remove(thread_t *thread) {
    if (!thread || !ready_queue_head) return;
    
    if (ready_queue_head == thread) {
        ready_queue_head = thread->next;
        if (ready_queue_tail == thread) {
            ready_queue_tail = NULL;
        }
        thread->next = NULL;
        return;
    }
    
    thread_t *current = ready_queue_head;
    while (current->next) {
        if (current->next == thread) {
            current->next = thread->next;
            if (ready_queue_tail == thread) {
                ready_queue_tail = current;
            }
            thread->next = NULL;
            return;
        }
        current = current->next;
    }
}

static void thread_wrapper(void) {
    thread_t *current = current_thread;
    if (!current) {
        PRINT(YELLOW, BLACK, "[THREAD] No current thread in wrapper!\n");
        while(1) __asm__ volatile("hlt");
    }
    
    // Enable interrupts for this thread
    __asm__ volatile("sti");
    
    void (*entry)(void) = (void (*)(void))current->entry_point;
    if (entry) {
        entry();
    }
    
    PRINT(WHITE, BLACK, "[THREAD] TID=%u exited normally\n", current->tid);
    thread_exit();
}

static void setup_thread_context(thread_t *thread, void (*entry)(void)) {
    // Clear context
    for (int i = 0; i < sizeof(cpu_context_t); i++) {
        ((uint8_t*)&thread->context)[i] = 0;
    }
    
    // Setup stack
    uint64_t stack_top = (uint64_t)thread->stack_base + thread->stack_size;
    stack_top &= ~0xFULL;  // 16-byte align
    
    // Setup context for switch_to_thread
    thread->context.rsp = stack_top;
    thread->context.rbp = 0;
    thread->context.rip = (uint64_t)thread_wrapper;
    thread->context.rflags = 0x202;  // IF=1
    
    // Set segment registers
    thread->context.cs = 0x08;  // Kernel code
    thread->context.ss = 0x10;  // Kernel data
}

int thread_create(uint32_t pid, void (*entry_point)(void), uint32_t stack_size,
                  uint64_t runtime, uint64_t deadline, uint64_t period) {
    
    if (!entry_point) {
        PRINT(YELLOW, BLACK, "[THREAD] NULL entry point\n");
        return -1;
    }
    
    process_t *proc = get_process(pid);
    if (!proc) {
        PRINT(YELLOW, BLACK, "[THREAD] Process %u not found\n", pid);
        return -1;
    }
    
    if (proc->thread_count >= MAX_THREADS_PER_PROCESS) {
        PRINT(YELLOW, BLACK, "[THREAD] Max threads for process %u\n", pid);
        return -1;
    }
    
    int slot = find_free_thread_slot();
    if (slot < 0) {
        PRINT(YELLOW, BLACK, "[THREAD] No free thread slots\n");
        return -1;
    }
    
    thread_t *thread = &thread_table[slot];
    
    // Allocate stack
    thread->stack_size = stack_size;
    thread->stack_base = kmalloc(stack_size);
    if (!thread->stack_base) {
        PRINT(YELLOW, BLACK, "[THREAD] Stack allocation failed\n");
        return -1;
    }
    
    // Zero stack (for debugging)
    for (uint32_t i = 0; i < stack_size; i++) {
        ((uint8_t*)thread->stack_base)[i] = 0xCC;
    }
    
    // Initialize thread
    thread->tid = next_tid++;
    thread->parent = proc;
    thread->state = THREAD_STATE_READY;
    thread->used = 1;
    thread->next = NULL;
    thread->private_data = NULL;
    thread->entry_point = (uint64_t)entry_point;
    
    // Setup context
    setup_thread_context(thread, entry_point);
    
    // Scheduling parameters
    thread->sched.runtime = runtime;
    thread->sched.deadline = deadline;
    thread->sched.period = period;
    thread->sched.absolute_deadline = 0;
    thread->sched.remaining_runtime = runtime;
    thread->last_scheduled = 0;
    
    // Add to process
    proc->threads[proc->thread_count++] = thread;
    
    // Add to ready queue
    ready_queue_add(thread);
    
    PRINT(MAGENTA, BLACK, "[THREAD] Created TID=%u for PID=%u (entry=0x%llx)\n", 
          thread->tid, proc->pid, thread->entry_point);
    
    return thread->tid;
}

thread_t* get_thread(uint32_t tid) {
    for (int i = 0; i < MAX_THREADS_GLOBAL; i++) {
        if (thread_table[i].used && thread_table[i].tid == tid) {
            return &thread_table[i];
        }
    }
    return NULL;
}

thread_t* get_current_thread(void) {
    return current_thread;
}

void thread_block(uint32_t tid) {
    thread_t *thread = get_thread(tid);
    if (!thread) {
        PRINT(YELLOW, BLACK, "[THREAD] Block: TID=%u not found\n", tid);
        return;
    }
    
    if (thread->state == THREAD_STATE_BLOCKED) {
        return;  // Already blocked
    }
    
    thread->state = THREAD_STATE_BLOCKED;
    ready_queue_remove(thread);
    
    PRINT(WHITE, BLACK, "[THREAD] Blocked TID=%u\n", tid);
    
    // If blocking current thread, schedule next
    if (thread == current_thread) {
        schedule();
    }
}

void thread_unblock(uint32_t tid) {
    thread_t *thread = get_thread(tid);
    if (!thread) {
        PRINT(YELLOW, BLACK, "[THREAD] Unblock: TID=%u not found\n", tid);
        return;
    }
    
    if (thread->state != THREAD_STATE_BLOCKED) {
        return;  // Not blocked
    }
    
    thread->state = THREAD_STATE_READY;
    ready_queue_add(thread);
    
    PRINT(WHITE, BLACK, "[THREAD] Unblocked TID=%u\n", tid);
}

void thread_exit(void) {
    if (!current_thread) {
        PRINT(YELLOW, BLACK, "[THREAD] Exit: no current thread\n");
        while(1) __asm__ volatile("hlt");
    }
    
    uint32_t tid = current_thread->tid;
    PRINT(WHITE, BLACK, "[THREAD] Exiting TID=%u\n", tid);
    
    current_thread->state = THREAD_STATE_TERMINATED;
    current_thread->used = 0;
    
    // Free stack
    if (current_thread->stack_base) {
        kfree(current_thread->stack_base);
        current_thread->stack_base = NULL;
    }
    
    // Remove from process
    process_t *proc = current_thread->parent;
    if (proc) {
        for (int i = 0; i < proc->thread_count; i++) {
            if (proc->threads[i] == current_thread) {
                // Shift remaining threads
                for (int j = i; j < proc->thread_count - 1; j++) {
                    proc->threads[j] = proc->threads[j + 1];
                }
                proc->thread_count--;
                proc->threads[proc->thread_count] = NULL;
                break;
            }
        }
        
        // Mark process as terminated if no threads left
        if (proc->thread_count == 0) {
            proc->state = PROCESS_STATE_TERMINATED;
            PRINT(WHITE, BLACK, "[THREAD] Process %u terminated (no threads)\n", proc->pid);
        }
    }
    
    current_thread = NULL;
    
    // Schedule next thread
    schedule();
    
    // Should never reach here
    PRINT(YELLOW, BLACK, "[FATAL] Thread exit returned!\n");
    while(1) __asm__ volatile("hlt");
}

void thread_yield(void) {
    if (!scheduler_enabled) return;
    if (!current_thread) return;
    
    schedule();
}

extern void switch_to_thread(cpu_context_t *old_ctx, cpu_context_t *new_ctx);

void schedule(void) {
    if (!scheduler_enabled) return;
    
    // Prevent recursive scheduling
    if (in_scheduler) return;
    in_scheduler = 1;
    
    thread_t *prev = current_thread;
    thread_t *next = ready_queue_head;
    
    // If current thread is still runnable, put it back
    if (prev && prev->state == THREAD_STATE_RUNNING) {
        prev->state = THREAD_STATE_READY;
        ready_queue_add(prev);
    }
    
    // Get next thread from queue
    if (!next) {
        // No threads ready
        in_scheduler = 0;
        
        // If we had a previous thread, something went wrong
        if (prev && prev->state != THREAD_STATE_BLOCKED && 
            prev->state != THREAD_STATE_TERMINATED) {
            PRINT(YELLOW, BLACK, "[SCHED] No threads but prev exists!\n");
        }
        return;
    }
    
    // Remove from ready queue
    ready_queue_head = next->next;
    if (ready_queue_tail == next) {
        ready_queue_tail = NULL;
    }
    next->next = NULL;
    next->state = THREAD_STATE_RUNNING;
    
    // If no previous thread, this is the first thread
    if (!prev) {
        current_thread = next;
        in_scheduler = 0;
        
        PRINT(MAGENTA, BLACK, "[SCHED] Starting first thread TID=%u\n", next->tid);
        
        // Jump to thread directly
        __asm__ volatile(
            "mov %0, %%rsp\n"
            "xor %%rbp, %%rbp\n"
            "sti\n"
            "jmp *%1\n"
            :
            : "r"(next->context.rsp), "r"(next->context.rip)
            : "memory"
        );
        
        // Should never return
        PRINT(YELLOW, BLACK, "[FATAL] First thread returned!\n");
        while(1) __asm__ volatile("hlt");
    }
    
    // Same thread? Nothing to do
    if (prev == next) {
        in_scheduler = 0;
        return;
    }
    
    current_thread = next;
    in_scheduler = 0;
    
    // Context switch
    switch_to_thread(&prev->context, &next->context);
}

void scheduler_tick(void) {
    if (!scheduler_enabled) return;
    
    // Don't interrupt scheduler
    if (in_scheduler) return;
    
    // For now, just yield if we have a current thread
    if (current_thread) {
        thread_yield();
    }
}

// Context switch assembly
__asm__(
".global switch_to_thread\n"
"switch_to_thread:\n"
"    pushfq\n"
"    push %rbx\n"
"    push %rbp\n"
"    push %r12\n"
"    push %r13\n"
"    push %r14\n"
"    push %r15\n"
"    mov %rsp, 56(%rdi)\n"  // Save RSP to old context
"    mov 56(%rsi), %rsp\n"  // Load RSP from new context
"    pop %r15\n"
"    pop %r14\n"
"    pop %r13\n"
"    pop %r12\n"
"    pop %rbp\n"
"    pop %rbx\n"
"    popfq\n"
"    ret\n"
);