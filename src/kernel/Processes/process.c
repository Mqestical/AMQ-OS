#include "process.h"
#include "memory.h"
#include "print.h"
#include "string_helpers.h"

process_t process_table[MAX_PROCESSES];

static uint32_t next_pid = 1;

void process_init(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_table[i].used = 0;
        process_table[i].pid = 0;
        process_table[i].thread_count = 0;
        process_table[i].state = PROCESS_STATE_TERMINATED;
        process_table[i].memory_space = 0;
        
        for (int j = 0; j < MAX_THREADS_PER_PROCESS; j++) {
            process_table[i].threads[j] = NULL;
        }
        
        for (int j = 0; j < 64; j++) {
            process_table[i].name[j] = 0;
        }
    }
    
    PRINT(MAGENTA, BLACK, "[PROCESS] Process management initialized\n");
}

static int find_free_process(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (!process_table[i].used) {
            return i;
        }
    }
    return -1;
}

int process_create(const char *name, uint64_t memory_space) {
    int idx = find_free_process();
    if (idx < 0) {
        PRINT(YELLOW, BLACK, "[PROCESS] No free process slots\n");
        return -1;
    }
    
    process_t *proc = &process_table[idx];
    proc->pid = next_pid++;
    proc->memory_space = memory_space;
    proc->state = PROCESS_STATE_READY;
    proc->thread_count = 0;
    proc->used = 1;
    
    int i = 0;
    while (name[i] && i < 63) {
        proc->name[i] = name[i];
        i++;
    }
    proc->name[i] = '\0';
    
    PRINT(MAGENTA, BLACK, "[PROCESS] Created process '%s' (PID=%u, Memory=0x%llX)\n",
          proc->name, proc->pid, memory_space);
    
    return proc->pid;
}

process_t* get_process(uint32_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].used && process_table[i].pid == pid) {
            return &process_table[i];
        }
    }
    return NULL;
}

void print_process_table(void) {
    PRINT(WHITE, BLACK, "\n=== Process Table ===\n");
    
    int count = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].used) {
            process_t *p = &process_table[i];
            
            char *state_str;
            if (p->state == PROCESS_STATE_RUNNING) state_str = "RUNNING";
            else if (p->state == PROCESS_STATE_READY) state_str = "READY";
            else if (p->state == PROCESS_STATE_BLOCKED) state_str = "BLOCKED";
            else state_str = "TERMINATED";
            
            PRINT(MAGENTA, BLACK, "PID=%u | '%s' | Memory=0x%llX | State=%s | Threads=%u\n",
                  p->pid, p->name, p->memory_space, state_str, p->thread_count);
            
            for (uint32_t j = 0; j < p->thread_count; j++) {
                thread_t *t = p->threads[j];
                if (t) {
                    char *tstate_str;
                    if (t->state == THREAD_STATE_RUNNING) tstate_str = "RUNNING";
                    else if (t->state == THREAD_STATE_READY) tstate_str = "READY";
                    else if (t->state == THREAD_STATE_BLOCKED) tstate_str = "BLOCKED";
                    else tstate_str = "TERMINATED";
                    
                    PRINT(WHITE, BLACK, "  â””â”€ TID=%u | %s | Deadline=%llu ns\n",
                          t->tid, tstate_str, t->sched.absolute_deadline);
                }
            }
            count++;
        }
    }
    
    if (count == 0) {
        PRINT(WHITE, BLACK, "(No processes)\n");
    }
}