
#ifndef FG_H
#define FG_H

#include <stdint.h>

#define MAX_JOBS 32

typedef enum {
    JOB_RUNNING = 0,
    JOB_STOPPED = 1,
    JOB_SLEEPING = 2,
    JOB_DONE = 3
} job_state_t;

typedef struct {
    int used;
    int job_id;
    uint32_t pid;
    uint32_t tid;
    job_state_t state;
    int is_background;
    uint64_t sleep_until;
    char command[256];
} job_t;

typedef struct {
    int job_id;
    char command[256];
} cmd_thread_data_t;

extern cmd_thread_data_t bg_thread_data[MAX_JOBS];

void jobs_init(void);
void jobs_set_active(int active);
int add_fg_job(const char *command, uint32_t pid, uint32_t tid);
int add_bg_job(const char *command, uint32_t pid, uint32_t tid);
void remove_job(int job_id);
void list_jobs(void);
void list_bg_jobs(void);
job_t* get_job(int job_id);
void update_jobs(void);
void scheduler_enable(void);
void update_jobs_safe(void);

#endif
