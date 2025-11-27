// ============================================================================
// process.h
// ============================================================================
#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include <sched.h>
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

// CPU register context for x86_64
typedef struct {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp, rsp;
    uint64_t r8, r9, r10, r11;
    uint64_t r12, r13, r14, r15;
    uint64_t rip;       // Program counter
    uint64_t rflags;
    uint64_t cs, ss;
} cpu_context_t;

// SCHED_DEADLINE scheduling parameters
typedef struct {
    uint64_t runtime;      // Maximum execution time (ns)
    uint64_t deadline;     // Relative deadline (ns)
    uint64_t period;       // Period (ns)
    uint64_t absolute_deadline;  // Absolute deadline
    uint64_t remaining_runtime;  // Runtime left in current period
} deadline_params_t;

// Forward declaration
struct process_t;


typedef struct thread_t {
    uint32_t tid;                           // Thread ID
    struct process_t *parent;               // Parent process
    thread_state_t state;                   // Current state
    cpu_context_t context;                  // CPU context
    uint64_t stack_pointer;                 // Current stack pointer
    void *stack_base;                       // Stack base address
    uint32_t stack_size;                    // Stack size
    deadline_params_t sched;                   // Scheduling parameters
    uint64_t last_scheduled;                // Last time scheduled
    struct thread_t *next;                  // Next in queue
    int used;                               // Is this slot used?
    void *private_data;                     // Private data (e.g., for job tracking)
    uint64_t entry_point;                   // ADD THIS LINE - Thread entry point function
} thread_t;
// Process Control Block (PCB)
typedef struct process_t {
    uint32_t pid;                    // Process ID
    uint64_t memory_space;           // Memory space address (page table)
    process_state_t state;           // Process state
    thread_t *threads[MAX_THREADS_PER_PROCESS];  // Array of threads
    uint32_t thread_count;           // Number of threads
    uint8_t used;                    // Is this PCB in use?
    char name[64];                   // Process name
} process_t;

// Process functions
void process_init(void);
int process_create(const char *name, uint64_t memory_space);
process_t* get_process(uint32_t pid);
void print_process_table(void);

// Thread functions
void init_kernel_threads(void);
int thread_create(uint32_t pid, void (*entry_point)(void), uint32_t stack_size, 
                  uint64_t runtime, uint64_t deadline, uint64_t period);
thread_t* get_thread(uint32_t tid);
thread_t* get_current_thread(void);
void thread_exit(void);
void thread_yield(void);
void thread_block(uint32_t tid);
void thread_unblock(uint32_t tid);

// Scheduler functions
void scheduler_init(void);
void scheduler_tick(void);
void schedule(void);
void insert_ready_queue(thread_t *thread);
void remove_ready_queue(thread_t *thread);

// External access to global tables
extern process_t process_table[MAX_PROCESSES];
extern thread_t thread_table[MAX_THREADS_GLOBAL];
void start_scheduler(void);  // Activate the scheduler
#endif // PROCESS_H