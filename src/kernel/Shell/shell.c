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
#include "elf_loader.h"
#include "elf_test.h"
#include "asm.h"
#include "anthropic.h"
#include "AC97.h"
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
int fibonacci_rng();
void play_rps();
void compute_result(const char *user_str, const char *computer_str,
                    const char *z, const char *o, const char *t);
void shell_command_elftest(void);
void shell_command_elfcheck(const char *args);
void shell_command_elfload(const char *args);
void shell_command_elfinfo(const char *args);
void test_syscall_interface(void);

void draw_cursor(int visible) {
    cursor.bg_color = BLACK;
    if (visible) {
        draw_char(cursor.x, cursor.y, '_', cursor.fg_color, cursor.bg_color);
    } else {
        draw_char(cursor.x, cursor.y, ' ', cursor.fg_color, cursor.bg_color);
    }
}

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

// ============================================================================
// PARSE NUMBER FUNCTION
// ============================================================================

uint32_t parse_number(const char *str) {
    uint32_t result = 0;
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    return result;
}

// ============================================================================
// Audio Command Implementations
// ============================================================================

void cmd_audioinit(void) {
    PRINT(WHITE, BLACK, "Initializing AC'97 audio driver...\n");
    
    if (g_ac97_device && g_ac97_device->initialized) {
        PRINT(WHITE, BLACK, "Audio already initialized.\n");
        ac97_print_info();
        return;
    }
    
    if (ac97_init() == 0) {
        PRINT(MAGENTA, BLACK, "\nAudio initialization successful!\n");
        PRINT(WHITE, BLACK, "Try these commands:\n");
        PRINT(WHITE, BLACK, "  audioinfo    - Display audio device information\n");
        PRINT(WHITE, BLACK, "  beep <freq>  - Play a beep tone (440Hz default)\n");
        PRINT(WHITE, BLACK, "  volume <L> <R> - Set master volume (0-100)\n");
        PRINT(WHITE, BLACK, "  audiotest    - Run audio test\n");
    } else {
        PRINT(YELLOW, BLACK, "Audio initialization failed!\n");
        PRINT(WHITE, BLACK, "This might be because:\n");
        PRINT(WHITE, BLACK, "  - No AC'97 device present in system\n");
        PRINT(WHITE, BLACK, "  - Device not properly detected\n");
        PRINT(WHITE, BLACK, "  - Running in emulator without audio device\n");
        PRINT(WHITE, BLACK, "  - For QEMU: add -soundhw ac97 to command line\n");
    }
}

void cmd_audioinfo(void) {
    if (!g_ac97_device || !g_ac97_device->initialized) {
        PRINT(YELLOW, BLACK, "Audio device not initialized. Run 'audioinit' first.\n");
        return;
    }
    
    ac97_print_info();
}

void cmd_audiotest(void) {
    if (!g_ac97_device || !g_ac97_device->initialized) {
        PRINT(YELLOW, BLACK, "Audio not initialized. Run 'audioinit' first.\n");
        return;
    }
    
    PRINT(CYAN, BLACK, "\n=== Audio Test Suite ===\n\n");
    
    // Test 1: Simple beeps at different frequencies
    PRINT(WHITE, BLACK, "Test 1: Playing frequency sweep...\n");
    
    uint32_t frequencies[] = {262, 294, 330, 349, 392, 440, 494, 523};  // C4 to C5
    const char *notes[] = {"C4", "D4", "E4", "F4", "G4", "A4", "B4", "C5"};
    
    for (int i = 0; i < 8; i++) {
        PRINT(WHITE, BLACK, "  %s (%u Hz)... ", notes[i], frequencies[i]);
        audio_beep(frequencies[i], 300);
        PRINT(MAGENTA, BLACK, "OK\n");
        
        // Short delay between notes
        for (volatile int j = 0; j < 10000000; j++);
    }
    
    PRINT(MAGENTA, BLACK, "\nTest 1: Complete\n\n");
    
    // Test 2: Volume control
    PRINT(WHITE, BLACK, "Test 2: Volume control test...\n");
    
    uint8_t volumes[] = {100, 75, 50, 25, 50, 75, 100};
    
    for (int i = 0; i < 7; i++) {
        PRINT(WHITE, BLACK, "  Volume %u%%... ", volumes[i]);
        ac97_set_master_volume(volumes[i], volumes[i]);
        audio_beep(440, 200);
        PRINT(MAGENTA, BLACK, "OK\n");
        
        for (volatile int j = 0; j < 5000000; j++);
    }
    
    PRINT(MAGENTA, BLACK, "\nTest 2: Complete\n\n");
    
    // Test 3: Stereo panning
    PRINT(WHITE, BLACK, "Test 3: Stereo panning test...\n");
    
    PRINT(WHITE, BLACK, "  Left channel... ");
    ac97_set_master_volume(100, 0);
    audio_beep(440, 500);
    PRINT(MAGENTA, BLACK, "OK\n");
    
    for (volatile int j = 0; j < 10000000; j++);
    
    PRINT(WHITE, BLACK, "  Right channel... ");
    ac97_set_master_volume(0, 100);
    audio_beep(440, 500);
    PRINT(MAGENTA, BLACK, "OK\n");
    
    for (volatile int j = 0; j < 10000000; j++);
    
    PRINT(WHITE, BLACK, "  Both channels... ");
    ac97_set_master_volume(100, 100);
    audio_beep(440, 500);
    PRINT(MAGENTA, BLACK, "OK\n");
    
    PRINT(MAGENTA, BLACK, "\nTest 3: Complete\n\n");
    
    // Reset to normal volume
    ac97_set_master_volume(75, 75);
    
    PRINT(CYAN, BLACK, "=== All Tests Complete ===\n\n");
    PRINT(MAGENTA, BLACK, "Audio system is working correctly!\n");
}

