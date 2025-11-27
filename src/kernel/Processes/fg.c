#include "fg.h"
#include "print.h"
#include "process.h"
#include "string_helpers.h"

static job_t fg_table[MAX_JOBS];
static int next_job_id = 1;
static uint64_t system_time_ms = 0;
static int jobs_active = 0;  // NEW: Flag to enable/disable job tracking

void jobs_init(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        fg_table[i].used = 0;
        fg_table[i].job_id = 0;
        fg_table[i].pid = 0;
        fg_table[i].tid = 0;
        fg_table[i].state = JOB_DONE;
        fg_table[i].is_background = 0;
        fg_table[i].sleep_until = 0;
        fg_table[i].command[0] = '\0';
    }
    
    jobs_active = 0;  // Start disabled
    
    PRINT(0xFF00FF00, 0x000000, "[FG] Job control initialized (INACTIVE)\n");
}

// NEW: Enable/disable job tracking
void jobs_set_active(int active) {
    jobs_active = active;
    if (active) {
        PRINT(0xFF00FF00, 0x000000, "[FG] Job tracking ENABLED\n");
    } else {
        PRINT(0xFFFFFF00, 0x000000, "[FG] Job tracking DISABLED\n");
    }
}

static int find_free_slot(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!fg_table[i].used) {
            return i;
        }
    }
    return -1;
}

int add_fg_job(const char *command, uint32_t pid, uint32_t tid) {
    int idx = find_free_slot();
    if (idx < 0) {
        PRINT(0xFFFF0000, 0x000000, "[FG] No free job slots\n");
        return -1;
    }
    
    job_t *job = &fg_table[idx];
    job->job_id = next_job_id++;
    job->pid = pid;
    job->tid = tid;
    job->state = JOB_RUNNING;
    job->is_background = 0;
    job->used = 1;
    job->sleep_until = 0;
    
    int i = 0;
    while (command[i] && i < 255) {
        job->command[i] = command[i];
        i++;
    }
    job->command[i] = '\0';
    
    return job->job_id;
}

int add_bg_job(const char *command, uint32_t pid, uint32_t tid) {
    int idx = find_free_slot();
    if (idx < 0) {
        PRINT(0xFFFF0000, 0x000000, "[BG] No free job slots\n");
        return -1;
    }
    
    job_t *job = &fg_table[idx];
    job->job_id = next_job_id++;
    job->pid = pid;
    job->tid = tid;
    job->state = JOB_RUNNING;
    job->is_background = 1;
    job->used = 1;
    job->sleep_until = 0;
    
    int i = 0;
    while (command[i] && i < 255) {
        job->command[i] = command[i];
        i++;
    }
    job->command[i] = '\0';
    
    PRINT(0xFFFFFF00, 0x000000, "[%d] %d\n", job->job_id, job->pid);
    
    return job->job_id;
}

void remove_job(int job_id) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (fg_table[i].used && fg_table[i].job_id == job_id) {
            PRINT(0xFF00FF00, 0x000000, "[%d]+ Done                    %s\n",
                  job_id, fg_table[i].command);
            
            fg_table[i].used = 0;
            fg_table[i].state = JOB_DONE;
            return;
        }
    }
}

void list_jobs(void) {
    PRINT(0xFFFFFFFF, 0x000000, "\nJobs:\n");
    
    int count = 0;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (fg_table[i].used) {
            job_t *job = &fg_table[i];
            
            char *state_str;
            if (job->state == JOB_RUNNING) state_str = "Running";
            else if (job->state == JOB_STOPPED) state_str = "Stopped";
            else if (job->state == JOB_SLEEPING) state_str = "Sleeping";
            else state_str = "Done";
            
            if (job->is_background) {
                PRINT(0xFFFFFF00, 0x000000, "[%d]  %s                    %s\n",
                      job->job_id, state_str, job->command);
            } else {
                PRINT(0xFFFFFF00, 0x000000, "[%d]  %s (fg)               %s\n",
                      job->job_id, state_str, job->command);
            }
            count++;
        }
    }
    
    if (count == 0) {
        PRINT(0xFFFFFF00, 0x000000, "(No jobs)\n");
    }
}

job_t* get_job(int job_id) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (fg_table[i].used && fg_table[i].job_id == job_id) {
            return &fg_table[i];
        }
    }
    return NULL;
}

void update_jobs_safe(void) {
    // Don't run if not active (during early boot)
    if (!jobs_active) {
        return;
    }
    
    // Extra safety: Check if we're initialized
    extern thread_t thread_table[MAX_THREADS_GLOBAL];
    if (!thread_table) {
        return;
    }
    
    // Now call the actual update function
    update_jobs();
}


// CRITICAL: This is called from IRQ handler, must be SAFE
void update_jobs(void) {
    // Don't run if not active (during early boot)
    if (!jobs_active) {
        return;
    }
    
    system_time_ms++;
    
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!fg_table[i].used) continue;
        
        job_t *job = &fg_table[i];
        
        // SAFETY: Check thread table bounds
        if (job->tid == 0 || job->tid >= MAX_THREADS_GLOBAL) {
            job->used = 0;
            job->state = JOB_DONE;
            continue;
        }
        
        // Check if thread still exists
        thread_t *thread = get_thread(job->tid);
        
        // If thread is gone or terminated, remove the job
        if (!thread || thread->state == THREAD_STATE_TERMINATED) {
            if (job->is_background && job->state != JOB_DONE) {
                PRINT(0xFF00FF00, 0x000000, "\n[%d]+ Done                    %s\n",
                      job->job_id, job->command);
            }
            
            job->used = 0;
            job->state = JOB_DONE;
            continue;
        }
        
        // Wake up sleeping jobs
        if (job->state == JOB_SLEEPING) {
            if (system_time_ms >= job->sleep_until) {
                thread_unblock(job->tid);
                job->state = JOB_RUNNING;
                
                if (job->is_background) {
                    PRINT(0xFF00FF00, 0x000000, "\n[%d]  Woke up from sleep       %s\n",
                          job->job_id, job->command);
                }
            }
        }
    }
}

void list_bg_jobs(void) {
    PRINT(0xFFFFFFFF, 0x000000, "\nBackground Jobs:\n");
    
    int count = 0;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (fg_table[i].used && fg_table[i].is_background) {
            job_t *job = &fg_table[i];
            
            char *state_str;
            if (job->state == JOB_RUNNING) state_str = "Running";
            else if (job->state == JOB_STOPPED) state_str = "Stopped";
            else if (job->state == JOB_SLEEPING) state_str = "Sleeping";
            else state_str = "Done";
            
            PRINT(0xFFFFFF00, 0x000000, "[%d]  %s                    %s\n",
                  job->job_id, state_str, job->command);
            count++;
        }
    }
    
    if (count == 0) {
        PRINT(0xFFFFFF00, 0x000000, "(No background jobs)\n");
    }
}
