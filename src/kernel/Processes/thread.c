// thread.c - WORKING VERSION

#include "process.h"
#include "memory.h"
#include "print.h"
#include "string_helpers.h"

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
    scheduler_enabled = 0;
    
    PRINT(0xFF00FF00, 0x000000, "[THREAD] Scheduler initialized (DISABLED)\n");
}

void scheduler_enable(void) {
    scheduler_enabled = 1;
    PRINT(0xFF00FF00, 0x000000, "[THREAD] Scheduler ENABLED\n");
}

static int find_free_thread(void) {
    for (int i = 0; i < MAX_THREADS_GLOBAL; i++) {
        if (!thread_table[i].used) {
            return i;
        }
    }
    return -1;
}

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

static void thread_entry_wrapper(void) {
    thread_t *current = get_current_thread();
    if (!current) {
        PRINT(0xFFFF0000, 0x000000, "[WRAPPER] ERROR: No current thread!\n");
        while(1) __asm__ volatile("hlt");
    }
    
    PRINT(0xFF00FFFF, 0x000000, "[WRAPPER] Starting TID=%u\n", current->tid);
    
    void (*entry)(void) = (void (*)(void))current->entry_point;
    
    if (entry) {
        entry();
    }
    
    PRINT(0xFFFFFF00, 0x000000, "[WRAPPER] Thread %u returned\n", current->tid);
    thread_exit();
}

static void setup_thread_stack(thread_t *thread, void (*entry)(void)) {
    uint64_t stack_top = (uint64_t)thread->stack_base + thread->stack_size;
    stack_top &= ~0xF;
    
    thread->context.rsp = stack_top;
    thread->context.rbp = 0;
    thread->context.rbx = 0;
    thread->context.r12 = 0;
    thread->context.r13 = 0;
    thread->context.r14 = 0;
    thread->context.r15 = 0;
    thread->context.rip = (uint64_t)entry;
}

int thread_create(uint32_t pid, void (*entry_point)(void), uint32_t stack_size,
                  uint64_t runtime, uint64_t deadline, uint64_t period) {
    process_t *proc = get_process(pid);
    if (!proc) {
        PRINT(0xFFFF0000, 0x000000, "[THREAD] Process PID=%u not found\n", pid);
        return -1;
    }
    
    if (proc->thread_count >= MAX_THREADS_PER_PROCESS) {
        PRINT(0xFFFF0000, 0x000000, "[THREAD] Max threads reached for PID=%u\n", pid);
        return -1;
    }
    
    int idx = find_free_thread();
    if (idx < 0) {
        PRINT(0xFFFF0000, 0x000000, "[THREAD] No free thread slots\n");
        return -1;
    }
    
    thread_t *thread = &thread_table[idx];
    thread->tid = next_tid++;
    thread->parent = proc;
    thread->state = THREAD_STATE_READY;
    thread->used = 1;
    thread->next = NULL;
    thread->private_data = NULL;
    thread->entry_point = (uint64_t)entry_point;
    
    thread->stack_size = stack_size;
    thread->stack_base = kmalloc(stack_size);
    if (!thread->stack_base) {
        PRINT(0xFFFF0000, 0x000000, "[THREAD] Failed to allocate stack\n");
        thread->used = 0;
        return -1;
    }
    
    for (uint32_t i = 0; i < stack_size; i++) {
        ((uint8_t*)thread->stack_base)[i] = 0;
    }
    
    for (int i = 0; i < sizeof(cpu_context_t); i++) {
        ((uint8_t*)&thread->context)[i] = 0;
    }
    
    setup_thread_stack(thread, thread_entry_wrapper);
    
    thread->sched.runtime = runtime;
    thread->sched.deadline = deadline;
    thread->sched.period = period;
    thread->sched.absolute_deadline = current_time_ns + deadline;
    thread->sched.remaining_runtime = runtime;
    thread->last_scheduled = current_time_ns;
    
    proc->threads[proc->thread_count++] = thread;
    insert_ready_queue(thread);
    
    PRINT(0xFF00FF00, 0x000000, "[THREAD] Created TID=%u for PID=%u\n", thread->tid, proc->pid);
    
    return thread->tid;
}

thread_t* get_current_thread(void) {
    return current_thread;
}

