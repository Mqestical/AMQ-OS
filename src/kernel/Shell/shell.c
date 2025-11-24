#include <efi.h>
#include <efilib.h>
#include "print.h"
#include "memory.h"
#include "serial.h"
#include "vfs.h"
#include "ata.h"

#define CURSOR_BLINK_RATE 50000

extern volatile uint32_t interrupt_counter;
extern volatile uint8_t last_scancode;
extern volatile uint8_t scancode_write_pos;
extern volatile uint8_t scancode_read_pos;
extern volatile int serial_initialized;
extern void process_keyboard_buffer(void);
extern char* get_input_and_reset(void);
extern int input_available(void);

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

// Process user command
void process_command(char* cmd) {
    // Skip empty input
    if (cmd[0] == '\0') return;
    
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
    
    // --- Basic commands ---
    if (strcmp(cmd, cmd1) == 0) {
        char msg[] = "Hello from AMQ OS!\n";
        printk(0xFF00FF00, 0x000000, msg);
    } 
    else if (strcmp(cmd, cmd2) == 0) {
        char msg1[] = "Available commands:\n";
        char msg2[] = "  hello          - Say hello\n";
        char msg3[] = "  clear          - Clear screen\n";
        char msg4[] = "  echo <text>    - Echo text\n";
        char msg5[] = "  ls [path]      - List directory\n";
        char msg6[] = "  cat <file>     - Display file\n";
        char msg7[] = "  touch <file>   - Create file\n";
        char msg8[] = "  mkdir <dir>    - Create directory\n";
        char msg9[] = "  rm <file>      - Remove file/dir\n";
        char msg10[] = "  write <file>   - Write to file\n";
        char msg11[] = "  df             - Filesystem stats\n";
        char msg12[] = "  memstats       - Memory stats\n";
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
    
    // --- VFS commands ---
    else if (strcmp(cmd, cmd55) == 0) {
        char path[] = "/";
        vfs_list_directory(path);
    }
    else if (strncmp(cmd, cmd5, 3) == 0) {
        char* path = cmd + 3;
        
        // Build absolute path
        char fullpath[256];
        if (path[0] == '/') {
            strcpy_local(fullpath, path);
        } else {
            fullpath[0] = '/';
            strcpy_local(fullpath + 1, path);
        }
        
        vfs_list_directory(fullpath);
    }
    else if (strncmp(cmd, cmd6, 4) == 0) {
        char* filename = cmd + 4;
        
        // Build absolute path
        char fullpath[256];
        if (filename[0] == '/') {
            strcpy_local(fullpath, filename);
        } else {
            fullpath[0] = '/';
            strcpy_local(fullpath + 1, filename);
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
        
        // Build absolute path
        char fullpath[256];
        if (filename[0] == '/') {
            strcpy_local(fullpath, filename);
        } else {
            fullpath[0] = '/';
            strcpy_local(fullpath + 1, filename);
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
        
        // Build absolute path
        char fullpath[256];
        if (dirname[0] == '/') {
            strcpy_local(fullpath, dirname);
        } else {
            fullpath[0] = '/';
            strcpy_local(fullpath + 1, dirname);
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
        
        // Build absolute path
        char fullpath[256];
        if (path[0] == '/') {
            strcpy_local(fullpath, path);
        } else {
            fullpath[0] = '/';
            strcpy_local(fullpath + 1, path);
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
        // Parse: write /path/to/file This is the content
        char* rest = cmd + 6;
        char filename[256];
        char content[512];
        
        // Extract filename (first token)
        int i = 0;
        while (rest[i] && rest[i] != ' ' && i < 255) {
            filename[i] = rest[i];
            i++;
        }
        filename[i] = '\0';
        
        // Skip whitespace
        while (rest[i] == ' ') i++;
        
        // Rest is content
        int j = 0;
        while (rest[i] && j < 511) {
            content[j++] = rest[i++];
        }
        content[j] = '\0';
        
        if (filename[0] == '\0' || content[0] == '\0') {
            char err[] = "Usage: write <file> <content>\n";
            printk(0xFFFF0000, 0x000000, err);
        } else {
            // Build absolute path
            char fullpath[256];
            if (filename[0] == '/') {
                strcpy_local(fullpath, filename);
            } else {
                fullpath[0] = '/';
                strcpy_local(fullpath + 1, filename);
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
            char msg3[] = "  Free blocks:  %u\n";
            char msg4[] = "  Used blocks:  %u\n";
            char msg5[] = "  Block size:   %u bytes\n";
            char msg6[] = "  Total size:   %u KB\n";
            char msg7[] = "  Used size:    %u KB\n";
            char msg8[] = "  Free size:    %u KB\n";
            
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
    char prompt[] = "> ";
    
    printk(0x00FFFFFF, 0x000000, line1);
    printk(0x00FFFFFF, 0x000000, title);
    printk(0x00FFFFFF, 0x000000, line2);
    printk(0xFFFFFFFF, 0x000000, welcome);
    printk(0xFF00FF00, 0x000000, prompt);

    int cursor_visible = 1;
    int cursor_timer = 0;

    while (1) {
        cursor_timer++;
        
        // Process keyboard input
        process_keyboard_buffer();
        
        // Check if user pressed Enter
        if (input_available()) {
            char* input = get_input_and_reset();
            
            // Process the command
            process_command(input);
            
            // Show prompt again
            printk(0xFF00FF00, 0x000000, prompt);
        }
        
        // Blink cursor
        if (cursor_timer >= CURSOR_BLINK_RATE) {
            cursor_timer = 0;
            cursor_visible = !cursor_visible;
            draw_cursor(cursor_visible);
        }
        
        for (volatile int i = 0; i < 4000; i++);
    }
}

void init_shell(void) {
    ClearScreen(0x000000);
    SetCursorPos(0, 0);
    //while(1) __asm__ volatile("hlt");
    run_text_demo();
}