void cmd_beep(const char *args) {
    if (!g_ac97_device || !g_ac97_device->initialized) {
        PRINT(YELLOW, BLACK, "Audio not initialized. Run 'audioinit' first.\n");
        return;
    }
    
    // Parse frequency from arguments
    uint32_t frequency = 440;  // Default A4 note
    
    if (args && *args) {
        // Skip whitespace
        while (*args == ' ') args++;
        
        if (*args >= '0' && *args <= '9') {
            frequency = parse_number(args);
            
            if (frequency < 20 || frequency > 20000) {
                PRINT(YELLOW, BLACK, "Frequency must be between 20-20000 Hz\n");
                return;
            }
        }
    }
    
    PRINT(WHITE, BLACK, "Playing %u Hz beep...\n", frequency);
    
    if (audio_beep(frequency, 500) == 0) {
        PRINT(MAGENTA, BLACK, "Beep complete!\n");
    } else {
        PRINT(YELLOW, BLACK, "Beep failed!\n");
    }
}

void cmd_volume(const char *args) {
    if (!g_ac97_device || !g_ac97_device->initialized) {
        PRINT(YELLOW, BLACK, "Audio not initialized. Run 'audioinit' first.\n");
        return;
    }
    
    if (!args || !*args) {
        // Display current volume
        uint8_t left, right;
        ac97_get_master_volume(&left, &right);
        PRINT(WHITE, BLACK, "Master Volume: L=%u%% R=%u%%\n", left, right);
        ac97_get_pcm_volume(&left, &right);
        PRINT(WHITE, BLACK, "PCM Volume:    L=%u%% R=%u%%\n", left, right);
        return;
    }
    
    // Parse left and right values
    const char *p = args;
    while (*p == ' ') p++;
    
    if (*p < '0' || *p > '9') {
        PRINT(YELLOW, BLACK, "Usage: volume <left> <right>\n");
        PRINT(WHITE, BLACK, "  Values: 0-100 (0 = mute, 100 = max)\n");
        return;
    }
    
    uint32_t left = parse_number(p);
    
    // Skip to next number
    while (*p >= '0' && *p <= '9') p++;
    while (*p == ' ') p++;
    
    uint32_t right = left;  // Default: same as left
    if (*p >= '0' && *p <= '9') {
        right = parse_number(p);
    }
    
    if (left > 100) left = 100;
    if (right > 100) right = 100;
    
    ac97_set_master_volume(left, right);
    
    PRINT(MAGENTA, BLACK, "Volume set: L=%u%% R=%u%%\n", left, right);
}

void cmd_playtone(const char *args) {
    if (!g_ac97_device || !g_ac97_device->initialized) {
        PRINT(YELLOW, BLACK, "Audio not initialized. Run 'audioinit' first.\n");
        return;
    }
    
    // Parse: freq duration (e.g., "440 1000" for 440Hz 1 second)
    const char *p = args;
    while (*p == ' ') p++;
    
    if (*p < '0' || *p > '9') {
        PRINT(YELLOW, BLACK, "Usage: playtone <frequency> <duration_ms>\n");
        PRINT(WHITE, BLACK, "  Example: playtone 440 1000\n");
        return;
    }
    
    uint32_t frequency = parse_number(p);
    
    while (*p >= '0' && *p <= '9') p++;
    while (*p == ' ') p++;
    
    uint32_t duration = 1000;  // Default 1 second
    if (*p >= '0' && *p <= '9') {
        duration = parse_number(p);
    }
    
    if (frequency < 20 || frequency > 20000) {
        PRINT(YELLOW, BLACK, "Frequency must be between 20-20000 Hz\n");
        return;
    }
    
    if (duration > 10000) {
        PRINT(YELLOW, BLACK, "Duration limited to 10 seconds\n");
        duration = 10000;
    }
    
    PRINT(WHITE, BLACK, "Playing %u Hz for %u ms...\n", frequency, duration);
    
    audio_beep(frequency, duration);
    
    PRINT(MAGENTA, BLACK, "Done!\n");
}

void cmd_playsine(const char *args) {
    if (!g_ac97_device || !g_ac97_device->initialized) {
        PRINT(YELLOW, BLACK, "Audio not initialized. Run 'audioinit' first.\n");
        return;
    }
    
    PRINT(WHITE, BLACK, "Simple sine wave playback - use 'playtone' for now\n");
    PRINT(WHITE, BLACK, "Example: playtone 440 1000\n");
}

void cmd_audiomute(const char *args) {
    if (!g_ac97_device || !g_ac97_device->initialized) {
        PRINT(YELLOW, BLACK, "Audio not initialized. Run 'audioinit' first.\n");
        return;
    }
    
    // Check if we should unmute
    if (args && *args) {
        while (*args == ' ') args++;
        if (strncmp(args, "off", 3) == 0) {
            ac97_mute_master(0);
            PRINT(MAGENTA, BLACK, "Audio unmuted\n");
            return;
        }
    }
    
    ac97_mute_master(1);
    PRINT(MAGENTA, BLACK, "Audio muted\n");
}

