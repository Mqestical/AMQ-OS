
#include "process.h"
#include "memory.h"
#include "print.h"
#include "string_helpers.h"
#include "TSS.h"


thread_t thread_table[MAX_THREADS_GLOBAL];


static thread_t *current_thread = NULL;
static thread_t *idle_thread = NULL;
static uint32_t next_tid = 1;
static volatile int scheduler_enabled = 0;
static volatile int in_scheduler = 0;


static thread_t *ready_queue_head = NULL;
static thread_t *ready_queue_tail = NULL;


int get_scheduler_enabled(void) {
    return scheduler_enabled;
}

void debug_context_switch(void) {
    thread_t *current = get_current_thread();

    if (!current) {
        PRINT(YELLOW, BLACK, "[DEBUG] No current thread\n");
        return;
    }

    PRINT(CYAN, BLACK, "\n=== Context Switch Debug ===\n");
    PRINT(WHITE, BLACK, "Current thread: TID=%u\n", current->tid);
    PRINT(WHITE, BLACK, "  State: %d\n", current->state);
    PRINT(WHITE, BLACK, "  RSP: 0x%llx\n", current->context.rsp);
    PRINT(WHITE, BLACK, "  RIP: 0x%llx\n", current->context.rip);
    PRINT(WHITE, BLACK, "  Stack base: 0x%llx\n", (uint64_t)current->stack_base);
    PRINT(WHITE, BLACK, "  Stack top:  0x%llx\n",
          (uint64_t)current->stack_base + current->stack_size);


    uint64_t stack_start = (uint64_t)current->stack_base;
    uint64_t stack_end = stack_start + current->stack_size;

    if (current->context.rsp >= stack_start && current->context.rsp < stack_end) {
        PRINT(GREEN, BLACK, "  RSP within stack\n");
    } else {
        PRINT(RED, BLACK, "RSP OUTSIDE STACK!\n");
    }


    if (ready_queue_head) {
        thread_t *next = ready_queue_head;
        PRINT(WHITE, BLACK, "\nNext thread: TID=%u\n", next->tid);
        PRINT(WHITE, BLACK, "  RSP: 0x%llx\n", next->context.rsp);
        PRINT(WHITE, BLACK, "  RIP: 0x%llx\n", next->context.rip);

        stack_start = (uint64_t)next->stack_base;
        stack_end = stack_start + next->stack_size;

        if (next->context.rsp >= stack_start && next->context.rsp < stack_end) {
            PRINT(GREEN, BLACK, "  RSP within stack\n");
        } else {
            PRINT(RED, BLACK, " â€” RSP OUTSIDE STACK!\n");
        }


        PRINT(WHITE, BLACK, "\n  Stack at RSP:\n");
        uint64_t *sp = (uint64_t*)next->context.rsp;
        for (int i = 0; i < 8; i++) {
            PRINT(WHITE, BLACK, "    [RSP+%d] = 0x%llx\n", i * 8, sp[i]);
        }
    }
}

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


    if (!current_thread && ready_queue_head) {
        PRINT(YELLOW, BLACK, "[SCHED] No current thread, forcing initial schedule...\n");
        schedule();
    }
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

    __asm__ volatile(
        "mov $0x10, %%rax\n"
        "mov %%rax, %%ds\n"
        "mov %%rax, %%es\n"
        "mov %%rax, %%ss\n"
        ::: "rax", "memory"
    );

    thread_t *current = current_thread;

    if (!current) {
        PRINT(YELLOW, BLACK, "[THREAD] No current thread in wrapper!\n");
        while(1) __asm__ volatile("hlt");
    }

    PRINT(GREEN, BLACK, "[THREAD] TID=%u started execution\n", current->tid);


    __asm__ volatile("sti");



    __asm__ volatile(
        "and $-16, %%rsp\n"
        "sub $8, %%rsp\n"
        ::: "rsp", "memory"
    );

    void (*entry)(void) = (void (*)(void))current->entry_point;
    if (entry) {
        entry();
    }

    PRINT(WHITE, BLACK, "[THREAD] TID=%u exited normally\n", current->tid);
    thread_exit();
}

