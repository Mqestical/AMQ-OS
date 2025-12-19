// fg.c - Fixed background job management

#include "fg.h"
#include "print.h"
#include "process.h"
#include "sleep.h"
#include "string_helpers.h"
#include "vfs.h"
// Job table
job_t job_table[MAX_JOBS];  // Remove static - needs to be visible
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
    if (!jobs_enabled) {
        PRINT(YELLOW, BLACK, "[JOBS] Job system not enabled\n");
        return -1;
    }
    
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
    
    PRINT(WHITE, BLACK, "[%d] %d (TID %u)\n", job->job_id, pid, tid);
    
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
            thread_t *thread = get_thread(job->tid);
            
            const char *state_str;
            const char *detail_str = "";
            
            if (!thread || thread->state == THREAD_STATE_TERMINATED) {
                state_str = "Done";
            } else {
                switch (job->state) {
                    case JOB_RUNNING:
                        state_str = "Running";
                        if (thread->state == THREAD_STATE_READY) {
                            detail_str = " (ready)";
                        } else if (thread->state == THREAD_STATE_BLOCKED) {
                            detail_str = " (blocked)";
                        }
                        break;
                    case JOB_STOPPED:
                        state_str = "Stopped";
                        break;
                    case JOB_SLEEPING:
                        state_str = "Sleeping";
                        uint64_t current = get_uptime_ms();
                        if (job->sleep_until > current) {
                            uint64_t remaining = (job->sleep_until - current) / 1000;
                            static char detail_buf[32];
                            int idx = 0;
                            detail_buf[idx++] = ' ';
                            detail_buf[idx++] = '(';
                            if (remaining == 0) {
                                detail_buf[idx++] = '<';
                                detail_buf[idx++] = '1';
                            } else {
                                char digits[20];
                                int d = 0;
                                uint64_t n = remaining;
                                while (n > 0) {
                                    digits[d++] = '0' + (n % 10);
                                    n /= 10;
                                }
                                for (int j = d - 1; j >= 0; j--) {
                                    detail_buf[idx++] = digits[j];
                                }
                            }
                            detail_buf[idx++] = 's';
                            detail_buf[idx++] = ')';
                            detail_buf[idx] = '\0';
                            detail_str = detail_buf;
                        }
                        break;
                    default:
                        state_str = "Unknown";
                        break;
                }
            }
            
            PRINT(WHITE, BLACK, "[%d]  %s%-20s%s%-30s\n",
                  job->job_id, 
                  job->is_background ? "" : "(fg) ",
                  state_str,
                  detail_str,
                  job->command);
            count++;
        }
    }
    
    if (count == 0) {
        PRINT(WHITE, BLACK, "(No jobs)\n");
    }
}

// Safe version for interrupt context - no printing
void update_jobs_safe(void) {
    if (!jobs_enabled) return;
    
    uint64_t current_time_ms = get_uptime_ms();
    
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!job_table[i].used) continue;
        
        job_t *job = &job_table[i];
        thread_t *thread = get_thread(job->tid);
        
        // Check if thread terminated
        if (!thread || thread->state == THREAD_STATE_TERMINATED) {
            job->used = 0;
            job->state = JOB_DONE;
            continue;
        }
        
        // Handle sleeping jobs
        if (job->state == JOB_SLEEPING && job->sleep_until > 0) {
            if (current_time_ms >= job->sleep_until) {
                thread_unblock(job->tid);
                job->state = JOB_RUNNING;
                job->sleep_until = 0;
            }
            continue;
        }
        
        // Update job state based on thread state
        switch (thread->state) {
            case THREAD_STATE_RUNNING:
            case THREAD_STATE_READY:
                if (job->state != JOB_RUNNING) {
                    job->state = JOB_RUNNING;
                }
                break;
                
            case THREAD_STATE_BLOCKED:
                if (job->sleep_until > 0) {
                    if (job->state != JOB_SLEEPING) {
                        job->state = JOB_SLEEPING;
                    }
                } else {
                    if (job->state != JOB_STOPPED) {
                        job->state = JOB_STOPPED;
                    }
                }
                break;
                
            case THREAD_STATE_TERMINATED:
                // Will be caught in next iteration
                break;
        }
    }
}

void update_jobs(void) {
    if (!jobs_enabled) return;
    
    uint64_t current_time_ms = get_uptime_ms();
    static uint64_t last_check_ms = 0;
    
    // Only print debug every second to avoid spam
    int should_debug = (current_time_ms - last_check_ms) > 1000;
    if (should_debug) {
        last_check_ms = current_time_ms;
    }
    
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!job_table[i].used) continue;
        
        job_t *job = &job_table[i];
        thread_t *thread = get_thread(job->tid);
        
        // Check if thread terminated
        if (!thread || thread->state == THREAD_STATE_TERMINATED) {
            if (job->is_background && job->state != JOB_DONE) {
                PRINT(MAGENTA, BLACK, "\n[%d]+ Done                    %s\n",
                      job->job_id, job->command);
                PRINT(GREEN, BLACK, "%s> ", vfs_get_cwd_path());
            }
            job->used = 0;
            job->state = JOB_DONE;
            continue;
        }
        
        // Handle sleeping jobs
        if (job->state == JOB_SLEEPING && job->sleep_until > 0) {
            if (current_time_ms >= job->sleep_until) {
                if (should_debug) {
                    PRINT(WHITE, BLACK, "[JOB %d] Waking up (current=%llu, target=%llu)\n", 
                          job->job_id, current_time_ms, job->sleep_until);
                }
                
                thread_unblock(job->tid);
                job->state = JOB_RUNNING;
                job->sleep_until = 0;
                
                if (job->is_background) {
                    PRINT(MAGENTA, BLACK, "\n[%d]  Awake                    %s\n",
                          job->job_id, job->command);
                    PRINT(GREEN, BLACK, "%s> ", vfs_get_cwd_path());
                }
            }
            continue;
        }
        
        // Update job state based on thread state
        switch (thread->state) {
            case THREAD_STATE_RUNNING:
            case THREAD_STATE_READY:
                if (job->state != JOB_RUNNING) {
                    job->state = JOB_RUNNING;
                }
                break;
                
            case THREAD_STATE_BLOCKED:
                if (job->sleep_until > 0) {
                    if (job->state != JOB_SLEEPING) {
                        job->state = JOB_SLEEPING;
                    }
                } else {
                    if (job->state != JOB_STOPPED) {
                        job->state = JOB_STOPPED;
                    }
                }
                break;
                
            case THREAD_STATE_TERMINATED:
                // Will be caught in next iteration
                break;
        }
    }
}