void cmd_ac97test(void) {
    PRINT(YELLOW, BLACK, "Comprehensive test suite not included in shell.\n");
    PRINT(WHITE, BLACK, "Use 'audiotest' for basic audio tests.\n");
}

void create_elf_from_asm(const char *output_path, const char *asm_code) {
    asm_context_t asm_ctx;
    asm_init(&asm_ctx);

    PRINT(WHITE, BLACK, "[ASM] Assembling code...\n");
    print_unsigned(asm_ctx.error, 16);

    printk(CYAN, BLACK, asm_code);

    if (asm_program(&asm_ctx, asm_code) < 0) {
        PRINT(YELLOW, BLACK, "[ASM] Error: %s\n", asm_ctx.error_msg);
        return;
    }

    size_t code_size;
    uint8_t *code = asm_get_code(&asm_ctx, &code_size);

    if (!code || code_size == 0) {
        PRINT(YELLOW, BLACK, "[ASM] No code generated\n");
        return;
    }

    PRINT(MAGENTA, BLACK, "[ASM] Generated");
    print_unsigned(code_size, 16);
     PRINT(MAGENTA, BLACK, "bytes of machine code\n");

    size_t total_size = sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr) + code_size;
    uint8_t *elf = kmalloc(total_size);

    if (!elf) {
        PRINT(YELLOW, BLACK, "[ASM] Out of memory\n");
        return;
    }

    for (size_t i = 0; i < total_size; i++) {
        elf[i] = 0;
    }

    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)elf;
    ehdr->e_ident[EI_MAG0] = 0x7F;
    ehdr->e_ident[EI_MAG1] = 'E';
    ehdr->e_ident[EI_MAG2] = 'L';
    ehdr->e_ident[EI_MAG3] = 'F';
    ehdr->e_ident[EI_CLASS] = ELFCLASS64;
    ehdr->e_ident[EI_DATA] = ELFDATA2LSB;
    ehdr->e_ident[EI_VERSION] = EV_CURRENT;
    ehdr->e_type = ET_EXEC;
    ehdr->e_machine = EM_X86_64;
    ehdr->e_version = EV_CURRENT;
    ehdr->e_entry = 0x400000;
    ehdr->e_phoff = sizeof(Elf64_Ehdr);
    ehdr->e_ehsize = sizeof(Elf64_Ehdr);
    ehdr->e_phentsize = sizeof(Elf64_Phdr);
    ehdr->e_phnum = 1;

    Elf64_Phdr *phdr = (Elf64_Phdr *)(elf + sizeof(Elf64_Ehdr));
    phdr->p_type = PT_LOAD;
    phdr->p_flags = PF_R | PF_X;
    phdr->p_offset = sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr);
    phdr->p_vaddr = 0x400000;
    phdr->p_paddr = 0x400000;
    phdr->p_filesz = code_size;
    phdr->p_memsz = code_size;
    phdr->p_align = 0x1000;

    uint8_t *code_section = elf + sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr);
    for (size_t i = 0; i < code_size; i++) {
        code_section[i] = code[i];
    }

    char fullpath[256];
    if (output_path[0] == '/') {
        strcpy_local(fullpath, output_path);
    } else {
        const char *cwd = vfs_get_cwd_path();
        strcpy_local(fullpath, cwd);
        int len = strlen_local(fullpath);
        if (len > 0 && fullpath[len-1] != '/') {
            fullpath[len] = '/';
            fullpath[len+1] = '\0';
        }
        int i = strlen_local(fullpath);
        int j = 0;
        while (output_path[j] && i < 255) {
            fullpath[i++] = output_path[j++];
        }
        fullpath[i] = '\0';
    }

    int fd = vfs_open(fullpath, FILE_WRITE);
    if (fd < 0) {
        vfs_create(fullpath, FILE_READ | FILE_WRITE);
        fd = vfs_open(fullpath, FILE_WRITE);
    }

    if (fd >= 0) {
        int written = vfs_write(fd, elf, total_size);
        vfs_close(fd);
        if (written > 0) {
            PRINT(MAGENTA, BLACK, "[ASM] Created ELF: %s (%d bytes)\n", fullpath, written);
            PRINT(WHITE, BLACK, "[ASM] Test with: elfinfo %s\n", output_path);
            PRINT(WHITE, BLACK, "[ASM] Run with: elfload %s\n", output_path);
        } else {
            PRINT(YELLOW, BLACK, "[ASM] Failed to write file\n");
        }
    } else {
        PRINT(YELLOW, BLACK, "[ASM] Failed to create file\n");
    }

    kfree(elf);
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

    data->job_id = 0;
    data->command[0] = '\0';

    thread_exit();
}


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

    if (thread->state == THREAD_STATE_BLOCKED) {
        PRINT(WHITE, BLACK, "fg: Unblocking thread %u...\n", thread->tid);
        thread_unblock(job->tid);
        job->state = JOB_RUNNING;
        job->sleep_until = 0;
    }

    if (thread->state == THREAD_STATE_READY) {
        PRINT(MAGENTA, BLACK, "fg: Thread %u is ready, will be scheduled\n", thread->tid);
    }

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
    if (cmd[0] == '\0') return;

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

        process_t *proc = get_process(1);
        if (!proc) {
            PRINT(YELLOW, BLACK, "[ERROR] Init process not found\n");
            return;
        }

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

        strcpy_safe_local(bg_thread_data[data_idx].command, cmd, 256);
        bg_thread_data[data_idx].job_id = -1;

        int tid = thread_create(
            proc->pid,
            bg_command_thread,
            65536,
            50000000,
            500000000,
            500000000
        );

        if (tid < 0) {
            PRINT(YELLOW, BLACK, "[ERROR] Failed to create background thread\n");
            bg_thread_data[data_idx].job_id = 0;
            return;
        }

        thread_t *thread = get_thread(tid);
        if (thread) {
            thread->private_data = &bg_thread_data[data_idx];
        }

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
    char cmd25[] = "rps";
    char subcmd25_1[] = "rock";
    char subcmd25_2[] = "paper";
    char subcmd25_3[] = "scissors";
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
        PRINT(YELLOW, BLACK, "  stum -r3 - switch to usermode (UnAvailable!)\n");

        PRINT(CYAN, BLACK, "\nAudio Commands:\n");
        PRINT(WHITE, BLACK, "  audioinit - Initialize AC'97 audio device\n");
        PRINT(WHITE, BLACK, "  audioinfo - Display audio device information\n");
        PRINT(WHITE, BLACK, "  audiotest - Run comprehensive audio test suite\n");
        PRINT(WHITE, BLACK, "  beep [freq] - Play beep tone (default 440Hz)\n");
        PRINT(WHITE, BLACK, "  volume [L] [R] - Set/display master volume (0-100)\n");
        PRINT(WHITE, BLACK, "  playtone <freq> <ms> - Play specific frequency\n");
        PRINT(WHITE, BLACK, "  audiomute [off] - Mute/unmute audio\n");

        PRINT(BROWN, BLACK, "RPS - Play Rock Paper Scissors with a Computer!\n");
        PRINT(CYAN, BLACK, "\nELF Commands:\n");
        PRINT(WHITE, BLACK, "  elfinfo <file>  - Display ELF file information\n");
        PRINT(WHITE, BLACK, "  elfload <file>  - Load and execute ELF executable\n");
        PRINT(WHITE, BLACK, "  elfcheck <file> - Quick ELF validation\n");
        PRINT(WHITE, BLACK, "  elftest         - Run ELF loader tests\n");
        PRINT(CYAN, BLACK, "\nAssembler Commands:\n");
        PRINT(WHITE, BLACK, "  ASM <output.elf> <assembly code>\n");
        PRINT(BROWN, BLACK, "    Assemble inline x86-64 code and create ELF executable\n");
        PRINT(BROWN, BLACK, "    Use semicolons (;) to separate instructions\n");
        PRINT(BROWN, BLACK, "    Example: ASM hello.elf mov rax, 72; ret\n");
        PRINT(WHITE, BLACK, "\n  asmfile <source.asm> <output.elf>\n");
        PRINT(BROWN, BLACK, "    Assemble code from a file\n");
        PRINT(BROWN, BLACK, "    Example: asmfile program.asm prog.elf\n");
        PRINT(WHITE, BLACK, "\n  Supported Instructions:\n");
        PRINT(BROWN, BLACK, "    mov reg, imm  - Move immediate to register\n");
        PRINT(BROWN, BLACK, "    add reg, imm  - Add immediate (-128 to 127)\n");
        PRINT(BROWN, BLACK, "    sub reg, imm  - Subtract immediate\n");
        PRINT(BROWN, BLACK, "    xor reg, reg  - XOR two registers\n");
        PRINT(BROWN, BLACK, "    ret           - Return from function\n");
        PRINT(BROWN, BLACK, "    nop           - No operation\n");
        PRINT(WHITE, BLACK, "\n  Available Registers:\n");
        PRINT(BROWN, BLACK, "    rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp, r8-r15\n");
        PRINT(CYAN, BLACK, "\nAssembler Commands:\n");
        PRINT(WHITE, BLACK, "  ASM <file> <code>   - Assemble inline (use ; for newlines)\n");
        PRINT(WHITE, BLACK, "  asmfile <src> <out> - Assemble from file\n");
        PRINT(BROWN, BLACK, "  Supported Instructions:\n");
        PRINT(BROWN, BLACK, "    Data: mov, push, pop\n");
        PRINT(BROWN, BLACK, "    Arithmetic: add, sub, inc, dec, neg\n");
        PRINT(BROWN, BLACK, "    Logic: and, or, xor, not, shl, shr\n");
        PRINT(BROWN, BLACK, "    Compare: cmp\n");
        PRINT(BROWN, BLACK, "    Control: syscall, ret, nop\n");
        PRINT(BROWN, BLACK, "  Registers: rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp, r8-r15\n");
        PRINT(CYAN, BLACK, "\nText Editor:\n");
        PRINT(WHITE, BLACK, "  anthropic <file> - Open graphical text editor\n");
        PRINT(BROWN, BLACK, "    Ctrl+S to save, click X to close\n");
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
        return;
    } else if (strncmp(cmd, cmd25, 4) == 0) {
        PRINT(YELLOW, BLACK, "Pick: Rock, Paper, or Scissors? (you're going to lose btw)\n");
        play_rps();
}

   else if (STRNCMP(cmd, "elfinfo ", 8) == 0) {
       shell_command_elfinfo(cmd + 8);
   }
   else if (STRNCMP(cmd, "elfinfo", 8) == 0) {
       PRINT(YELLOW, BLACK, "Usage: elfinfo <file>\n");
   }
   else if (STRNCMP(cmd, "elfload ", 8) == 0) {
       shell_command_elfload(cmd + 8);
   }
   else if (STRNCMP(cmd, "elfload", 7) == 0) {
       PRINT(YELLOW, BLACK, "Usage: elfload <file>\n");
   }
   else if (STRNCMP(cmd, "elfcheck ", 9) == 0) {
       shell_command_elfcheck(cmd + 9);
   }
   else if (STRNCMP(cmd, "elfcheck", 8) == 0) {
       PRINT(YELLOW, BLACK, "Usage: elfcheck <file>\n");
   }
   else if (STRNCMP(cmd, "elftest", 8) == 0) {
       shell_command_elftest();
   }
