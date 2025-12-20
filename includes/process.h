#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>

#define MAX_PROCESSES 64
#define MAX_THREADS_PER_PROCESS 16
#define MAX_THREADS_GLOBAL 256

// Thread states
typedef enum {
    THREAD_STATE_READY,
    THREAD_STATE_RUNNING,
    THREAD_STATE_BLOCKED,
    THREAD_STATE_TERMINATED
} thread_state_t;

// Process states
typedef enum {
    PROCESS_STATE_READY,
    PROCESS_STATE_RUNNING,
    PROCESS_STATE_BLOCKED,
    PROCESS_STATE_TERMINATED
} process_state_t;

// CPU context for context switching
typedef struct {
    uint64_t rsp;      // Stack pointer (must be first for asm)
    uint64_t rbp;
    uint64_t rbx;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rip;      // Return address
    uint64_t rflags;
    uint64_t cs;
    uint64_t ss;
} cpu_context_t;

// Scheduling parameters
typedef struct {
    uint64_t runtime;
    uint64_t deadline;
    uint64_t period;
    uint64_t absolute_deadline;
    uint64_t remaining_runtime;
} deadline_params_t;

struct process_t;

// Thread structure
typedef struct thread_t {
    uint32_t tid;
    struct process_t *parent;
    thread_state_t state;
    cpu_context_t context;
    void *stack_base;
    uint32_t stack_size;
    uint64_t stack_pointer;  // Deprecated, use context.rsp
    deadline_params_t sched;
    uint64_t last_scheduled;
    struct thread_t *next;   // For ready queue
    int used;
    void *private_data;
    uint64_t entry_point;
    uint64_t sleep_until;
} thread_t;

// Process structure
typedef struct process_t {
    uint32_t pid;
    uint64_t memory_space;
    process_state_t state;
    thread_t *threads[MAX_THREADS_PER_PROCESS];
    uint32_t thread_count;
    uint8_t used;
    char name[64];
} process_t;

// Process management
void process_init(void);
int process_create(const char *name, uint64_t memory_space);
process_t* get_process(uint32_t pid);
void print_process_table(void);

// Thread management
int thread_create(uint32_t pid, void (*entry_point)(void), uint32_t stack_size, 
                  uint64_t runtime, uint64_t deadline, uint64_t period);
thread_t* get_thread(uint32_t tid);
thread_t* get_current_thread(void);
void thread_exit(void);
void thread_yield(void);
void thread_block(uint32_t tid);
void thread_unblock(uint32_t tid);

// Scheduler
void scheduler_init(void);
void scheduler_enable(void);
void scheduler_disable(void);
void scheduler_tick(void);
void schedule(void);

// Kernel threads
void init_kernel_threads(void);

// Global tables
extern process_t process_table[MAX_PROCESSES];
extern thread_t thread_table[MAX_THREADS_GLOBAL];

#endif // PROCESS_H