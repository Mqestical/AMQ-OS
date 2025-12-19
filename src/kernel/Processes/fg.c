// fg.c - Background job management rewrite

#include "fg.h"
#include "print.h"
#include "process.h"
#include "sleep.h"
#include "string_helpers.h"

// Job table
static job_t job_table[MAX_JOBS];
static int next_job_id = 1;
static int jobs_enabled = 0;

void jobs_init(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        job_table[i].used = 0;
        job_table[i].job_id = 0;
        job_table[i].pid = 0;
        job_table[i].tid = 0;
        job_table[i].state = JOB_DONE;
        job_table[i].is_background = 0;
        job_table[i].sleep_until = 0;
        job_table[i].command[0] = '\0';
    }
    
    jobs_enabled = 0;
    PRINT(MAGENTA, BLACK, "[JOBS] Job system initialized\n");
}

void jobs_set_active(int active) {
    jobs_enabled = active;
    if (active) {
        PRINT(MAGENTA, BLACK, "[JOBS] Job tracking ENABLED\n");
    } else {
        PRINT(WHITE, BLACK, "[JOBS] Job tracking DISABLED\n");
    }
}

static int find_free_job_slot(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!job_table[i].used) {
            return i;
        }
    }
    return -1;
}

int add_fg_job(const char *command, uint32_t pid, uint32_t tid) {
    int slot = find_free_job_slot();
    if (slot < 0) {
        PRINT(YELLOW, BLACK, "[JOBS] No free job slots\n");
        return -1;
    }
    
    job_t *job = &job_table[slot];
    job->job_id = next_job_id++;
    job->pid = pid;
    job->tid = tid;
    job->state = JOB_RUNNING;
    job->is_background = 0;
    job->used = 1;
    job->sleep_until = 0;
    
    // Copy command
    int i = 0;
    while (command[i] && i < 255) {
        job->command[i] = command[i];
        i++;
    }
    job->command[i] = '\0';
    
    PRINT(GREEN, BLACK, "[JOBS] Created foreground job %d (TID=%u)\n", 
          job->job_id, tid);
    
    return job->job_id;
}

int add_bg_job(const char *command, uint32_t pid, uint32_t tid) {
    int slot = find_free_job_slot();
    if (slot < 0) {
        PRINT(YELLOW, BLACK, "[JOBS] No free job slots\n");
        return -1;
    }
    
    job_t *job = &job_table[slot];
    job->job_id = next_job_id++;
    job->pid = pid;
    job->tid = tid;
    job->state = JOB_RUNNING;
    job->is_background = 1;
    job->used = 1;
    job->sleep_until = 0;
    
    // Copy command
    int i = 0;
    while (command[i] && i < 255) {
        job->command[i] = command[i];
        i++;
    }
    job->command[i] = '\0';
    
    PRINT(WHITE, BLACK, "[%d] %d\n", job->job_id, pid);
    
    return job->job_id;
}

void remove_job(int job_id) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (job_table[i].used && job_table[i].job_id == job_id) {
            PRINT(MAGENTA, BLACK, "[%d]+ Done                    %s\n",
                  job_id, job_table[i].command);
            
            job_table[i].used = 0;
            job_table[i].state = JOB_DONE;
            return;
        }
    }
}

job_t* get_job(int job_id) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (job_table[i].used && job_table[i].job_id == job_id) {
            return &job_table[i];
        }
    }
    return NULL;
}

void list_jobs(void) {
    PRINT(WHITE, BLACK, "\n=== Jobs ===\n");
    
    int count = 0;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (job_table[i].used) {
            job_t *job = &job_table[i];
            
            const char *state_str;
            switch (job->state) {
                case JOB_RUNNING: state_str = "Running"; break;
                case JOB_STOPPED: state_str = "Stopped"; break;
                case JOB_SLEEPING: state_str = "Sleeping"; break;
                default: state_str = "Done"; break;
            }
            
            PRINT(WHITE, BLACK, "[%d]  %s%-20s%s\n",
                  job->job_id, 
                  job->is_background ? "" : "(fg) ",
                  state_str,
                  job->command);
            count++;
        }
    }
    
    if (count == 0) {
        PRINT(WHITE, BLACK, "(No jobs)\n");
    }
}

void update_jobs(void) {
    if (!jobs_enabled) return;
    
    uint64_t current_time_ms = get_uptime_ms();
    
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!job_table[i].used) continue;
        
        job_t *job = &job_table[i];
        thread_t *thread = get_thread(job->tid);
        
        // Check if thread terminated
        if (!thread || thread->state == THREAD_STATE_TERMINATED) {
            if (job->is_background && job->state != JOB_DONE) {
                PRINT(MAGENTA, BLACK, "\n[%d]+ Done                    %s\n",
                      job->job_id, job->command);
            }
            job->used = 0;
            job->state = JOB_DONE;
            continue;
        }
        
        // Check sleep timeout
        if (job->state == JOB_SLEEPING) {
            if (current_time_ms >= job->sleep_until) {
                PRINT(WHITE, BLACK, "\n[JOB %d] Waking up\n", job->job_id);
                
                thread_unblock(job->tid);
                job->state = JOB_RUNNING;
                job->sleep_until = 0;
                
                if (job->is_background) {
                    PRINT(MAGENTA, BLACK, "[%d]  Resumed                  %s\n",
                          job->job_id, job->command);
                }
            }
        }
    }
}