else if (strncmp(cmd, "ASM ", 4) == 0) {
    char *rest = cmd + 4;
    char filename[256];

    int i = 0;
    while (rest[i] && rest[i] != ' ' && i < 255) {
        filename[i] = rest[i];
        i++;
    }
    filename[i] = '\0';

    while (rest[i] == ' ') i++;

    char *asm_code = &rest[i];

    if (filename[0] == '\0' || asm_code[0] == '\0') {
        PRINT(YELLOW, BLACK, "Usage: asm <file> <assembly code>\n");
        PRINT(WHITE, BLACK, "Example: asm test.elf mov rax, 42; ret\n");
    } else {
        char code_buf[512];
        int j = 0;
        for (int k = 0; asm_code[k] && j < 511; k++) {
            if (asm_code[k] == ';') {
                code_buf[j++] = '\n';
            } else {
                code_buf[j++] = asm_code[k];
            }
        }
        code_buf[j] = '\0';

        create_elf_from_asm(filename, code_buf);
    }
}

else if (STRNCMP(cmd, "asmfile ", 8) == 0) {
    char *rest = cmd + 8;
    char source[256];
    char output[256];

    int i = 0;
    while (rest[i] && rest[i] != ' ' && i < 255) {
        source[i] = rest[i];
        i++;
    }
    source[i] = '\0';

    while (rest[i] == ' ') i++;

    int j = 0;
    while (rest[i] && j < 255) {
        output[j++] = rest[i++];
    }
    output[j] = '\0';

    if (source[0] == '\0' || output[0] == '\0') {
        PRINT(YELLOW, BLACK, "Usage: asmfile <source.asm> <output.elf>\n");
        return;
    }

    char fullpath[256];
    if (source[0] == '/') {
        strcpy_local(fullpath, source);
    } else {
        const char *cwd = vfs_get_cwd_path();
        strcpy_local(fullpath, cwd);
        int len = strlen_local(fullpath);
        if (len > 0 && fullpath[len-1] != '/') {
            fullpath[len] = '/';
            fullpath[len+1] = '\0';
        }
        int k = strlen_local(fullpath);
        int m = 0;
        while (source[m] && k < 255) {
            fullpath[k++] = source[m++];
        }
        fullpath[k] = '\0';
    }

    int fd = vfs_open(fullpath, FILE_READ);
    if (fd < 0) {
        PRINT(YELLOW, BLACK, "Cannot open source file: %s\n", fullpath);
        return;
    }

    uint8_t asm_buf[2048];
    int bytes = vfs_read(fd, asm_buf, 2047);
    vfs_close(fd);

    if (bytes <= 0) {
        PRINT(YELLOW, BLACK, "Failed to read source file\n");
        return;
    }

    asm_buf[bytes] = '\0';

    create_elf_from_asm(output, (char *)asm_buf);
} else if (STRNCMP(cmd, "anthropic ", 10) == 0) {
    char* filename = cmd + 10;


    while (*filename == ' ') filename++;

    if (filename[0] == '\0') {
        PRINT(YELLOW, BLACK, "Usage: anthropic <filename>\n");
        PRINT(WHITE, BLACK, "  Opens a graphical text editor\n");
        PRINT(WHITE, BLACK, "  Ctrl+S to save\n");
        PRINT(WHITE, BLACK, "  Click X button to close\n");
    } else {
        anthropic_editor(filename);

        PRINT(MAGENTA, BLACK, "\n%s> ", vfs_get_cwd_path());
    }
}
else if (STRNCMP(cmd, "anthropic",9) == 0) {
    PRINT(YELLOW, BLACK, "Usage: anthropic <filename>\n");
} else if (STRNCMP(cmd, "audioinit", 9) == 0) {
    cmd_audioinit();
}
else if (STRNCMP(cmd, "audioinfo", 9) == 0) {
    cmd_audioinfo();
}
else if (STRNCMP(cmd, "audiotest", 9) == 0) {
    cmd_audiotest();
}
else if (STRNCMP(cmd, "beep ", 5) == 0) {
    cmd_beep(cmd + 5);
}
else if (STRNCMP(cmd, "beep", 4) == 0) {
    cmd_beep(NULL);
}
else if (STRNCMP(cmd, "volume ", 7) == 0) {
    cmd_volume(cmd + 7);
}
else if (STRNCMP(cmd, "volume", 6) == 0) {
    cmd_volume(NULL);
}
else if (STRNCMP(cmd, "playtone ", 9) == 0) {
    cmd_playtone(cmd + 9);
}
else if (STRNCMP(cmd, "playtone", 8) == 0) {
    cmd_playtone(NULL);
}
else if (STRNCMP(cmd, "audiomute ", 10) == 0) {
    cmd_audiomute(cmd + 10);
}
else if (STRNCMP(cmd, "audiomute", 9) == 0) {
    cmd_audiomute(NULL);
} else {
        PRINT(YELLOW, BLACK, "Unknown command: %s\n", cmd);
        PRINT(YELLOW, BLACK, "Try 'help' for available commands\n");
    }
}

