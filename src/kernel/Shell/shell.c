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
#include "mouse.h"

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
    cursor.bg_color = BLACK;
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
        PRINT(YELLOW, BLACK, "[BG ERROR] Invalid thread data\n");
        thread_exit();
        return;
    }
    
    cmd_thread_data_t *data = (cmd_thread_data_t*)current->private_data;
    
    PRINT(MAGENTA, BLACK, "\n[BG %d] Starting: %s\n", data->job_id, data->command);
    
    // Parse command
    char cmd_name[64];
    int i = 0;
    while (data->command[i] && data->command[i] != ' ' && i < 63) {
        cmd_name[i] = data->command[i];
        i++;
    }
    cmd_name[i] = '\0';
    
    if (strcmp(cmd_name, "sleep") == 0) {
        char *arg = data->command;
        while (*arg && *arg != ' ') arg++;
        while (*arg == ' ') arg++;
        
        uint32_t seconds = 0;
        while (*arg >= '0' && *arg <= '9') {
            seconds = seconds * 10 + (*arg - '0');
            arg++;
        }
        
        if (seconds > 0) {
            PRINT(WHITE, BLACK, "[BG %d] Sleeping %u seconds\n", data->job_id, seconds);
            
            // Busy-wait with yields
            for (uint32_t s = 0; s < seconds; s++) {
                for (volatile uint64_t j = 0; j < 50000000; j++) {
                    if (j % 5000000 == 0) {
                        thread_yield();
                    }
                }
                PRINT(WHITE, BLACK, "[BG %d] %u/%u\n", data->job_id, s + 1, seconds);
            }
            
            PRINT(MAGENTA, BLACK, "[BG %d] Done!\n", data->job_id);
        }
    }
    else if (strcmp(cmd_name, "echo") == 0) {
        char *text = data->command + 5;
        PRINT(WHITE, BLACK, "[BG %d] %s\n", data->job_id, text);
    }
    else {
        PRINT(YELLOW, BLACK, "[BG %d] Unknown command: %s\n", data->job_id, cmd_name);
    }
    
    // Clean up
    data->job_id = 0;
    data->command[0] = '\0';
    
    thread_exit();
}

// ============================================================================
// FIXED: bring_to_foreground - Works without scheduler
// ============================================================================

void bring_to_foreground(int job_id) {
    job_t *job = get_job(job_id);
    if (!job) {
        PRINT(YELLOW, BLACK, "fg: job %d not found\n", job_id);
        return;
    }
    
    if (!job->is_background) {
        PRINT(WHITE, BLACK, "fg: job %d is already in foreground\n", job_id);
        return;
    }
    
    thread_t *thread = get_thread(job->tid);
    if (!thread) {
        PRINT(YELLOW, BLACK, "fg: thread %u not found for job %d\n", job->tid, job_id);
        return;
    }
    
    PRINT(MAGENTA, BLACK, "fg: job %d (%s) - thread state = %d\n", 
          job_id, job->command, thread->state);
    
    // If blocked/sleeping, wake it up
    if (thread->state == THREAD_STATE_BLOCKED) {
        PRINT(WHITE, BLACK, "fg: Unblocking thread %u...\n", thread->tid);
        thread_unblock(job->tid);
        job->state = JOB_RUNNING;
        job->sleep_until = 0;
    }
    
    // If it's ready but not running, it will be scheduled automatically
    if (thread->state == THREAD_STATE_READY) {
        PRINT(MAGENTA, BLACK, "fg: Thread %u is ready, will be scheduled\n", thread->tid);
    }
    
    // move to fg
    job->is_background = 0;
    
    PRINT(MAGENTA, BLACK, "fg: job %d (%s) moved to foreground\n", job_id, job->command);
    PRINT(WHITE, BLACK, "Note: Job continues running. Check with 'jobs' command.\n");
}

void send_to_background(int job_id) {
    job_t *job = get_job(job_id);
    if (!job) {
        PRINT(YELLOW, BLACK, "bg: job %d not found\n", job_id);
        return;
    }
    
    if (job->is_background) {
        PRINT(WHITE, BLACK, "bg: job %d is already in background\n", job_id);
        return;
    }
    
    job->is_background = 1;

    PRINT(MAGENTA, BLACK, "bg: job %d (%s) sent to background\n", job_id, job->command);
}

