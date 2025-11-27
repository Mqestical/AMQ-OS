#include <efi.h>
#include <efilib.h>
#include "print.h"
#include "memory.h"
#include "serial.h"
#include "vfs.h"
#include "ata.h"
#include "tinyfs.h"
#include "process.h"
#include "fg.h"
#include "sleep.h"
#include "syscall.h"
#include "string_helpers.h"
#define CURSOR_BLINK_RATE 50000

void bg_command_thread(void);
void bring_to_foreground(int job_id);
void send_to_background(int job_id);
extern volatile uint32_t interrupt_counter;
extern volatile uint8_t last_scancode;
extern volatile uint8_t scancode_write_pos;
extern volatile uint8_t scancode_read_pos;
extern volatile int serial_initialized;
extern void process_keyboard_buffer(void);
extern char* get_input_and_reset(void);
extern int input_available(void);
void test_syscall_interface(void);

void draw_cursor(int visible) {
    if (visible) {
        draw_char(cursor.x, cursor.y, '_', cursor.fg_color, cursor.bg_color);
    } else {
        draw_char(cursor.x, cursor.y, ' ', cursor.fg_color, cursor.bg_color);
    }
}

// String helpers
int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

int strncmp(const char* s1, const char* s2, int n) {
    for (int i = 0; i < n; i++) {
        if (s1[i] != s2[i]) return s1[i] - s2[i];
        if (s1[i] == '\0') return 0;
    }
    return 0;
}

void strcpy_local(char* dest, const char* src) {
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
}