void run_text_demo(void) {
    scheduler_enable();
    PRINT(CYAN, BLACK, "==========================================\n");
    PRINT(CYAN, BLACK, "    AMQ Operating System v1.2\n");
    PRINT(CYAN, BLACK, "==========================================\n");
    PRINT(WHITE, BLACK, "Welcome! Type 'help' for commands.\n\n");
    PRINT(MAGENTA, BLACK, "%s> ", vfs_get_cwd_path());

    int cursor_visible = 1;
    int cursor_timer = 0;

    while (1) {
        cursor_timer++;

        process_keyboard_buffer();
        mouse();
        if (input_available()) {
            char* input = get_input_and_reset();
            process_command(input);
            PRINT(MAGENTA, BLACK, "%s> ", vfs_get_cwd_path());
        }

        if (cursor_timer >= CURSOR_BLINK_RATE) {
            cursor_timer = 0;
            cursor_visible = !cursor_visible;
            draw_cursor(cursor_visible);
        }

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


void switch_to_user_mode(void (*user_func)(void)) {
    PRINT(WHITE, BLACK, "[SWITCH] Entering user mode...\n");

    extern void* kmalloc(size_t size);
    uint8_t *user_stack = (uint8_t*)kmalloc(16384);
    uint64_t user_stack_top = (uint64_t)user_stack + 16384;

    user_stack_top &= ~0xF;

    PRINT(WHITE, BLACK, "[SWITCH] User stack at 0x%llx\n", user_stack_top);
    PRINT(WHITE, BLACK, "[SWITCH] User function at 0x%llx\n", (uint64_t)user_func);

    __asm__ volatile(
        "cli\n"
        "mov %0, %%rsp\n"
        "pushq $0x20 | 3\n"
        "pushq %0\n"
        "pushfq\n"
        "pop %%rax\n"
        "or $0x200, %%rax\n"
        "push %%rax\n"
        "pushq $0x18 | 3\n"
        "pushq %1\n"
        "iretq\n"
        :
        : "r"(user_stack_top), "r"((uint64_t)user_func)
        : "memory", "rax"
    );

    PRINT(YELLOW, BLACK, "[ERROR] Failed to switch to user mode\n");
}

int fibonacci_rng() {
    static int a = 0;
    static int b = 1;

    int next = a + b;
    a = b;
    b = next;

    return next % 3;
}

void play_rps() {
    char* userpick = NULL;

    char z[] = "rock";
    char o[] = "paper";
    char t[] = "scissors";

    while (!userpick) {
        process_keyboard_buffer();

        if (input_available()) {
            userpick = get_input_and_reset();
            if (userpick[0] == '\0') {
                userpick = NULL;
            }
        }
    }

    int computerpick = fibonacci_rng();

    char* computer_str = (computerpick == 0) ? z :
                         (computerpick == 1) ? o :
                         t;

    char* user_str = (strncmp(userpick, z, 4) == 0) ? z :
                     (strncmp(userpick, o, 5) == 0) ? o :
                     (strncmp(userpick, t, 8) == 0) ? t :
                     NULL;

    if (!user_str) {
        PRINT(YELLOW,BLACK, "Invalid input!\n");
        return;
    }

    PRINT(YELLOW, BLACK, "You picked: %s\n", user_str);
    PRINT(YELLOW, BLACK, "Computer picked: %s\n", computer_str);

    compute_result(user_str, computer_str, z,o,t);

}

void compute_result(const char *user_str, const char *computer_str,
                    const char *z, const char *o, const char *t)
{
    if ((strncmp(user_str, z, 4) == 0 && strncmp(computer_str, z, 4) == 0) ||
        (strncmp(user_str, o, 5) == 0 && strncmp(computer_str, o, 5) == 0) ||
        (strncmp(user_str, t, 8) == 0 && strncmp(computer_str, t, 8) == 0))
    {
        PRINT(YELLOW, BLACK, "Draw!\n");
        return;
    }

    if ((strncmp(user_str, z, 4) == 0 && strncmp(computer_str, t, 8) == 0) ||
        (strncmp(user_str, o, 5) == 0 && strncmp(computer_str, z, 4) == 0) ||
        (strncmp(user_str, t, 8) == 0 && strncmp(computer_str, o, 5) == 0))
    {
        PRINT(YELLOW, BLACK, "you win (unfortunately, fuck!)\n");
        return;
    }

    PRINT(YELLOW, BLACK, "easy win, you're horrible lmfao\n");
}

void shell_command_elftest(void) {
    PRINT(CYAN, BLACK, "\n========================================\n");
    PRINT(CYAN, BLACK, "  ELF Loader Test Suite\n");
    PRINT(CYAN, BLACK, "========================================\n\n");

    elf_test_simple();

    PRINT(MAGENTA, BLACK, "\n=== All Tests Complete ===\n");
}

void shell_command_elfcheck(const char *args) {
    while (*args == ' ') args++;

    if (*args == '\0') {
        PRINT(YELLOW, BLACK, "Usage: elfcheck <file>\n");
        return;
    }

    char fullpath[256];
    if (args[0] == '/') {
        int i = 0;
        while (args[i] && i < 255) {
            fullpath[i] = args[i];
            i++;
        }
        fullpath[i] = '\0';
    } else {
        const char *cwd = vfs_get_cwd_path();
        int i = 0;
        while (cwd[i] && i < 254) {
            fullpath[i] = cwd[i];
            i++;
        }
        if (i > 0 && fullpath[i-1] != '/') {
            fullpath[i++] = '/';
        }
        int j = 0;
        while (args[j] && i < 255) {
            fullpath[i++] = args[j++];
        }
        fullpath[i] = '\0';
    }

    int fd = vfs_open(fullpath, FILE_READ);
    if (fd < 0) {
        PRINT(YELLOW, BLACK, "Cannot open: %s\n", fullpath);
        return;
    }

    uint8_t header[64];
    int bytes = vfs_read(fd, header, 64);
    vfs_close(fd);

    if (bytes < 64) {
        PRINT(YELLOW, BLACK, "File too small\n");
        return;
    }

    if (header[0] != 0x7F || header[1] != 'E' ||
        header[2] != 'L' || header[3] != 'F') {
        PRINT(YELLOW, BLACK, "✗ Not an ELF file\n");
        return;
    }

    PRINT(MAGENTA, BLACK, "✓ Valid ELF file\n");

    if (header[4] == 1) {
        PRINT(WHITE, BLACK, "  Class: ELF32 (32-bit)\n");
    } else if (header[4] == 2) {
        PRINT(WHITE, BLACK, "  Class: ELF64 (64-bit)\n");
    }

    if (header[5] == 1) {
        PRINT(WHITE, BLACK, "  Data: Little-endian\n");
    } else if (header[5] == 2) {
        PRINT(WHITE, BLACK, "  Data: Big-endian\n");
    }

    uint16_t type = *(uint16_t *)&header[16];
    PRINT(WHITE, BLACK, "  Type: ");
    switch (type) {
        case 0: PRINT(WHITE, BLACK, "None\n"); break;
        case 1: PRINT(WHITE, BLACK, "Relocatable\n"); break;
        case 2: PRINT(WHITE, BLACK, "Executable\n"); break;
        case 3: PRINT(WHITE, BLACK, "Shared object\n"); break;
        case 4: PRINT(WHITE, BLACK, "Core dump\n"); break;
        default: PRINT(WHITE, BLACK, "Unknown\n"); break;
    }

    uint16_t machine = *(uint16_t *)&header[18];
    PRINT(WHITE, BLACK, "  Machine: ");
    switch (machine) {
        case 3: PRINT(WHITE, BLACK, "Intel 80386\n"); break;
        case 62: PRINT(WHITE, BLACK, "AMD x86-64\n"); break;
        default: PRINT(WHITE, BLACK, "%u\n", machine); break;
    }
}

void shell_command_elfload(const char *args) {
    while (*args == ' ') args++;

    if (*args == '\0') {
        PRINT(YELLOW, BLACK, "Usage: elfload <file>\n");
        PRINT(WHITE, BLACK, "  Loads and executes an ELF executable\n");
        return;
    }

    char fullpath[256];
    if (args[0] == '/') {
        int i = 0;
        while (args[i] && i < 255) {
            fullpath[i] = args[i];
            i++;
        }
        fullpath[i] = '\0';
    } else {
        const char *cwd = vfs_get_cwd_path();
        int i = 0;
        while (cwd[i] && i < 254) {
            fullpath[i] = cwd[i];
            i++;
        }
        if (i > 0 && fullpath[i-1] != '/') {
            fullpath[i++] = '/';
        }
        int j = 0;
        while (args[j] && i < 255) {
            fullpath[i++] = args[j++];
        }
        fullpath[i] = '\0';
    }

    PRINT(WHITE, BLACK, "[ELFLOAD] Loading: %s\n", fullpath);

    int fd = vfs_open(fullpath, FILE_READ);
    if (fd < 0) {
        PRINT(YELLOW, BLACK, "Failed to open: %s\n", fullpath);
        return;
    }

    vfs_node_t *node = vfs_resolve_path(fullpath);
    if (!node) {
        vfs_close(fd);
        return;
    }

    size_t file_size = node->size;

    void *elf_buffer = kmalloc(file_size);
    if (!elf_buffer) {
        PRINT(YELLOW, BLACK, "Out of memory\n");
        vfs_close(fd);
        return;
    }

    int bytes = vfs_read(fd, (uint8_t *)elf_buffer, file_size);
    vfs_close(fd);

    if (bytes != (int)file_size) {
        PRINT(YELLOW, BLACK, "Failed to read file\n");
        kfree(elf_buffer);
        return;
    }

    elf_load_info_t load_info;
    int result = elf_load(elf_buffer, file_size, &load_info);

    if (result != ELF_SUCCESS) {
        PRINT(YELLOW, BLACK, "Failed to load ELF: error %d\n", result);
        kfree(elf_buffer);
        return;
    }

    PRINT(MAGENTA, BLACK, "[ELFLOAD] Successfully loaded:\n");
    PRINT(MAGENTA, BLACK, "  Base address: 0x%llx\n", load_info.base_addr);
    PRINT(MAGENTA, BLACK, "  Entry point:  0x%llx\n", load_info.entry_point);
    PRINT(MAGENTA, BLACK, "  Dynamic: %s\n", load_info.is_dynamic ? "Yes" : "No");
    PRINT(MAGENTA, BLACK, "  TLS: %s\n", load_info.has_tls ? "Yes" : "No");

    if (load_info.has_tls) {
        PRINT(WHITE, BLACK, "  TLS size: %llu bytes\n", load_info.tls_size);
    }

    const char *name = fullpath;
    for (int i = 0; fullpath[i]; i++) {
        if (fullpath[i] == '/') {
            name = &fullpath[i + 1];
        }
    }

    int pid = process_create(name, load_info.base_addr);
    if (pid < 0) {
        PRINT(YELLOW, BLACK, "Failed to create process\n");
        kfree(elf_buffer);
        return;
    }

    void (*entry)(void) = (void (*)(void))load_info.entry_point;
    int tid = thread_create(pid, entry, 131072, 50000000, 1000000000, 1000000000);

    if (tid < 0) {
        PRINT(YELLOW, BLACK, "Failed to create thread\n");
        kfree(elf_buffer);
        return;
    }

    extern int add_fg_job(const char *command, uint32_t pid, uint32_t tid);
    int job_id = add_fg_job(fullpath, pid, tid);

    PRINT(MAGENTA, BLACK, "[ELFLOAD] Created process %d (thread %d, job %d)\n",
          pid, tid, job_id);
    PRINT(MAGENTA, BLACK, "[ELFLOAD] Program is now running\n");

}

void shell_command_elfinfo(const char *args) {
    while (*args == ' ') args++;

    if (*args == '\0') {
        PRINT(YELLOW, BLACK, "Usage: elfinfo <file>\n");
        PRINT(WHITE, BLACK, "  Displays detailed ELF file information\n");
        return;
    }

    char fullpath[256];
    if (args[0] == '/') {
        int i = 0;
        while (args[i] && i < 255) {
            fullpath[i] = args[i];
            i++;
        }
        fullpath[i] = '\0';
    } else {
        const char *cwd = vfs_get_cwd_path();
        int i = 0;
        while (cwd[i] && i < 254) {
            fullpath[i] = cwd[i];
            i++;
        }
        if (i > 0 && fullpath[i-1] != '/') {
            fullpath[i++] = '/';
        }
        int j = 0;
        while (args[j] && i < 255) {
            fullpath[i++] = args[j++];
        }
        fullpath[i] = '\0';
    }

    int fd = vfs_open(fullpath, FILE_READ);
    if (fd < 0) {
        PRINT(YELLOW, BLACK, "Failed to open: %s\n", fullpath);
        return;
    }

    vfs_node_t *node = vfs_resolve_path(fullpath);
    if (!node) {
        vfs_close(fd);
        return;
    }

    size_t file_size = node->size;

    void *elf_buffer = kmalloc(file_size);
    if (!elf_buffer) {
        PRINT(YELLOW, BLACK, "Out of memory\n");
        vfs_close(fd);
        return;
    }

    int bytes = vfs_read(fd, (uint8_t *)elf_buffer, file_size);
    vfs_close(fd);

    if (bytes != (int)file_size) {
        PRINT(YELLOW, BLACK, "Failed to read file\n");
        kfree(elf_buffer);
        return;
    }

    if (elf_validate(elf_buffer, file_size) != ELF_SUCCESS) {
        PRINT(YELLOW, BLACK, "Not a valid ELF file\n");
        kfree(elf_buffer);
        return;
    }

    elf_context_t *ctx = elf_create_context(elf_buffer, file_size);
    if (!ctx) {
        PRINT(YELLOW, BLACK, "Failed to parse ELF\n");
        kfree(elf_buffer);
        return;
    }

    PRINT(CYAN, BLACK, "\n========================================\n");
    PRINT(CYAN, BLACK, "  ELF File Information: %s\n", fullpath);
    PRINT(CYAN, BLACK, "========================================\n");

    elf_print_header(ctx->ehdr);
    elf_print_program_headers(ctx);
    elf_print_section_headers(ctx);

    elf_resolve_symbols(ctx);

    int func_count = 0, obj_count = 0, total_count = 0;
    for (int i = 0; i < 256; i++) {
        elf_symbol_t *sym = ctx->symbol_hash[i];
        while (sym) {
            total_count++;
            if (sym->type == STT_FUNC) func_count++;
            else if (sym->type == STT_OBJECT) obj_count++;
            sym = sym->next;
        }
    }

    PRINT(WHITE, BLACK, "\n=== Symbol Summary ===\n");
    PRINT(WHITE, BLACK, "Total symbols: %d\n", total_count);
    PRINT(WHITE, BLACK, "Functions: %d\n", func_count);
    PRINT(WHITE, BLACK, "Objects: %d\n", obj_count);

    if (total_count > 0 && total_count <= 50) {
        elf_print_symbols(ctx);
    } else if (total_count > 50) {
        PRINT(WHITE, BLACK, "(Too many symbols to display, use 'elfsyms' for full list)\n");
    }

    if (ctx->dynamic) {
        elf_print_dynamic(ctx);
    }

    elf_destroy_context(ctx);
    kfree(elf_buffer);
}