void process_command(char* cmd) {
    // don't do anything if input is empty.
    if (cmd[0] == '\0') return;

    // ========================================================================
    // CHECK FOR BACKGROUND EXECUTION (command ends with &)
    // ========================================================================
    int len = strlen_local(cmd);
    int is_background = 0;
    if (len > 0 && cmd[len-1] == '&') {
        is_background = 1;
        cmd[len-1] = '\0';
        len--;
        while (len > 0 && cmd[len-1] == ' ') {
            cmd[len-1] = '\0';
            len--;
        }
        if (len == 0 || cmd[0] == '\0') {
            PRINT(YELLOW, BLACK, "Invalid command\n");
            return;
        }

        PRINT(WHITE, BLACK, "[SHELL] Running in background: %s\n", cmd);

        // get init process (PID=1)
        process_t *proc = get_process(1);
        if (!proc) {
            PRINT(YELLOW, BLACK, "[ERROR] Init process not found\n");
            return;
        }

        // find free bg_thread_data slot
        extern cmd_thread_data_t bg_thread_data[MAX_JOBS];
        int data_idx = -1;
        for (int i = 0; i < MAX_JOBS; i++) {
            if (bg_thread_data[i].job_id == 0) {
                data_idx = i;
                break;
            }
        }
        if (data_idx < 0) {
            PRINT(YELLOW, BLACK, "[ERROR] No free background slots\n");
            return;
        }

        // copy command to thread data
        strcpy_safe_local(bg_thread_data[data_idx].command, cmd, 256);
        bg_thread_data[data_idx].job_id = -1; // temp value

        // Create thread
        int tid = thread_create(
            proc->pid,
            bg_command_thread,
            65536,  // 64kb stack
            50000000,
            500000000,
            500000000
        );
        
        if (tid < 0) {
            PRINT(YELLOW, BLACK, "[ERROR] Failed to create background thread\n");
            bg_thread_data[data_idx].job_id = 0;
            return;
        }

        // Set thread private data
        thread_t *thread = get_thread(tid);
        if (thread) {
            thread->private_data = &bg_thread_data[data_idx];
        }

        // Add to background job table
        int job_id = add_bg_job(cmd, proc->pid, tid);
        if (job_id > 0) {
            bg_thread_data[data_idx].job_id = job_id;
            PRINT(MAGENTA, BLACK, "[SHELL] Created background job %d (TID=%d)\n", 
                  job_id, tid);
        } else {
            PRINT(YELLOW, BLACK, "[ERROR] Failed to add background job\n");
            bg_thread_data[data_idx].job_id = 0;
        }

        return; 
    }

    // ========================================================================
    // FOREGROUND COMMAND PROCESSING
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
    char cmd23[] = "testbg";
    char cmd24[] = "stum -r3";
    // --- Basic commands ---
    if (strcmp(cmd, cmd1) == 0) {
        PRINT(MAGENTA, BLACK, "hello :D\n");
    }
    else if (strcmp(cmd, cmd2) == 0) {
        PRINT(WHITE, BLACK, "Available commands:\n");
        PRINT(WHITE, BLACK, "  hello - Say hello\n");
        PRINT(WHITE, BLACK, "  clear - Clear screen\n");
        PRINT(WHITE, BLACK, "  echo <text> - Echo text\n");
        PRINT(WHITE, BLACK, "  ls [path] - List directory\n");
        PRINT(WHITE, BLACK, "  cat <file> - Display file\n");
        PRINT(WHITE, BLACK, "  touch <file> - Create file\n");
        PRINT(WHITE, BLACK, "  mkdir <dir> - Create directory\n");
        PRINT(WHITE, BLACK, "  rm <file> - Remove file/dir\n");
        PRINT(WHITE, BLACK, "  write <file> - Write to file\n");
        PRINT(WHITE, BLACK, "  df - Filesystem stats\n");
        PRINT(WHITE, BLACK, "  memstats - Memory stats\n");
        PRINT(YELLOW, BLACK, "  format - Format disk (WARNING: DISK WILL BE WIPED!)\n");
        PRINT(WHITE, BLACK, "  cd <dir> - Change directory\n");
        PRINT(WHITE, BLACK, "  pwd - Print working directory\n");
        PRINT(MAGENTA, BLACK, "  ps - Show processes\n");
        PRINT(MAGENTA, BLACK, "  threads - Show threads\n");
        PRINT(MAGENTA, BLACK, "  jobs - List all jobs\n");
        PRINT(MAGENTA, BLACK, "  fg <job_id> - Bring to foreground\n");
        PRINT(MAGENTA, BLACK, "  bg <job_id> - Send to background\n");
        PRINT(MAGENTA, BLACK, "  sleep <sec> - Sleep for seconds\n");
        PRINT(WHITE, BLACK, "  command & - Run in background\n");
        PRINT(MAGENTA, BLACK, "  syscalltest - Test syscall interface\n");
        PRINT(MAGENTA, BLACK, "  testbg - Test background jobs\n");
        PRINT(YELLOW, BLACK, "  Switching to usermode...\n");
    }
    else if (strcmp(cmd, cmd3) == 0) {
        ClearScreen(BLACK);
        SetCursorPos(0, 0);
    }
    else if (strncmp(cmd, cmd4, 5) == 0) {
        PRINT(WHITE, BLACK, "%s\n", cmd + 5);
    }
    else if (strcmp(cmd, cmd12) == 0) {
        memory_stats();
    }
    // --- Process/Thread commands ---
    else if (strcmp(cmd, cmd16) == 0) {
        print_process_table();
    }
    else if (strcmp(cmd, cmd17) == 0) {
        PRINT(WHITE, BLACK, "\n=== Thread Information ===\n");
        thread_t *current = get_current_thread();
        if (current) {
            PRINT(MAGENTA, BLACK, "Current thread: TID=%u (PID=%u)\n", current->tid, current->parent->pid);
        } else {
            PRINT(WHITE, BLACK, "No thread currently running\n");
        }

        // count threads by state
        int ready = 0, running = 0, blocked = 0;
        for (int i = 0; i < MAX_THREADS_GLOBAL; i++) {
            if (thread_table[i].used) {
                if (thread_table[i].state == THREAD_STATE_READY) ready++;
                else if (thread_table[i].state == THREAD_STATE_RUNNING) running++;
                else if (thread_table[i].state == THREAD_STATE_BLOCKED) blocked++;
            }
        }

        PRINT(WHITE, BLACK, "\nThread states:\n");
        PRINT(MAGENTA, BLACK, "  Running: %d\n", running);
        PRINT(WHITE, BLACK, "  Ready: %d\n", ready);
        PRINT(YELLOW, BLACK, "  Blocked: %d\n", blocked);
    }
    // --- job control commands ---
    else if (strcmp(cmd, cmd18) == 0) {
        list_jobs();
    }
    else if (strncmp(cmd, cmd19, 3) == 0) {
        int job_id = 0;
        char *arg = cmd + 3;
        while (*arg >= '0' && *arg <= '9') {
            job_id = job_id * 10 + (*arg - '0');
            arg++;
        }
        if (job_id > 0) {
            bring_to_foreground(job_id);
        } else {
            PRINT(YELLOW, BLACK, "Usage: fg <job_id>\n");
        }
    }
    else if (strncmp(cmd, cmd20, 3) == 0) {
        int job_id = 0;
        char *arg = cmd + 3;
        while (*arg >= '0' && *arg <= '9') {
            job_id = job_id * 10 + (*arg - '0');
            arg++;
        }
        if (job_id > 0) {
            send_to_background(job_id);
        } else {
            PRINT(YELLOW, BLACK, "Usage: bg <job_id>\n");
        }
    }
    else if (strncmp(cmd, cmd21, 6) == 0) {
        uint32_t seconds = 0;
        char *arg = cmd + 6;
        while (*arg >= '0' && *arg <= '9') {
            seconds = seconds * 10 + (*arg - '0');
            arg++;
        }
        if (seconds > 0) {
            PRINT(WHITE, BLACK, "Sleeping for %u seconds...\n", seconds);
            sleep_seconds(seconds);
            PRINT(MAGENTA, BLACK, "Awake!\n");
        } else {
            PRINT(YELLOW, BLACK, "error: sleep count can not be under 0!\n");
        }
    }
    // --- VFS commands ---
    else if (strcmp(cmd, cmd55) == 0) {
        vfs_list_directory(vfs_get_cwd_path());
    }
    else if (strcmp(cmd, cmd15) == 0) {
        PRINT(WHITE, BLACK, "%s\n", vfs_get_cwd_path());
    }
    else if (strcmp(cmd, cmd22) == 0) {
        test_syscall_interface();
    }
    else if (strcmp(cmd, cmd23) == 0) {
        PRINT(MAGENTA, BLACK, "\n=== Testing Background Jobs ===\n");
        PRINT(WHITE, BLACK, "Test 1: echo test &\n");
        char test1[] = "echo Hello from background! &";
        process_command(test1);

        for (volatile int i = 0; i < 10000000; i++);
        
        PRINT(WHITE, BLACK, "Test 2: sleep 3 &\n");
        char test2[] = "sleep 3 &";
        process_command(test2);
        
        PRINT(MAGENTA, BLACK, "\nCheck with 'jobs' command\n");
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
                PRINT(WHITE, BLACK, "%s\n\n", buffer);
            } else {
                PRINT(YELLOW, BLACK, "File is empty or read error\n");
            }
            vfs_close(fd);
        } else {
            PRINT(YELLOW, BLACK, "File not found: %s\n", fullpath);
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
            PRINT(MAGENTA, BLACK, "Created file: %s\n\n\n", fullpath);
        } else {
            PRINT(YELLOW, BLACK, "Failed to create file\n");
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
            PRINT(MAGENTA, BLACK, "Created directory: %s\n\n\n", fullpath);
        } else {
            PRINT(YELLOW, BLACK, "Failed to create directory\n");
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
            PRINT(MAGENTA, BLACK, "Removed: %s\n", fullpath);
        } else {
            PRINT(YELLOW, BLACK, "Failed to remove: %s\n", fullpath);
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
            PRINT(YELLOW, BLACK, "Usage: write <file> <content>\n");
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
                    PRINT(MAGENTA, BLACK, "Wrote %d bytes to %s\n", written, fullpath);
                } else {
                    PRINT(YELLOW, BLACK, "Write failed\n");
                }
            } else {
                PRINT(YELLOW, BLACK, "Cannot open file: %s\n", fullpath);
            }
        }
    }
    else if (strcmp(cmd, cmd11) == 0) {
        fs_stats_t stats;
        char path[] = "/";
        if (vfs_statfs(path, &stats) == 0) {
            PRINT(WHITE, BLACK, "Filesystem statistics:\n");
            PRINT(WHITE, BLACK, "  Total blocks: %u\n", stats.total_blocks);
            PRINT(WHITE, BLACK, "  Free blocks: %u\n", stats.free_blocks);
            PRINT(WHITE, BLACK, "  Used blocks: %u\n", stats.total_blocks - stats.free_blocks);
            PRINT(WHITE, BLACK, "  Block size: %u bytes\n", stats.block_size);
            uint32_t total_kb = (stats.total_blocks * stats.block_size) / 1024;
            uint32_t free_kb = (stats.free_blocks * stats.block_size) / 1024;
            uint32_t used_kb = total_kb - free_kb;
            PRINT(WHITE, BLACK, "  Total size: %u KB\n", total_kb);
            PRINT(WHITE, BLACK, "  Used size: %u KB\n", used_kb);
            PRINT(WHITE, BLACK, "  Free size: %u KB\n", free_kb);
        } else {
            PRINT(YELLOW, BLACK, "Cannot get filesystem stats\n");
        }
    }
    else if (strcmp(cmd, cmd13) == 0) {
        PRINT(YELLOW, BLACK, "DISK HAS BEEN WIPED!\n");
        PRINT(WHITE, BLACK, "Unmounting filesystem...\n");
        vfs_node_t *root = vfs_get_root();
        if (root && root->fs && root->fs->ops && root->fs->ops->unmount) {
            root->fs->ops->unmount(root->fs);
        }
        
        char device[] = "ata0";
        if (tinyfs_format(device) != 0) {
            PRINT(YELLOW, BLACK, "[ERROR] Format failed\n");
            for (;;) {}
        }
        
        PRINT(WHITE, BLACK, "Remounting filesystem...\n");
        char fs_type[] = "tinyfs";
        char mount_point[] = "/";
        if (vfs_mount(fs_type, device, mount_point) != 0) {
            PRINT(YELLOW, BLACK, "[ERROR] Remount failed\n");
            for (;;) {}
        }
        PRINT(MAGENTA, BLACK, "Format complete - filesystem remounted\n");
    }
    else if (strcmp(cmd, cmd144) == 0) {
        char root[] = "/";
        if (vfs_chdir(root) == 0) {
            PRINT(MAGENTA, BLACK, "%s\n", vfs_get_cwd_path());
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
                PRINT(MAGENTA, BLACK, "%s\n", vfs_get_cwd_path());
            }
        } else {
            if (vfs_chdir(dir) == 0) {
                PRINT(MAGENTA, BLACK, "%s\n", vfs_get_cwd_path());
            }
        }
    } else if (strncmp(cmd, cmd24, 9) == 0) {
        // TODO: implement this later, return for now.
        return;
    }
    else {
        PRINT(YELLOW, BLACK, "Unknown command: %s\n", cmd);
        PRINT(YELLOW, BLACK, "Try 'help' for available commands\n");
    }
}