int strlen_local(const char* str) {
    int len = 0;
    while (str[len]) len++;
    return len;
}
static void strcpy_safe_local(char *dest, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

void bg_command_thread(void) {
    thread_t *current = get_current_thread();
    if (!current || !current->private_data) {
        thread_exit();
        return;
    }
    
    cmd_thread_data_t *data = (cmd_thread_data_t*)current->private_data;
    
    char msg[] = "\n[%d] Running: %s\n";
    printk(0xFFFFFF00, 0x000000, msg, data->job_id, data->command);
    
    // Parse and execute the command
    if (strncmp(data->command, "sleep ", 6) == 0) {
        char *arg = data->command + 6;
        uint32_t seconds = 0;
        
        while (*arg >= '0' && *arg <= '9') {
            seconds = seconds * 10 + (*arg - '0');
            arg++;
        }
        
        if (seconds > 0) {
            sleep_seconds(seconds);
        }
    }
    else if (strncmp(data->command, "echo ", 5) == 0) {
        char *text = data->command + 5;
        char fmt[] = "\n[%d] Output: %s\n";
        printk(0xFFFFFF00, 0x000000, fmt, data->job_id, text);
    }
    else {
        char unknown[] = "\n[%d] Unknown command: %s\n";
        printk(0xFFFF0000, 0x000000, unknown, data->job_id, data->command);
    }
    
    // CRITICAL: Mark slot as free BEFORE exiting thread
    data->job_id = 0;
    data->command[0] = '\0';
    
    // Thread exit will trigger job cleanup in update_jobs()
    thread_exit();
}

// ============================================================================
// FIXED: bring_to_foreground - Works without scheduler
// ============================================================================

void bring_to_foreground(int job_id) {
    job_t *job = get_job(job_id);
    if (!job) {
        char err[] = "fg: job %d not found\n";
        printk(0xFFFF0000, 0x000000, err, job_id);
        return;
    }
    
    if (!job->is_background) {
        char err[] = "fg: job %d is already in foreground\n";
        printk(0xFFFFFF00, 0x000000, err, job_id);
        return;
    }
    
    thread_t *thread = get_thread(job->tid);
    if (!thread) {
        char err[] = "fg: thread %u not found for job %d\n";
        printk(0xFFFF0000, 0x000000, err, job->tid, job_id);
        return;
    }
    
    // If sleeping, wake it up
    if (thread->state == THREAD_STATE_BLOCKED) {
        thread_unblock(job->tid);
        job->state = JOB_RUNNING;
        job->sleep_until = 0;
    }
    
    // Move to foreground
    job->is_background = 0;
    
    char msg[] = "fg: job %d (%s) brought to foreground\n";
    printk(0xFF00FF00, 0x000000, msg, job_id, job->command);
    
    // CRITICAL: Without scheduler, we can't actually "wait" for completion
    // Just inform the user and let update_jobs() handle cleanup
    char note[] = "Note: Job will continue running. Use 'jobs' to check status.\n";
    printk(0xFFFFFF00, 0x000000, note);
}

// ============================================================================
// FIXED: send_to_background
// ============================================================================

void send_to_background(int job_id) {
    job_t *job = get_job(job_id);
    if (!job) {
        char err[] = "bg: job %d not found\n";
        printk(0xFFFF0000, 0x000000, err, job_id);
        return;
    }
    
    if (job->is_background) {
        char err[] = "bg: job %d is already in background\n";
        printk(0xFFFFFF00, 0x000000, err, job_id);
        return;
    }
    
    job->is_background = 1;
    
    char msg[] = "bg: job %d (%s) sent to background\n";
    printk(0xFF00FF00, 0x000000, msg, job_id, job->command);
}

void process_command(char* cmd) {
    // Skip empty input
    if (cmd[0] == '\0') return;

    // ========================================================================
    // CHECK FOR BACKGROUND EXECUTION (command ends with &)
    // ========================================================================
    int len = strlen_local(cmd);
    int is_background = 0;
    if (len > 0 && cmd[len-1] == '&') {
        is_background = 1;
        cmd[len-1] = '\0'; // Remove the '&'
        // Trim trailing spaces
        len--;
        while (len > 0 && cmd[len-1] == ' ') {
            cmd[len-1] = '\0';
            len--;
        }
        // Skip if command is now empty
        if (len == 0 || cmd[0] == '\0') {
            char err[] = "Invalid command\n";
            printk(0xFFFF0000, 0x000000, err);
            return;
        }

        char bg_msg[] = "[SHELL] Running in background: %s\n";
        printk(0xFFFFFF00, 0x000000, bg_msg, cmd);

        // Create background job
        // 1. Get/create a process for background jobs
        process_t *proc = get_process(1); // Use init process (PID=1)
        if (!proc) {
            char err[] = "[ERROR] Init process not found\n";
            printk(0xFFFF0000, 0x000000, err);
            return;
        }

        // 2. Find free bg_thread_data slot
        int data_idx = -1;
        for (int i = 0; i < MAX_JOBS; i++) {
            if (bg_thread_data[i].job_id == 0) {
                data_idx = i;
                break;
            }
        }
        if (data_idx < 0) {
            char err[] = "[ERROR] No free background slots\n";
            printk(0xFFFF0000, 0x000000, err);
            return;
        }

        // 3. Copy command to thread data
        strcpy_safe_local(bg_thread_data[data_idx].command, cmd, 256);
        bg_thread_data[data_idx].job_id = -1; // Will be set after thread creation

        // 4. Create thread
        int tid = thread_create(
            proc->pid,          // Parent process
            bg_command_thread,  // Entry point
            8192,               // 8KB stack
            50000000,           // 50ms runtime
            500000000,          // 500ms deadline
            500000000           // 500ms period
        );
        if (tid < 0) {
            char err[] = "[ERROR] Failed to create background thread\n";
            printk(0xFFFF0000, 0x000000, err);
            bg_thread_data[data_idx].job_id = 0; // Free the slot
            return;
        }

        // 5. Set thread private data
        thread_t *thread = get_thread(tid);
        if (thread) {
            thread->private_data = &bg_thread_data[data_idx];
        }

        // 6. Add to background job table
        int job_id = add_bg_job(cmd, proc->pid, tid);
        if (job_id > 0) {
            bg_thread_data[data_idx].job_id = job_id;
        } else {
            char err[] = "[ERROR] Failed to add background job\n";
            printk(0xFFFF0000, 0x000000, err);
            // Thread will still run but won't be tracked
        }

        return; // Don't execute the command in foreground
    }

    // ========================================================================
    // FOREGROUND COMMAND PROCESSING (original commands)
    // ========================================================================
    char cmd1[] = "hello";
    char cmd2[] = "help";
    char cmd3[] = "clear";
    char cmd4[] = "echo ";
    char cmd5[] = "ls ";
    char cmd55[] = "ls";
    char cmd6[] = "cat ";
    char cmd7[] = "touch ";
    char cmd8[] = "mkdir ";
    char cmd9[] = "rm ";
    char cmd10[] = "write ";
    char cmd11[] = "df";
    char cmd12[] = "memstats";
    char cmd13[] = "format";
    char cmd14[] = "cd ";
    char cmd144[] = "cd";
    char cmd15[] = "pwd";
    char cmd16[] = "ps";
    char cmd17[] = "threads";
    char cmd18[] = "jobs";
    char cmd19[] = "fg ";
    char cmd20[] = "bg ";
    char cmd21[] = "sleep ";
    char cmd22[] = "syscalltest";
    // --- Basic commands ---
    if (strcmp(cmd, cmd1) == 0) {
        char msg[] = "Hello from AMQ OS!\n";
        printk(0xFF00FF00, 0x000000, msg);
    }
    else if (strcmp(cmd, cmd2) == 0) {
        char msg1[] = "Available commands:\n";
        char msg2[] = "  hello - Say hello\n";
        char msg3[] = "  clear - Clear screen\n";
        char msg4[] = "  echo <text> - Echo text\n";
        char msg5[] = "  ls [path] - List directory\n";
        char msg6[] = "  cat <file> - Display file\n";
        char msg7[] = "  touch <file> - Create file\n";
        char msg8[] = "  mkdir <dir> - Create directory\n";
        char msg9[] = "  rm <file> - Remove file/dir\n";
        char msg10[] = "  write <file> - Write to file\n";
        char msg11[] = "  df - Filesystem stats\n";
        char msg12[] = "  memstats - Memory stats\n";
        char msg13[] = "  format - Format disk (WARNING: DISK WILL BE WIPED!)\n";
        char msg14[] = "  cd <dir> - Change directory\n";
        char msg15[] = "  pwd - Print working directory\n";
        char msg16[] = "  ps - Show processes\n";
        char msg17[] = "  threads - Show threads\n";
        char msg18[] = "  jobs - List all jobs\n";
        char msg19[] = "  fg <job_id> - Bring to foreground\n";
        char msg20[] = "  bg <job_id> - Send to background\n";
        char msg21[] = "  sleep <sec> - Sleep for seconds\n";
        char msg22[] = "  command & - Run in background\n";
        char msg23[] = "syscalltest";
        printk(0xFFFFFFFF, 0x000000, msg1);
        printk(0xFFFFFFFF, 0x000000, msg2);
        printk(0xFFFFFFFF, 0x000000, msg3);
        printk(0xFFFFFFFF, 0x000000, msg4);
        printk(0xFFFFFFFF, 0x000000, msg5);
        printk(0xFFFFFFFF, 0x000000, msg6);
        printk(0xFFFFFFFF, 0x000000, msg7);
        printk(0xFFFFFFFF, 0x000000, msg8);
        printk(0xFFFFFFFF, 0x000000, msg9);
        printk(0xFFFFFFFF, 0x000000, msg10);
        printk(0xFFFFFFFF, 0x000000, msg11);
        printk(0xFFFFFFFF, 0x000000, msg12);
        printk(0xFFFF0000, 0x000000, msg13);
        printk(0xFFFFFFFF, 0x000000, msg14);
        printk(0xFFFFFFFF, 0x000000, msg15);
        printk(0xFF00FFFF, 0x000000, msg16);
        printk(0xFF00FFFF, 0x000000, msg17);
        printk(0xFF00FFFF, 0x000000, msg18);
        printk(0xFF00FFFF, 0x000000, msg19);
        printk(0xFF00FFFF, 0x000000, msg20);
        printk(0xFF00FFFF, 0x000000, msg21);
        printk(0xFFFFFF00, 0x000000, msg22);
        printk(0xFF00FFFF, 0x000000, msg23);
    }
    else if (strcmp(cmd, cmd3) == 0) {
        ClearScreen(0x000000);
        SetCursorPos(0, 0);
    }
    else if (strncmp(cmd, cmd4, 5) == 0) {
        char* text = cmd + 5;
        char fmt[] = "%s\n";
        printk(0xFFFFFFFF, 0x000000, fmt, text);
    }
    else if (strcmp(cmd, cmd12) == 0) {
        memory_stats();
    }
    // --- Process/Thread commands ---
    else if (strcmp(cmd, cmd16) == 0) {
        print_process_table();
    }
    else if (strcmp(cmd, cmd17) == 0) {
        char header[] = "\n=== Thread Information ===\n";
        printk(0xFFFFFFFF, 0x000000, header);

        thread_t *current = get_current_thread();
        if (current) {
            char msg[] = "Current thread: TID=%u (PID=%u)\n";
            printk(0xFF00FF00, 0x000000, msg, current->tid, current->parent->pid);
        } else {
            char msg[] = "No thread currently running\n";
            printk(0xFFFFFF00, 0x000000, msg);
        }

        // Count threads by state
        int ready = 0, running = 0, blocked = 0;
        for (int i = 0; i < MAX_THREADS_GLOBAL; i++) {
            if (thread_table[i].used) {
                if (thread_table[i].state == THREAD_STATE_READY) ready++;
                else if (thread_table[i].state == THREAD_STATE_RUNNING) running++;
                else if (thread_table[i].state == THREAD_STATE_BLOCKED) blocked++;
            }
        }

        char stats[] = "\nThread states:\n";
        char stats1[] = "  Running: %d\n";
        char stats2[] = "  Ready: %d\n";
        char stats3[] = "  Blocked: %d\n";
        printk(0xFFFFFFFF, 0x000000, stats);
        printk(0xFF00FF00, 0x000000, stats1, running);
        printk(0xFFFFFF00, 0x000000, stats2, ready);
        printk(0xFFFF0000, 0x000000, stats3, blocked);
    }
    // --- Job control commands ---
    else if (strcmp(cmd, cmd18) == 0) {
        list_jobs();
    }
    else if (strncmp(cmd, cmd19, 3) == 0) {
        char *arg = cmd + 3;
        int job_id = 0;
        while (*arg >= '0' && *arg <= '9') {
            job_id = job_id * 10 + (*arg - '0');
            arg++;
        }
        if (job_id > 0) {
            bring_to_foreground(job_id);
        } else {
            char err[] = "Usage: fg <job_id>\n";
            printk(0xFFFF0000, 0x000000, err);
        }
    }
    else if (strncmp(cmd, cmd20, 3) == 0) {
        char *arg = cmd + 3;
        int job_id = 0;
        while (*arg >= '0' && *arg <= '9') {
            job_id = job_id * 10 + (*arg - '0');
            arg++;
        }
        if (job_id > 0) {
            send_to_background(job_id);
        } else {
            char err[] = "Usage: bg <job_id>\n";
            printk(0xFFFF0000, 0x000000, err);
        }
    }
 else if (strncmp(cmd, cmd21, 6) == 0) {
        char *arg = cmd + 6;
        uint32_t seconds = 0;
        
        while (*arg >= '0' && *arg <= '9') {
            seconds = seconds * 10 + (*arg - '0');
            arg++;
        }
        
        if (seconds > 0) {
            char msg[] = "Sleeping for %u seconds...\n";
            printk(0xFFFFFF00, 0x000000, msg, seconds);
            
            // Use the REAL sleep_seconds from sleep.c (timer-based)
            sleep_seconds(seconds);
            
            char done[] = "Awake!\n";
            printk(0xFF00FF00, 0x000000, done);
        } else {
            char err[] = "Usage: sleep <seconds>\n";
            printk(0xFFFF0000, 0x000000, err);
        }
    }
    // --- VFS commands ---
    else if (strcmp(cmd, cmd55) == 0) {
        const char* cwd = vfs_get_cwd_path();
        vfs_list_directory(cwd);
    }
    else if (strncmp(cmd, cmd5, 3) == 0) {
        char* path = cmd + 3;
        char fullpath[256];
        if (path[0] == '/') {
            strcpy_local(fullpath, path);
        } else {
            const char* cwd = vfs_get_cwd_path();
            strcpy_local(fullpath, cwd);
            int len = strlen_local(fullpath);
            if (len > 0 && fullpath[len-1] != '/') {
                fullpath[len] = '/';
                fullpath[len+1] = '\0';
            }
            int i = strlen_local(fullpath);
            int j = 0;
            while (path[j] && i < 255) {
                fullpath[i++] = path[j++];
            }
            fullpath[i] = '\0';
        }
        vfs_list_directory(fullpath);
    }
    else if (strncmp(cmd, cmd6, 4) == 0) {
        char* filename = cmd + 4;
        char fullpath[256];
        if (filename[0] == '/') {
            strcpy_local(fullpath, filename);
        } else {
            const char* cwd = vfs_get_cwd_path();
            strcpy_local(fullpath, cwd);
            int len = strlen_local(fullpath);
            if (len > 0 && fullpath[len-1] != '/') {
                fullpath[len] = '/';
                fullpath[len+1] = '\0';
            }
            int i = strlen_local(fullpath);
            int j = 0;
            while (filename[j] && i < 255) {
                fullpath[i++] = filename[j++];
            }
            fullpath[i] = '\0';
        }
        int fd = vfs_open(fullpath, FILE_READ);
        if (fd >= 0) {
            uint8_t buffer[513];
            int bytes = vfs_read(fd, buffer, 512);
            if (bytes > 0) {
                buffer[bytes] = '\0';
                char fmt[] = "%s\n";
                printk(0xFFFFFFFF, 0x000000, fmt, buffer);
            } else {
                char err[] = "File is empty or read error\n";
                printk(0xFFFF0000, 0x000000, err);
            }
            vfs_close(fd);
        } else {
            char err[] = "File not found: %s\n";
            printk(0xFFFF0000, 0x000000, err, fullpath);
        }
    }
    else if (strncmp(cmd, cmd7, 6) == 0) {
        char* filename = cmd + 6;
        char fullpath[256];
        if (filename[0] == '/') {
            strcpy_local(fullpath, filename);
        } else {
            const char* cwd = vfs_get_cwd_path();
            strcpy_local(fullpath, cwd);
            int len = strlen_local(fullpath);
            if (len > 0 && fullpath[len-1] != '/') {
                fullpath[len] = '/';
                fullpath[len+1] = '\0';
            }
            int i = strlen_local(fullpath);
            int j = 0;
            while (filename[j] && i < 255) {
                fullpath[i++] = filename[j++];
            }
            fullpath[i] = '\0';
        }
        if (vfs_create(fullpath, FILE_READ | FILE_WRITE) == 0) {
            char msg[] = "Created file: %s\n";
            printk(0xFF00FF00, 0x000000, msg, fullpath);
        } else {
            char err[] = "Failed to create file\n";
            printk(0xFFFF0000, 0x000000, err);
        }
    }
    else if (strncmp(cmd, cmd8, 6) == 0) {
        char* dirname = cmd + 6;
        char fullpath[256];
        if (dirname[0] == '/') {
            strcpy_local(fullpath, dirname);
        } else {
            const char* cwd = vfs_get_cwd_path();
            strcpy_local(fullpath, cwd);
            int len = strlen_local(fullpath);
            if (len > 0 && fullpath[len-1] != '/') {
                fullpath[len] = '/';
                fullpath[len+1] = '\0';
            }
            int i = strlen_local(fullpath);
            int j = 0;
            while (dirname[j] && i < 255) {
                fullpath[i++] = dirname[j++];
            }
            fullpath[i] = '\0';
        }
        if (vfs_mkdir(fullpath, FILE_READ | FILE_WRITE) == 0) {
            char msg[] = "Created directory: %s\n";
            printk(0xFF00FF00, 0x000000, msg, fullpath);
        } else {
            char err[] = "Failed to create directory\n";
            printk(0xFFFF0000, 0x000000, err);
        }
    }
    else if (strncmp(cmd, cmd9, 3) == 0) {
        char* path = cmd + 3;
        char fullpath[256];
        if (path[0] == '/') {
            strcpy_local(fullpath, path);
        } else {
            const char* cwd = vfs_get_cwd_path();
            strcpy_local(fullpath, cwd);
            int len = strlen_local(fullpath);
            if (len > 0 && fullpath[len-1] != '/') {
                fullpath[len] = '/';
                fullpath[len+1] = '\0';
            }
            int i = strlen_local(fullpath);
            int j = 0;
            while (path[j] && i < 255) {
                fullpath[i++] = path[j++];
            }
            fullpath[i] = '\0';
        }
        if (vfs_unlink(fullpath) == 0) {
            char msg[] = "Removed: %s\n";
            printk(0xFF00FF00, 0x000000, msg, fullpath);
        } else {
            char err[] = "Failed to remove: %s\n";
            printk(0xFFFF0000, 0x000000, err, fullpath);
        }
    }
    else if (strncmp(cmd, cmd10, 6) == 0) {
        char* rest = cmd + 6;
        char filename[256];
        char content[512];
        
        int i = 0;
        while (rest[i] && rest[i] != ' ' && i < 255) {
            filename[i] = rest[i];
            i++;
        }
        filename[i] = '\0';
        
        while (rest[i] == ' ') i++;
        
        int j = 0;
        while (rest[i] && j < 511) {
            content[j++] = rest[i++];
        }
        content[j] = '\0';
        
        if (filename[0] == '\0' || content[0] == '\0') {
            char err[] = "Usage: write <file> <content>\n";
            printk(0xFFFF0000, 0x000000, err);
        } else {
            char fullpath[256];
            if (filename[0] == '/') {
                strcpy_local(fullpath, filename);
            } else {
                const char* cwd = vfs_get_cwd_path();
                strcpy_local(fullpath, cwd);
                int len = strlen_local(fullpath);
                if (len > 0 && fullpath[len-1] != '/') {
                    fullpath[len] = '/';
                    fullpath[len+1] = '\0';
                }
                int k = strlen_local(fullpath);
                int m = 0;
                while (filename[m] && k < 255) {
                    fullpath[k++] = filename[m++];
                }
                fullpath[k] = '\0';
            }
            int fd = vfs_open(fullpath, FILE_WRITE);
            if (fd >= 0) {
                int written = vfs_write(fd, (uint8_t*)content, strlen_local(content));
                vfs_close(fd);
                if (written > 0) {
                    char msg[] = "Wrote %d bytes to %s\n";
                    printk(0xFF00FF00, 0x000000, msg, written, fullpath);
                } else {
                    char err[] = "Write failed\n";
                    printk(0xFFFF0000, 0x000000, err);
                }
            } else {
                char err[] = "Cannot open file: %s\n";
                printk(0xFFFF0000, 0x000000, err, fullpath);
            }
        }
    }
    else if (strcmp(cmd, cmd11) == 0) {
        fs_stats_t stats;
        char path[] = "/";
        if (vfs_statfs(path, &stats) == 0) {
            char msg1[] = "Filesystem statistics:\n";
            char msg2[] = "  Total blocks: %u\n";
            char msg3[] = "  Free blocks: %u\n";
            char msg4[] = "  Used blocks: %u\n";
            char msg5[] = "  Block size: %u bytes\n";
            char msg6[] = "  Total size: %u KB\n";
            char msg7[] = "  Used size: %u KB\n";
            char msg8[] = "  Free size: %u KB\n";
            printk(0xFFFFFFFF, 0x000000, msg1);
            printk(0xFFFFFFFF, 0x000000, msg2, stats.total_blocks);
            printk(0xFFFFFFFF, 0x000000, msg3, stats.free_blocks);
            printk(0xFFFFFFFF, 0x000000, msg4, stats.total_blocks - stats.free_blocks);
            printk(0xFFFFFFFF, 0x000000, msg5, stats.block_size);
            uint32_t total_kb = (stats.total_blocks * stats.block_size) / 1024;
            uint32_t free_kb = (stats.free_blocks * stats.block_size) / 1024;
            uint32_t used_kb = total_kb - free_kb;
            printk(0xFFFFFFFF, 0x000000, msg6, total_kb);
            printk(0xFFFFFFFF, 0x000000, msg7, used_kb);
            printk(0xFFFFFFFF, 0x000000, msg8, free_kb);
        } else {
            char err[] = "Cannot get filesystem stats\n";
            printk(0xFFFF0000, 0x000000, err);
        }
    }
    else if (strcmp(cmd, cmd13) == 0) {
        char warning[] = "DISK HAS BEEN WIPED!\n";
        printk(0xFFFF0000, 0x000000, warning);
        
        char unmount_msg[] = "Unmounting filesystem...\n";
        printk(0xFFFFFF00, 0x000000, unmount_msg);
        vfs_node_t *root = vfs_get_root();
        if (root && root->fs && root->fs->ops && root->fs->ops->unmount) {
            root->fs->ops->unmount(root->fs);
        }
        
        char device[] = "ata0";
        if (tinyfs_format(device) != 0) {
            char err[] = "[ERROR] Format failed\n";
            printk(0xFFFF0000, 0x000000, err);
            for (;;) {}
        }
        
        char mount_msg[] = "Remounting filesystem...\n";
        printk(0xFFFFFF00, 0x000000, mount_msg);
        char fs_type[] = "tinyfs";
        char mount_point[] = "/";
        if (vfs_mount(fs_type, device, mount_point) != 0) {
            char err[] = "[ERROR] Remount failed\n";
            printk(0xFFFF0000, 0x000000, err);
            for (;;) {}
        }
        char success[] = "Format complete - filesystem remounted\n";
        printk(0xFF00FF00, 0x000000, success);
    }
    else if (strcmp(cmd, cmd144) == 0) {
        char root[] = "/";
        if (vfs_chdir(root) == 0) {
            char msg[] = "%s\n";
            printk(0xFF00FF00, 0x000000, msg, vfs_get_cwd_path());
        }
    }
    else if (strncmp(cmd, cmd14, 3) == 0) {
        char* dir_arg = cmd + 3;
        char dir[256];
        int i = 0;
        while (dir_arg[i] && dir_arg[i] != ' ' && i < 255) {
            dir[i] = dir_arg[i];
            i++;
        }
        dir[i] = '\0';
        
        if (dir[0] == '\0') {
            char root[] = "/";
            if (vfs_chdir(root) == 0) {
                char msg[] = "%s\n";
                printk(0xFF00FF00, 0x000000, msg, vfs_get_cwd_path());
            }
        } else {
            if (vfs_chdir(dir) == 0) {
                char msg[] = "%s\n";
                printk(0xFF00FF00, 0x000000, msg, vfs_get_cwd_path());
            }
        }
    }
    else if (strcmp(cmd, cmd15) == 0) {
        char msg[] = "%s\n";
        printk(0xFFFFFFFF, 0x000000, msg, vfs_get_cwd_path());
    }

    else if (strcmp(cmd, cmd22) == 0) {
    test_syscall_interface();
}

    else {
        char err1[] = "Unknown command: %s\n";
        char err2[] = "Try 'help' for available commands\n";
        printk(0xFFFF0000, 0x000000, err1, cmd);
        printk(0xFFFF0000, 0x000000, err2);
    }
}

void run_text_demo(void) {
    char line1[] = "==========================================\n";
    char title[] = "    AMQ Operating System v0.2\n";
    char line2[] = "==========================================\n";
    char welcome[] = "Welcome! Type 'help' for commands.\n\n";
    
    printk(0x00FFFFFF, 0x000000, line1);
    printk(0x00FFFFFF, 0x000000, title);
    printk(0x00FFFFFF, 0x000000, line2);
    printk(0xFFFFFFFF, 0x000000, welcome);
    
    char prompt_fmt[] = "%s> ";
    printk(0xFF00FF00, 0x000000, prompt_fmt, vfs_get_cwd_path());

    int cursor_visible = 1;
    int cursor_timer = 0;

    while (1) {
        cursor_timer++;
        
        // Process keyboard input
        process_keyboard_buffer();
        
        // Check if user pressed Enter
        if (input_available()) {
            char* input = get_input_and_reset();
            process_command(input);
            printk(0xFF00FF00, 0x000000, prompt_fmt, vfs_get_cwd_path());
        }
        
        // Blink cursor
        if (cursor_timer >= CURSOR_BLINK_RATE) {
            cursor_timer = 0;
            cursor_visible = !cursor_visible;
            draw_cursor(cursor_visible);
        }
        
        // Small delay
        for (volatile int i = 0; i < 4000; i++);
    }
}

void init_shell(void) {
    ClearScreen(0x000000);
    SetCursorPos(0, 0);
    run_text_demo();
}

void test_syscall_interface(void) {
    printk(0xFF00FFFF, 0x000000, "\n=== Testing Syscall Interface ===\n");
    
    // We need to switch to user mode to test syscalls
    // For now, we can test the handlers directly
    
    SAFE_PRINTK(0xFFFFFF00, 0x000000, "[TEST] Calling sys_getpid...\n");
    int64_t pid = sys_getpid();
    SAFE_PRINTK(0xFF00FF00, 0x000000, "[TEST] PID = %lld\n", pid);
    
    SAFE_PRINTK(0xFFFFFF00, 0x000000, "[TEST] Calling sys_uptime...\n");
    int64_t uptime = sys_uptime();
    SAFE_PRINTK(0xFF00FF00, 0x000000, "[TEST] Uptime = %lld seconds\n", uptime);
    
    SAFE_PRINTK(0xFFFFFF00, 0x000000, "[TEST] Calling sys_getcwd...\n");
    char cwd[256];
    sys_getcwd(cwd, 256);
    SAFE_PRINTK(0xFF00FF00, 0x000000, "[TEST] CWD = %s\n", cwd);
    
    SAFE_PRINTK(0xFFFFFF00, 0x000000, "[TEST] Testing sys_mkdir...\n");
    char t1[] = "/syscall_test";
    int ret = sys_mkdir(t1, FILE_READ | FILE_WRITE);
    if (ret == 0) {
        SAFE_PRINTK(0xFF00FF00, 0x000000, "[TEST] Created /syscall_test\n");
    } else {
        SAFE_PRINTK(0xFFFF0000, 0x000000, "[TEST] mkdir failed: %d\n", ret);
    }
    
    SAFE_PRINTK(0xFFFFFF00, 0x000000, "[TEST] Testing sys_open/write/close...\n");
    char t2[] = "/test_syscall.txt";
    int fd = sys_open(t2, FILE_WRITE, 0);
    if (fd >= 0) {
        char data[] = "Hello from syscall!\n";
        int64_t written = sys_write(fd, data, sizeof(data) - 1);
        SAFE_PRINTK(0xFF00FF00, 0x000000, "[TEST] Wrote %lld bytes\n", written);
        sys_close(fd);
        
        // Read it back
        char t3[] = "/test_syscall";
        fd = sys_open(t3, FILE_READ, 0);
        if (fd >= 0) {
            char buf[64];
            int64_t bytes = sys_read(fd, buf, 63);
            if (bytes > 0) {
                buf[bytes] = '\0';
                SAFE_PRINTK(0xFF00FF00, 0x000000, "[TEST] Read back: %s", buf);
            }
            sys_close(fd);
        }
    }
    
    SAFE_PRINTK(0xFF00FFFF, 0x000000, "=== Syscall Tests Complete ===\n\n");
}

// ============================================================================
// SWITCHING TO USER MODE (for real syscall testing)
// ============================================================================

// This function switches from kernel mode (ring 0) to user mode (ring 3)
void switch_to_user_mode(void (*user_func)(void)) {
    printk(0xFFFFFF00, 0x000000, "[SWITCH] Entering user mode...\n");
    
    // Set up user stack (allocate from heap)
    extern void* kmalloc(size_t size);
    uint8_t *user_stack = (uint8_t*)kmalloc(16384);  // 16KB user stack
    uint64_t user_stack_top = (uint64_t)user_stack + 16384;
    
    // Align stack
    user_stack_top &= ~0xF;
    
    SAFE_PRINTK(0xFFFFFF00, 0x000000, "[SWITCH] User stack at 0x%llx\n", user_stack_top);
    SAFE_PRINTK(0xFFFFFF00, 0x000000, "[SWITCH] User function at 0x%llx\n", (uint64_t)user_func);
    
    // Switch to user mode using IRET
    __asm__ volatile(
        "cli\n"                          // Disable interrupts
        "mov %0, %%rsp\n"                // Load user stack
        "pushq $0x20 | 3\n"              // SS (user data segment, RPL=3)
        "pushq %0\n"                     // RSP (user stack pointer)
        "pushfq\n"                       // RFLAGS
        "pop %%rax\n"
        "or $0x200, %%rax\n"             // Set IF (enable interrupts in user mode)
        "push %%rax\n"
        "pushq $0x18 | 3\n"              // CS (user code segment, RPL=3)
        "pushq %1\n"                     // RIP (user function)
        "iretq\n"                        // Jump to user mode!
        :
        : "r"(user_stack_top), "r"((uint64_t)user_func)
        : "memory", "rax"
    );
    
    // Should never reach here
    SAFE_PRINTK(0xFFFF0000, 0x000000, "[ERROR] Failed to switch to user mode\n");
}

// ============================================================================
// EXAMPLE: FULL BOOT SEQUENCE WITH SYSCALLS
// ============================================================================


