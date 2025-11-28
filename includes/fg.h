// fg.h - Background/Foreground Job Control Header

#ifndef FG_H
#define FG_H

#include <stdint.h>

#define MAX_JOBS 32

// Job states
typedef enum {
    JOB_RUNNING = 0,
    JOB_STOPPED = 1,
    JOB_SLEEPING = 2,
    JOB_DONE = 3
} job_state_t;

// Job structure
typedef struct {
    int used;
    int job_id;
    uint32_t pid;
    uint32_t tid;
    job_state_t state;
    int is_background;
    uint64_t sleep_until;  // Time in ms when sleeping job should wake
    char command[256];
} job_t;

// Command thread data (passed to background threads)
typedef struct {
    int job_id;
    char command[256];
} cmd_thread_data_t;

// Global background thread data array
extern cmd_thread_data_t bg_thread_data[MAX_JOBS];

// Function declarations
void jobs_init(void);
void jobs_set_active(int active);  // NEW: Enable/disable job tracking
int add_fg_job(const char *command, uint32_t pid, uint32_t tid);
int add_bg_job(const char *command, uint32_t pid, uint32_t tid);
void remove_job(int job_id);
void list_jobs(void);
void list_bg_jobs(void);
job_t* get_job(int job_id);
void update_jobs(void);  // Called from timer interrupt
void scheduler_enable(void);
void update_jobs_safe(void);

#endif // FG_H