thread_t* get_thread(uint32_t tid) {
    for (int i = 0; i < MAX_THREADS_GLOBAL; i++) {
        if (thread_table[i].used && thread_table[i].tid == tid) {
            return &thread_table[i];
        }
    }
    return NULL;
}

void schedule(void) {
    if (!scheduler_enabled) return;
    
    static volatile int in_schedule = 0;
    if (in_schedule) return;
    in_schedule = 1;
    
    thread_t *prev = current_thread;
    
    if (prev && prev->state == THREAD_STATE_RUNNING) {
        prev->state = THREAD_STATE_READY;
        insert_ready_queue(prev);
    }
    
    thread_t *next = ready_queue_head;
    if (!next) {
        in_schedule = 0;
        return;
    }
    
    ready_queue_head = next->next;
    next->next = NULL;
    next->state = THREAD_STATE_RUNNING;
    
    if (!prev) {
        current_thread = next;
        in_schedule = 0;
        
        PRINT(0xFF00FF00, 0x000000, "[SCHED] Starting TID=%u at 0x%llx\n", next->tid, next->context.rip);
        
        // Directly call the wrapper function - don't use assembly
        void (*wrapper)(void) = (void (*)(void))next->context.rip;
        wrapper();
        
        // Should never return
        PRINT(0xFFFF0000, 0x000000, "[FATAL] First thread returned!\n");
        while(1) __asm__ volatile("hlt");
    }
    
    if (prev == next) {
        in_schedule = 0;
        return;
    }
    
    current_thread = next;
    in_schedule = 0;
    
    switch_to_thread(&prev->context, &next->context);
}

void scheduler_tick(void) {
    if (!scheduler_enabled) return;
    current_time_ns += 10000000;
    // Don't auto-schedule from timer for now
    // schedule();
}

void thread_yield(void) {
    if (!scheduler_enabled) return;
    
    // Count ready threads
    int ready_count = 0;
    thread_t *t = ready_queue_head;
    while (t) {
        ready_count++;
        t = t->next;
    }
    
    // Don't yield if no other threads
    if (ready_count == 0) {
        return;
    }
    
    schedule();
}

void thread_block(uint32_t tid) {
    thread_t *thread = get_thread(tid);
    if (!thread) return;
    
    thread->state = THREAD_STATE_BLOCKED;
    remove_ready_queue(thread);
    
    if (thread == current_thread) {
        current_thread = NULL;
        schedule();
    }
    
    PRINT(0xFFFFFF00, 0x000000, "[THREAD] Blocked TID=%u\n", tid);
}

void thread_unblock(uint32_t tid) {
    thread_t *thread = get_thread(tid);
    if (!thread) return;
    
    if (thread->state == THREAD_STATE_BLOCKED) {
        thread->state = THREAD_STATE_READY;
        insert_ready_queue(thread);
        PRINT(0xFFFFFF00, 0x000000, "[THREAD] Unblocked TID=%u\n", tid);
    }
}

void thread_exit(void) {
    if (!current_thread) return;
    
    PRINT(0xFFFFFF00, 0x000000, "[THREAD] Exiting TID=%u\n", current_thread->tid);
    
    current_thread->state = THREAD_STATE_TERMINATED;
    current_thread->used = 0;
    
    if (current_thread->stack_base) {
        kfree(current_thread->stack_base);
        current_thread->stack_base = NULL;
    }
    
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
    schedule();
    
    while (1) __asm__ volatile("hlt");
}

__asm__(
".global switch_to_thread\n"
"switch_to_thread:\n"
"    mov %rsp, 0(%rdi)\n"
"    mov %rbp, 8(%rdi)\n"
"    mov %rbx, 16(%rdi)\n"
"    mov %r12, 24(%rdi)\n"
"    mov %r13, 32(%rdi)\n"
"    mov %r14, 40(%rdi)\n"
"    mov %r15, 48(%rdi)\n"
"    lea 1f(%rip), %rax\n"
"    mov %rax, 56(%rdi)\n"
"    mov 0(%rsi), %rsp\n"
"    mov 8(%rsi), %rbp\n"
"    mov 16(%rsi), %rbx\n"
"    mov 24(%rsi), %r12\n"
"    mov 32(%rsi), %r13\n"
"    mov 40(%rsi), %r14\n"
"    mov 48(%rsi), %r15\n"
"    jmp *56(%rsi)\n"
"1:\n"
"    ret\n"
);