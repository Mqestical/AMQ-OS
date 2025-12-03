#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include <sched.h>
#define MAX_PROCESSES 64
#define MAX_THREADS_PER_PROCESS 16
#define MAX_THREADS_GLOBAL 256

typedef enum {
    THREAD_STATE_READY,
    THREAD_STATE_RUNNING,
    THREAD_STATE_BLOCKED,
    THREAD_STATE_TERMINATED
} thread_state_t;

typedef enum {
    PROCESS_STATE_READY,
    PROCESS_STATE_RUNNING,
    PROCESS_STATE_BLOCKED,
    PROCESS_STATE_TERMINATED
} process_state_t;

typedef struct {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp, rsp;
    uint64_t r8, r9, r10, r11;
    uint64_t r12, r13, r14, r15;
    uint64_t rip;
    uint64_t rflags;
    uint64_t cs, ss;
} cpu_context_t;

typedef struct {
    uint64_t runtime;
    uint64_t deadline;
    uint64_t period;
    uint64_t absolute_deadline;
    uint64_t remaining_runtime;
} deadline_params_t;

struct process_t;


typedef struct thread_t {
    uint32_t tid;
    struct process_t *parent;
    thread_state_t state;
    cpu_context_t context;
    uint64_t stack_pointer;
    void *stack_base;
    uint32_t stack_size;
    deadline_params_t sched;
    uint64_t last_scheduled;
    struct thread_t *next;
    int used;
    void *private_data;
    uint64_t entry_point;
} thread_t;
typedef struct process_t {
    uint32_t pid;
    uint64_t memory_space;
    process_state_t state;
    thread_t *threads[MAX_THREADS_PER_PROCESS];
    uint32_t thread_count;
    uint8_t used;
    char name[64];
} process_t;

void process_init(void);
int process_create(const char *name, uint64_t memory_space);
process_t* get_process(uint32_t pid);
void print_process_table(void);

void init_kernel_threads(void);
int thread_create(uint32_t pid, void (*entry_point)(void), uint32_t stack_size, 
                  uint64_t runtime, uint64_t deadline, uint64_t period);
thread_t* get_thread(uint32_t tid);
thread_t* get_current_thread(void);
void thread_exit(void);
void thread_yield(void);
void thread_block(uint32_t tid);
void thread_unblock(uint32_t tid);

void scheduler_init(void);
void scheduler_tick(void);
void schedule(void);
void insert_ready_queue(thread_t *thread);
void remove_ready_queue(thread_t *thread);

extern process_t process_table[MAX_PROCESSES];
extern thread_t thread_table[MAX_THREADS_GLOBAL];
void start_scheduler(void);
#endif