void run_text_demo(void) {
    scheduler_enable();
    PRINT(CYAN, BLACK, "==========================================\n");
    PRINT(CYAN, BLACK, "    AMQ Operating System v0.7\n");
    PRINT(CYAN, BLACK, "==========================================\n");
    PRINT(WHITE, BLACK, "Welcome! Type 'help' for commands.\n\n");
    PRINT(MAGENTA, BLACK, "%s> ", vfs_get_cwd_path());

    int cursor_visible = 1;
    int cursor_timer = 0;

    while (1) {
        cursor_timer++;
        
        process_keyboard_buffer();
        mouse();
        // check if user pressed Enter
        if (input_available()) {
            char* input = get_input_and_reset();
            process_command(input);
            PRINT(MAGENTA, BLACK, "%s> ", vfs_get_cwd_path());
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
    ClearScreen(BLACK);
    SetCursorPos(0, 0);
    run_text_demo();
}

void test_syscall_interface(void) {
    PRINT(MAGENTA, BLACK, "\n=== Testing Syscall Interface ===\n");
    PRINT(WHITE, BLACK, "[TEST] Calling sys_getpid...\n");
    int64_t pid = sys_getpid();
    PRINT(MAGENTA, BLACK, "[TEST] PID = %lld\n", pid);
    
    PRINT(WHITE, BLACK, "[TEST] Calling sys_uptime...\n");
    int64_t uptime = sys_uptime();
    PRINT(MAGENTA, BLACK, "[TEST] Uptime = %lld seconds\n", uptime);
    
    PRINT(WHITE, BLACK, "[TEST] Calling sys_getcwd...\n");
    char cwd[256];
    sys_getcwd(cwd, 256);
    PRINT(MAGENTA, BLACK, "[TEST] CWD = %s\n", cwd);
    
    PRINT(WHITE, BLACK, "[TEST] Testing sys_mkdir...\n");
    char t1[] = "/syscall_test";
    int ret = sys_mkdir(t1, FILE_READ | FILE_WRITE);
    if (ret == 0) {
        PRINT(MAGENTA, BLACK, "[TEST] Created /syscall_test\n");
    } else {
        PRINT(YELLOW, BLACK, "[TEST] mkdir failed: %d\n", ret);
    }
    
    PRINT(WHITE, BLACK, "[TEST] Testing sys_open/write/close...\n");
    char t2[] = "/test_syscall.txt";
    int fd = sys_open(t2, FILE_WRITE, 0);
    if (fd >= 0) {
        char data[] = "Hello from syscall!\n";
        int64_t written = sys_write(fd, data, sizeof(data) - 1);
        PRINT(MAGENTA, BLACK, "[TEST] Wrote %lld bytes\n", written);
        sys_close(fd);
        
        // read it back
        char t3[] = "/test_syscall";
        fd = sys_open(t3, FILE_READ, 0);
        if (fd >= 0) {
            char buf[64];
            int64_t bytes = sys_read(fd, buf, 63);
            if (bytes > 0) {
                buf[bytes] = '\0';
                PRINT(MAGENTA, BLACK, "[TEST] Read back: %s", buf);
            }
            sys_close(fd);
        }
    }
    
    PRINT(MAGENTA, BLACK, "=== Syscall Tests Complete ===\n\n");
}

// ============================================================================
// SWITCHING TO USER MODE (for real syscall testing)
// ============================================================================

// switch to ring 3 (usermode, kernel ring=0)
void switch_to_user_mode(void (*user_func)(void)) {
    PRINT(WHITE, BLACK, "[SWITCH] Entering user mode...\n");
    
    // Set up user stack (allocate from heap)
    extern void* kmalloc(size_t size);
    uint8_t *user_stack = (uint8_t*)kmalloc(16384);  // 16KB user stack
    uint64_t user_stack_top = (uint64_t)user_stack + 16384;
    
    // Align stack
    user_stack_top &= ~0xF;
    
    PRINT(WHITE, BLACK, "[SWITCH] User stack at 0x%llx\n", user_stack_top);
    PRINT(WHITE, BLACK, "[SWITCH] User function at 0x%llx\n", (uint64_t)user_func);
    
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
    PRINT(YELLOW, BLACK, "[ERROR] Failed to switch to user mode\n");
}