static void setup_thread_context(thread_t *thread, void (*entry)(void)) {

    for (int i = 0; i < sizeof(cpu_context_t); i++) {
        ((uint8_t*)&thread->context)[i] = 0;
    }


    uint64_t stack_top = (uint64_t)thread->stack_base + thread->stack_size;
    stack_top &= ~0xFULL;









    uint64_t *stack = (uint64_t*)stack_top;



    stack--;
    *stack = (uint64_t)thread_wrapper;

    stack--;
    *stack = 0;

    stack--;
    *stack = 0;

    stack--;
    *stack = 0;

    stack--;
    *stack = 0;

    stack--;
    *stack = 0;

    stack--;
    *stack = 0;

    stack--;
    *stack = 0x202;


    thread->context.rsp = (uint64_t)stack;
    thread->context.rip = (uint64_t)thread_wrapper;
    thread->context.rflags = 0x202;
    thread->context.cs = 0x08;
    thread->context.ss = 0x10;
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


    thread->stack_size = stack_size;
    thread->stack_base = kmalloc(stack_size);
    if (!thread->stack_base) {
        PRINT(YELLOW, BLACK, "[THREAD] Stack allocation failed\n");
        return -1;
    }


    for (uint32_t i = 0; i < stack_size; i++) {
        ((uint8_t*)thread->stack_base)[i] = 0xCC;
    }


    thread->tid = next_tid++;
    thread->parent = proc;
    thread->state = THREAD_STATE_READY;
    thread->used = 1;
    thread->next = NULL;
    thread->private_data = NULL;
    thread->entry_point = (uint64_t)entry_point;


    setup_thread_context(thread, entry_point);


    thread->sched.runtime = runtime;
    thread->sched.deadline = deadline;
    thread->sched.period = period;
    thread->sched.absolute_deadline = 0;
    thread->sched.remaining_runtime = runtime;
    thread->last_scheduled = 0;


    proc->threads[proc->thread_count++] = thread;


    ready_queue_add(thread);

    PRINT(MAGENTA, BLACK, "[THREAD] Created TID=%u for PID=%u (entry=0x%llx)\n",
          thread->tid, proc->pid, thread->entry_point);
    PRINT(CYAN, BLACK, "[THREAD] TID=%u added, ready_head=%u\n",
      thread->tid, ready_queue_head ? ready_queue_head->tid : 0);

    if (scheduler_enabled && !current_thread) {
        PRINT(YELLOW, BLACK, "[THREAD] Scheduler enabled, auto-starting first thread\n");
        schedule();
    }

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
        return;
    }

    thread->state = THREAD_STATE_BLOCKED;
    ready_queue_remove(thread);

    PRINT(WHITE, BLACK, "[THREAD] Blocked TID=%u\n", tid);


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
        return;
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
            PRINT(WHITE, BLACK, "[THREAD] Process %u terminated (no threads)\n", proc->pid);
        }
    }

    current_thread = NULL;


    schedule();


    PRINT(YELLOW, BLACK, "[FATAL] Thread exit returned!\n");
    while(1) __asm__ volatile("hlt");
}

void thread_yield(void) {
    if (!scheduler_enabled) return;
    if (!current_thread) {

        if (ready_queue_head) {
            schedule();
        }
        return;
    }

    schedule();
}

extern void switch_to_thread(cpu_context_t *old_ctx, cpu_context_t *new_ctx);
void schedule(void) {
    if (!scheduler_enabled) {
        PRINT(RED, BLACK, "[SCHED] SCHEDULER DISABLED!\n");
        return;
    }

    if (in_scheduler) return;
    in_scheduler = 1;

    thread_t *prev = current_thread;


    if (prev && prev->state == THREAD_STATE_RUNNING) {
        prev->state = THREAD_STATE_READY;
        ready_queue_add(prev);
    }


    thread_t *next = ready_queue_head;


    if (!next) {
        PRINT(YELLOW, BLACK, "[SCHED] No ready threads - staying in current\n");
        in_scheduler = 0;


        if (prev && prev->state == THREAD_STATE_BLOCKED) {
            PRINT(RED, BLACK, "[SCHED] DEADLOCK: Current blocked, no ready threads!\n");
            PRINT(YELLOW, BLACK, "[SCHED] System will halt until interrupt unblocks a thread\n");


            current_thread = NULL;


            __asm__ volatile("sti; hlt");


            in_scheduler = 0;
            schedule();
        }
        return;
    }


    ready_queue_head = next->next;
    if (ready_queue_tail == next) {
        ready_queue_tail = NULL;
    }
    next->next = NULL;
    next->state = THREAD_STATE_RUNNING;


    if (!prev) {
        current_thread = next;
        in_scheduler = 0;

        PRINT(MAGENTA, BLACK, "[SCHED] Starting first thread TID=%u\n", next->tid);

        uint64_t new_rsp = next->context.rsp;

        __asm__ volatile(
            "mov %0, %%rsp\n"
            "mov $0x10, %%rax\n"
            "mov %%rax, %%ds\n"
            "mov %%rax, %%es\n"
            "mov %%rax, %%ss\n"
            "popfq\n"
            "pop %%rbx\n"
            "pop %%rbp\n"
            "pop %%r12\n"
            "pop %%r13\n"
            "pop %%r14\n"
            "pop %%r15\n"
            "sti\n"
            "ret\n"
            :
            : "r"(new_rsp)
            : "memory", "rax"
        );
    }


    if (prev == next) {
        in_scheduler = 0;
        return;
    }

    current_thread = next;
    in_scheduler = 0;


    switch_to_thread(&prev->context, &next->context);
    __asm__ volatile("sti");
}
void scheduler_tick(void) {
    if (!scheduler_enabled) return;


    if (in_scheduler) return;



    if (ready_queue_head) {
        schedule();
    }
}

extern void switch_to_thread(cpu_context_t *old_ctx, cpu_context_t *new_ctx);

__asm__(
".global switch_to_thread\n"
"switch_to_thread:\n"
"    # RDI = old context, RSI = new context\n"
"    \n"
"    # Save old context (callee-saved registers)\n"
"    push %r15\n"
"    push %r14\n"
"    push %r13\n"
"    push %r12\n"
"    push %rbp\n"
"    push %rbx\n"
"    pushfq\n"
"    \n"
"    # Save old RSP to context->rsp (offset 0)\n"
"    mov %rsp, 0(%rdi)\n"
"    \n"
"    # Load new RSP from context->rsp (offset 0)\n"
"    mov 0(%rsi), %rsp\n"
"    \n"
"    # Restore new context\n"
"    popfq\n"
"    pop %rbx\n"
"    pop %rbp\n"
"    pop %r12\n"
"    pop %r13\n"
"    pop %r14\n"
"    pop %r15\n"
"    \n"
"    # Return to new thread\n"
"    ret\n"
);