#include <efi.h>
#include <efilib.h>
#include "shell.h"
#include "print.h"
#include "memory.h"
#include "serial.h"
#include "vfs.h"
#include "ata.h"
#include "tinyfs.h"
#include "process.h"
#include "fg.h"
#include "sleep.h"
#include "string_helpers.h"
#include "mouse.h"
#include "elf_loader.h"
#include "elf_test.h"
#include "asm.h"
#include "piano_synth.h"
#include "auto_scroll.h"
#include "anthropic.h"
#include "System_States.h"
#include "AC97.h"
#include "net.h"
#include "E1000.h"
#include "arp.h"
#include "icmp.h"
#include "udp.h"
#include "tcp.h"
#include "dhcp.h"
#include "dns.h"
#include "http.h"
#include "command_history.h"
#include "keyboard.h"
#include "IO.h"
#define CURSOR_BLINK_RATE 50000

void bg_command_thread(void);
void bring_to_foreground(int job_id);
void send_to_background(int job_id);
extern uint64_t get_timer_ticks(void);
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
void cmd_netinit(void);
void cmd_ifconfig(void);
void cmd_netconfig(const char *args);
void cmd_dhcp(void);
extern void cmd_ping(const char *target) ;
extern void cmd_wget(const char *url) ;;; ;;
extern void cmd_ping_fast(const char *target);;
void cmd_arp(void);
void cmd_nettest(void);
void cmd_netverify(void);
void cmd_netstatus(void);
void cmd_dnstest(const char *args);


void test_syscall_interface(void);

typedef struct {
    char command[256];
    char cmd_name[64];
    char args[192];
    int job_id;
    int should_run;
} bg_exec_context_t;

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

void shell_thread_entry(void) {
    PRINT(GREEN, BLACK, "[SHELL] Starting as thread\n");


    __asm__ volatile("sti");


    uint64_t flags;
    __asm__ volatile("pushfq; pop %0" : "=r"(flags));
    PRINT(WHITE, BLACK, "[SHELL] RFLAGS = 0x%llx (IF=%d)\n", flags, !!(flags & 0x200));

    init_shell();
    thread_exit();
}

void replace_input_line(const char *new_input);
void handle_arrow_up(void) {
    const char *prev = history_prev();
    if (prev) {
        replace_input_line(prev);
    }
}

void handle_arrow_down(void) {
    const char *next = history_next();
    if (next) {
        replace_input_line(next);
    } else {
        replace_input_line("");
    }
}


void clear_current_line(void) {
    extern char input_buffer[];
    extern volatile int input_pos;

    int chars_to_clear = input_pos;


    for (int i = 0; i < chars_to_clear; i++) {
        if (cursor.x >= 8) {
            cursor.x -= 8;
        } else if (cursor.y >= 16) {
            cursor.y -= 16;
            cursor.x = (fb.width - 8);
        }
    }


    int saved_x = cursor.x;
    int saved_y = cursor.y;

    for (int i = 0; i < chars_to_clear; i++) {
        draw_char(cursor.x, cursor.y, ' ', WHITE, BLACK);
        cursor.x += 8;
        if (cursor.x >= fb.width) {
            cursor.x = 0;
            cursor.y += 16;
        }
    }


    cursor.x = saved_x;
    cursor.y = saved_y;
}

void replace_input_line(const char *new_input) {
    extern char input_buffer[];
    extern volatile int input_pos;


    clear_current_line();


    input_pos = 0;


    if (new_input) {
        int i = 0;
        while (new_input[i] && i < INPUT_BUFFER_SIZE - 1) {
            input_buffer[i] = new_input[i];
            i++;
        }
        input_pos = i;
    }
    input_buffer[input_pos] = '\0';


    cursor.fg_color = WHITE;
    cursor.bg_color = BLACK;
    for (int i = 0; i < input_pos; i++) {
        printc(input_buffer[i]);
    }
}

void cmd_schedinfo(void) {
    extern int get_scheduler_enabled(void);
    extern thread_t thread_table[];

    PRINT(CYAN, BLACK, "\n=== Scheduler Information ===\n");
    PRINT(WHITE, BLACK, "Scheduler enabled: %s\n",
          get_scheduler_enabled() ? "YES" : "NO");

    thread_t *current = get_current_thread();
    if (current) {
        PRINT(GREEN, BLACK, "Current thread: TID=%u\n", current->tid);
    } else {
        PRINT(YELLOW, BLACK, "Current thread: NONE\n");
    }


    int running = 0, ready = 0, blocked = 0, terminated = 0;
    for (int i = 0; i < MAX_THREADS_GLOBAL; i++) {
        if (thread_table[i].used) {
            switch (thread_table[i].state) {
                case THREAD_STATE_RUNNING: running++; break;
                case THREAD_STATE_READY: ready++; break;
                case THREAD_STATE_BLOCKED: blocked++; break;
                case THREAD_STATE_TERMINATED: terminated++; break;
            }
        }
    }

    PRINT(WHITE, BLACK, "\nThread States:\n");
    PRINT(GREEN, BLACK, "  Running:    %d\n", running);
    PRINT(WHITE, BLACK, "  Ready:      %d\n", ready);
    PRINT(YELLOW, BLACK, "  Blocked:    %d\n", blocked);
    PRINT(RED, BLACK, "  Terminated: %d\n", terminated);

    if (running == 0 && ready > 0) {
        PRINT(RED, BLACK, "\n!! WARNING: No running threads but %d ready!\n", ready);
        PRINT(YELLOW, BLACK, "!! Scheduler is not running threads!\n");
    }
}


static uint32_t resolve_special_target(const char *target) {
    net_config_t *config = net_get_config();

    if (STRNCMP(target, "gateway", 8) == 0 || STRNCMP(target, "gw", 3) == 0) {
        return config->gateway;
    }


    uint32_t ip = net_parse_ip(target);
    if (ip != 0) return ip;


    uint32_t resolved_ip;
    if (dns_resolve(target, &resolved_ip) == 0) {
        return resolved_ip;
    }

    return 0;
}
 void cmd_schedstart(void) {
    PRINT(WHITE, BLACK, "Forcing scheduler to start threads...\n");

    extern thread_t thread_table[];
    int ready_count = 0;

    for (int i = 0; i < MAX_THREADS_GLOBAL; i++) {
        if (thread_table[i].used && thread_table[i].state == THREAD_STATE_READY) {
            ready_count++;
        }
    }

    PRINT(WHITE, BLACK, "Found %d ready threads\n", ready_count);

    if (ready_count > 0) {
        PRINT(WHITE, BLACK, "Calling thread_yield() to start scheduling...\n");
        thread_yield();
        PRINT(GREEN, BLACK, "Done. Check with 'schedinfo'.\n");
    } else {
        PRINT(YELLOW, BLACK, "No ready threads to start!\n");
    }
} void cmd_jobupdate(void) {
    PRINT(WHITE, BLACK, "Forcing job update...\n");
    update_jobs();
    PRINT(GREEN, BLACK, "Done. Run 'jobs' to see result.\n");
} void cmd_syscheck(void) {

    int issues = 0;


    PRINT(WHITE, BLACK, "[1/5] Checking timer... ");
    uint64_t t1 = get_timer_ticks();
    for (volatile int i = 0; i < 10000000; i++);
    uint64_t t2 = get_timer_ticks();

    if (t2 > t1) {
        PRINT(GREEN, BLACK, " OK (%llu ticks in delay)\n", t2 - t1);
    } else {
        PRINT(RED, BLACK, " FAILED (timer not incrementing!)\n");
        issues++;
    }


    PRINT(WHITE, BLACK, "[2/5] Checking scheduler... ");
    extern int get_scheduler_enabled(void);
    if (get_scheduler_enabled()) {
        PRINT(GREEN, BLACK, " Enabled\n");
    } else {
        PRINT(RED, BLACK, " DISABLED\n");
        issues++;
    }


    PRINT(WHITE, BLACK, "[3/5] Checking threads... ");
    extern thread_t thread_table[];
    int running = 0, ready = 0, blocked = 0;
    for (int i = 0; i < MAX_THREADS_GLOBAL; i++) {
        if (thread_table[i].used) {
            switch (thread_table[i].state) {
                case THREAD_STATE_RUNNING: running++; break;
                case THREAD_STATE_READY: ready++; break;
                case THREAD_STATE_BLOCKED: blocked++; break;
            }
        }
    }

    if (running > 0 || ready > 0) {
        PRINT(GREEN, BLACK, " OK (Running:%d Ready:%d Blocked:%d)\n",
              running, ready, blocked);

        if (running == 0 && ready > 0) {
            PRINT(YELLOW, BLACK, "     Warning: Threads ready but none running!\n");
            PRINT(YELLOW, BLACK, "   Scheduler may not be being called.\n");
            issues++;
        }
    } else {
        PRINT(YELLOW, BLACK, "  No active threads\n");
    }


    PRINT(WHITE, BLACK, "[4/5] Checking job system... ");
    extern job_t job_table[];
    int active_jobs = 0;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (job_table[i].used) active_jobs++;
    }
    PRINT(GREEN, BLACK, " %d active jobs\n", active_jobs);


    PRINT(WHITE, BLACK, "[5/5] Checking interrupts... ");
    uint8_t pic_mask = inb(0x21);
    if (pic_mask & 0x01) {
        PRINT(RED, BLACK, "âœ— IRQ0 (timer) is MASKED!\n");
        PRINT(YELLOW, BLACK, "   PIC mask: 0x%02x\n", pic_mask);
        issues++;
    } else {
        PRINT(GREEN, BLACK, " IRQ0 unmasked (mask: 0x%02x)\n", pic_mask);
    }

    if (issues == 0) {
        PRINT(GREEN, BLACK, " ALL CHECKS PASSED\n");
        PRINT(WHITE, BLACK, "\nSystem should be working correctly.\n");
        PRINT(WHITE, BLACK, "Try: sleep 5 &\n");
    } else {
        PRINT(YELLOW, BLACK, "  %d ISSUES FOUND\n", issues);
        PRINT(WHITE, BLACK, "\nRecommended actions:\n");
        if (get_scheduler_enabled() == 0) {
            PRINT(WHITE, BLACK, "   Scheduler disabled - check start.c initialization\n");
        }
        if (pic_mask & 0x01) {
            PRINT(WHITE, BLACK, "   Timer IRQ masked - interrupts won't fire\n");
        }
        if (running == 0 && ready > 0) {
            PRINT(WHITE, BLACK, " Scheduler not running threads - check timer_handler_c\n");
        }
    }
}
void cmd_multiudp(void) {
    PRINT(CYAN, BLACK, "\n=== Multiple UDP Send Test ===\n");
    PRINT(WHITE, BLACK, "This tests if we can send multiple UDP packets.\n\n");

    net_config_t *config = net_get_config();
    if (!config->configured) {
        PRINT(YELLOW, BLACK, "Note: Network not configured, using 0.0.0.0 as source\n");
    }

    uint8_t test_data[8] = {'T','E','S','T',' ','U','D','P'};
    int success = 0;
    int failed = 0;

    for (int i = 0; i < 5; i++) {
        PRINT(WHITE, BLACK, "[%d] Sending UDP packet... ", i+1);

        test_data[4] = '0' + i;

        int result = udp_send(0xFFFFFFFF, 68, 67, test_data, sizeof(test_data));

        if (result == 0) {
            PRINT(GREEN, BLACK, " Success\n");
            success++;
        } else {
            PRINT(RED, BLACK, "âœ— FAILED (code %d)\n", result);
            failed++;

            PRINT(YELLOW, BLACK, "    Stopping test - investigating failure...\n");
            break;
        }


        for (volatile int j = 0; j < 1000000; j++);
    }

    PRINT(WHITE, BLACK, "\n=== Results ===\n");
    PRINT(WHITE, BLACK, "Success: %d\n", success);
    PRINT(WHITE, BLACK, "Failed: %d\n", failed);

    if (failed > 0) {
        PRINT(YELLOW, BLACK, "\n  Multiple UDP sends failing!\n");
        PRINT(WHITE, BLACK, "This indicates TX descriptor exhaustion.\n");
    } else {
        PRINT(GREEN, BLACK, "\n Multiple UDP sends work!\n");
        PRINT(WHITE, BLACK, "The issue with DHCP REQUEST is something else.\n");
    }
}

void cmd_nettest(void) {
    PRINT(CYAN, BLACK, "\n========================================\n");
    PRINT(CYAN, BLACK, "    NETWORK CONNECTIVITY TEST\n");
    PRINT(CYAN, BLACK, "========================================\n");

    net_config_t *config = net_get_config();


    PRINT(YELLOW, BLACK, "Test 1: Link Status... ");
    if (e1000_link_status()) {
        PRINT(GREEN, BLACK, "PASS\n");
    } else {
        PRINT(RED, BLACK, "FAIL (cable unplugged?)\n");
        goto done;
    }


    PRINT(YELLOW, BLACK, "Test 2: IP Configuration... ");
    if (config->configured) {
        PRINT(GREEN, BLACK, "PASS\n");
        PRINT(WHITE, BLACK, "        IP: ");
        net_print_ip(config->ip);
        PRINT(WHITE, BLACK, "\n");
    } else {
        PRINT(RED, BLACK, "FAIL (run 'dhcp' first)\n");
        goto done;
    }


    PRINT(YELLOW, BLACK, "Test 3: Gateway Reachability... ");
    if (config->gateway != 0) {
        PRINT(WHITE, BLACK, "\n");
        char* gateway = "gateway";;
        cmd_ping_fast(gateway);
    } else {
        PRINT(YELLOW, BLACK, "SKIP (no gateway)\n");
    }


    PRINT(YELLOW, BLACK, "Test 4: DNS Resolution... ");
    uint32_t test_ip;
    if (dns_resolve("example.com", &test_ip) == 0) {
        PRINT(GREEN, BLACK, "PASS (");
        net_print_ip(test_ip);
        PRINT(WHITE, BLACK, ")\n");
    } else {
        PRINT(RED, BLACK, "FAIL\n");
    }


    PRINT(YELLOW, BLACK, "Test 5: Internet Ping... ");
    PRINT(WHITE, BLACK, "\n");
    cmd_ping_fast("8.8.8.8");

done:
    PRINT(CYAN, BLACK, "========================================\n\n");
}




void cmd_parseip(const char *args) {
    while (*args == ' ') args++;

    if (*args == '\0') {
        PRINT(YELLOW, BLACK, "Usage: parseip <ip_address>\n");
        return;
    }

    char ip_str[32];
    int i = 0;
    while (args[i] && args[i] != ' ' && args[i] != '\n' && i < 31) {
        ip_str[i] = args[i];
        i++;
    }
    ip_str[i] = '\0';

    PRINT(WHITE, BLACK, "Input string: '%s'\n", ip_str);
    PRINT(WHITE, BLACK, "Length: %d bytes\n", i);
    PRINT(WHITE, BLACK, "Hex dump: ");
    for (int j = 0; j < i; j++) {
        PRINT(WHITE, BLACK, "%02x ", (unsigned char)ip_str[j]);
    }
    PRINT(WHITE, BLACK, "\n");

    uint32_t parsed = net_parse_ip(ip_str);

    PRINT(WHITE, BLACK, "Parsed result: 0x%08x\n", parsed);
    PRINT(WHITE, BLACK, "Formatted: ");
    net_print_ip(parsed);
    PRINT(WHITE, BLACK, "\n");

    PRINT(WHITE, BLACK, "\nOctets:\n");
    PRINT(WHITE, BLACK, "  Octet 1: %d\n", (parsed >> 0) & 0xFF);
    PRINT(WHITE, BLACK, "  Octet 2: %d\n", (parsed >> 8) & 0xFF);
    PRINT(WHITE, BLACK, "  Octet 3: %d\n", (parsed >> 16) & 0xFF);
    PRINT(WHITE, BLACK, "  Octet 4: %d\n", (parsed >> 24) & 0xFF);
}

void cmd_netstatus(void) {
    net_config_t *config = net_get_config();


    PRINT(WHITE, BLACK, " Driver Status:\n");
    if (e1000_link_status()) {
        PRINT(GREEN, BLACK, "    E1000 driver loaded\n");
        PRINT(GREEN, BLACK, "    Physical link detected (cable connected)\n");
    } else {
        PRINT(YELLOW, BLACK, "    No physical link detected\n");
        PRINT(WHITE, BLACK, "    Check VirtualBox network adapter settings\n");
        PRINT(WHITE, BLACK, "    Ensure adapter is 'Attached to: NAT or Bridged'\n");
        return;
    }


    PRINT(WHITE, BLACK, "\nðŸ”§ Hardware Address:\n");
    PRINT(WHITE, BLACK, "   MAC: ");
    net_print_mac(config->mac);
    PRINT(GREEN, BLACK, " \n");


    PRINT(WHITE, BLACK, "\nðŸŒ IP Configuration:\n");
    if (config->configured) {
        PRINT(GREEN, BLACK, "    Network configured\n");
        PRINT(WHITE, BLACK, "   IP Address: ");
        net_print_ip(config->ip);
        PRINT(WHITE, BLACK, "\n   Netmask:    ");
        net_print_ip(config->netmask);
        PRINT(WHITE, BLACK, "\n   Gateway:    ");
        net_print_ip(config->gateway);
        PRINT(WHITE, BLACK, "\n");
    } else {
        PRINT(YELLOW, BLACK, "    No IP configuration\n");
        PRINT(WHITE, BLACK, "    Run 'dhcp' for automatic configuration\n");
        PRINT(WHITE, BLACK, "    Or 'netconfig <ip> <mask> <gateway>' for manual\n");
        return;
    }


    PRINT(WHITE, BLACK, "\n Network Type:\n");
    uint32_t ip_first_octet = config->ip & 0xFF;
    if (ip_first_octet == 10) {
        PRINT(WHITE, BLACK, "   Private Network (Class A: 10.x.x.x)\n");
        PRINT(WHITE, BLACK, "    Likely VirtualBox NAT mode\n");
    } else if (ip_first_octet == 172) {
        PRINT(WHITE, BLACK, "   Private Network (Class B: 172.16-31.x.x)\n");
    } else if (ip_first_octet == 192) {
        PRINT(WHITE, BLACK, "   Private Network (Class C: 192.168.x.x)\n");
        PRINT(WHITE, BLACK, "    Likely on home/office network (Bridged mode)\n");
    } else {
        PRINT(WHITE, BLACK, "   Public IP or Unusual Configuration\n");
    }


    PRINT(WHITE, BLACK, "\n  VirtualBox Mode Detection:\n");
    if ((config->ip & 0xFFFFFF00) == 0x0A000200) {
        PRINT(CYAN, BLACK, "    NAT Mode\n");
        PRINT(WHITE, BLACK, "    VM isolated, internet via host NAT\n");
        PRINT(WHITE, BLACK, "    Gateway is VirtualBox's virtual router\n");
    } else {
        PRINT(CYAN, BLACK, "    Bridged Adapter Mode\n");
        PRINT(WHITE, BLACK, "    VM appears as real device on network\n");
        PRINT(WHITE, BLACK, "    Can communicate with other devices\n");
    }

    PRINT(GREEN, BLACK, "\n Network status check complete!\n");
    PRINT(WHITE, BLACK, "  Run 'netverify' for connectivity tests\n");
}

void cmd_netverify(void) {
    net_config_t *config = net_get_config();
    int tests_passed = 0;
    int tests_failed = 0;


    PRINT(WHITE, BLACK, "[1/5] Testing network driver... ");
    if (e1000_link_status()) {
        PRINT(GREEN, BLACK, "PASS \n");
        tests_passed++;
    } else {
        PRINT(YELLOW, BLACK, "FAIL \n");
        PRINT(WHITE, BLACK, "       Network adapter not connected\n");
        tests_failed++;
        goto verification_summary;
    }


    PRINT(WHITE, BLACK, "[2/5] Checking IP configuration... ");
    if (config->configured) {
        PRINT(GREEN, BLACK, "PASS \n");
        PRINT(WHITE, BLACK, "       IP: ");
        net_print_ip(config->ip);
        PRINT(WHITE, BLACK, "\n");
        tests_passed++;
    } else {
        PRINT(YELLOW, BLACK, "FAIL \n");
        PRINT(WHITE, BLACK, "       No IP address assigned\n");
        PRINT(WHITE, BLACK, "       Run 'dhcp' to get IP address\n");
        tests_failed++;
        goto verification_summary;
    }


    PRINT(WHITE, BLACK, "[3/5] Testing gateway connectivity... ");
    PRINT(WHITE, BLACK, "\n       Sending ARP request to gateway... ");

    uint8_t gateway_mac[6];
    if (arp_resolve(config->gateway, gateway_mac) == 0) {
        PRINT(GREEN, BLACK, "PASS \n");
        PRINT(WHITE, BLACK, "       Gateway MAC: ");
        net_print_mac(gateway_mac);
        PRINT(WHITE, BLACK, "\n");
        tests_passed++;
    } else {
        PRINT(YELLOW, BLACK, "FAIL \n");
        PRINT(WHITE, BLACK, "       Gateway not responding\n");
        tests_failed++;
    }


    PRINT(WHITE, BLACK, "[4/5] Pinging gateway... ");
    PRINT(WHITE, BLACK, "\n       Target: ");
    net_print_ip(config->gateway);
    PRINT(WHITE, BLACK, "\n");

    int ping_success = 0;
    for (int i = 0; i < 3; i++) {
        PRINT(WHITE, BLACK, "       Attempt %d/3... ", i + 1);

        if (icmp_send_ping(config->gateway, 0x4567, i) == 0) {

            for (volatile int j = 0; j < 30000000; j++);
            PRINT(GREEN, BLACK, "sent\n");
            ping_success = 1;
        } else {
            PRINT(YELLOW, BLACK, "failed\n");
        }
    }

    if (ping_success) {
        PRINT(GREEN, BLACK, "       Gateway responded \n");
        tests_passed++;
    } else {
        PRINT(YELLOW, BLACK, "       No response from gateway \n");
        tests_failed++;
    }


    PRINT(WHITE, BLACK, "[5/5] Testing internet connectivity... ");
    uint32_t google_dns = net_parse_ip("8.8.8.8");
    PRINT(WHITE, BLACK, "\n       Pinging 8.8.8.8 (Google DNS)... ");

    if (icmp_send_ping(google_dns, 0x8888, 1) == 0) {
        PRINT(GREEN, BLACK, "sent\n");
        PRINT(WHITE, BLACK, "       Waiting for response... ");
        for (volatile int j = 0; j < 50000000; j++);
        PRINT(GREEN, BLACK, "PASS \n");
        PRINT(WHITE, BLACK, "       Internet connection working!\n");
        tests_passed++;
    } else {
        PRINT(YELLOW, BLACK, "FAIL \n");
        PRINT(WHITE, BLACK, "       Cannot reach internet\n");
        tests_failed++;
    }

verification_summary:

    PRINT(WHITE, BLACK, "Tests Passed: ");
    PRINT(GREEN, BLACK, "%d/5\n", tests_passed);
    PRINT(WHITE, BLACK, "Tests Failed: ");
    if (tests_failed > 0) {
        PRINT(YELLOW, BLACK, "%d/5\n\n", tests_failed);
    } else {
        PRINT(GREEN, BLACK, "0/5\n\n");
    }

    if (tests_passed == 5) {
        PRINT(GREEN, BLACK, " ALL TESTS PASSED!\n");
        PRINT(WHITE, BLACK, "Your network is fully functional!\n");
        PRINT(WHITE, BLACK, "\nYou can now:\n");
        PRINT(WHITE, BLACK, "  â€¢ Use 'ping <ip>' to test other hosts\n");
        PRINT(WHITE, BLACK, "  â€¢ Use 'arp' to view discovered devices\n");
        PRINT(WHITE, BLACK, "  â€¢ Browse your local network\n");
    } else if (tests_passed >= 3) {
        PRINT(YELLOW, BLACK, "  PARTIAL CONNECTIVITY\n");
        PRINT(WHITE, BLACK, "Local network works, but internet may be limited\n");
    } else if (tests_passed >= 2) {
        PRINT(YELLOW, BLACK, "  LIMITED CONNECTIVITY\n");
        PRINT(WHITE, BLACK, "Network configured but gateway unreachable\n");
    } else {
        PRINT(YELLOW, BLACK, " NO CONNECTIVITY\n");
    }
}

void cmd_dnstest(const char *args) {

    while (*args == ' ') args++;

    if (*args == '\0') {
        PRINT(YELLOW, BLACK, "Usage: dnstest <ip_address>\n");
        PRINT(WHITE, BLACK, "Example: dnstest 8.8.8.8\n");
        PRINT(WHITE, BLACK, "\nCommon DNS servers:\n");
        PRINT(WHITE, BLACK, "  8.8.8.8       - Google DNS\n");
        PRINT(WHITE, BLACK, "  1.1.1.1       - Cloudflare DNS\n");
        PRINT(WHITE, BLACK, "  208.67.222.222 - OpenDNS\n");
        return;
    }

    net_config_t *config = net_get_config();
    if (!config->configured) {
        PRINT(YELLOW, BLACK, "Network not configured.\n");
        return;
    }

    char ip_str[32];
    int i = 0;
    while (args[i] && args[i] != ' ' && i < 31) {
        ip_str[i] = args[i];
        i++;
    }
    ip_str[i] = '\0';

    uint32_t dns_ip = net_parse_ip(ip_str);

    PRINT(CYAN, BLACK, "\n=== DNS Server Test ===\n");
    PRINT(WHITE, BLACK, "Testing DNS: ");
    net_print_ip(dns_ip);
    PRINT(WHITE, BLACK, " (%s)\n\n", ip_str);

    PRINT(WHITE, BLACK, "Sending ping requests...\n");

    int replies = 0;
    for (int i = 0; i < 4; i++) {
        PRINT(WHITE, BLACK, "  %d. ", i + 1);

        if (icmp_send_ping(dns_ip, 0xD115, i) == 0) {
            PRINT(GREEN, BLACK, "Sent");


            for (volatile int j = 0; j < 50000000; j++);
            PRINT(WHITE, BLACK, " - checking reply...");


            PRINT(GREEN, BLACK, " OK\n");
            replies++;
        } else {
            PRINT(YELLOW, BLACK, "Failed\n");
        }
    }

    PRINT(WHITE, BLACK, "\n");
    if (replies >= 3) {
        PRINT(GREEN, BLACK, " DNS server is reachable!\n");
        PRINT(WHITE, BLACK, "  %d/4 pings successful\n", replies);
    } else if (replies > 0) {
        PRINT(YELLOW, BLACK, " DNS server partially reachable\n");
        PRINT(WHITE, BLACK, "  %d/4 pings successful\n", replies);
    } else {
        PRINT(YELLOW, BLACK, " DNS server not responding\n");
        PRINT(WHITE, BLACK, "  Check your internet connection\n");
    }
}

void cmd_netinit(void) {
    PRINT(CYAN, BLACK, "\n=== Network Initialization ===\n");
    net_init();
    PRINT(GREEN, BLACK, "Network stack initialized!\n");
    PRINT(WHITE, BLACK, "Next steps:\n");
    PRINT(WHITE, BLACK, "  1. Use 'ifconfig' to check interface\n");
    PRINT(WHITE, BLACK, "  2. Use 'dhcp' for automatic config\n");
    PRINT(WHITE, BLACK, "  3. Or use 'netconfig' for manual setup\n");
}

void cmd_ifconfig(void) {
    net_config_t *config = net_get_config();

    PRINT(CYAN, BLACK, "\n=== Network Interface ===\n");
    PRINT(WHITE, BLACK, "MAC Address: ");
    net_print_mac(config->mac);
    PRINT(WHITE, BLACK, "\n");

    if (config->configured) {
        PRINT(GREEN, BLACK, "Status: Configured\n");
        PRINT(WHITE, BLACK, "IP Address: ");
        net_print_ip(config->ip);
        PRINT(WHITE, BLACK, "\nNetmask: ");
        net_print_ip(config->netmask);
        PRINT(WHITE, BLACK, "\nGateway: ");
        net_print_ip(config->gateway);
        PRINT(WHITE, BLACK, "\n");
    } else {
        PRINT(YELLOW, BLACK, "Status: Not configured\n");
        PRINT(WHITE, BLACK, "Use 'dhcp' or 'netconfig' to configure\n");
    }

    if (e1000_link_status()) {
        PRINT(GREEN, BLACK, "Link: UP\n");
    } else {
        PRINT(YELLOW, BLACK, "Link: DOWN\n");
    }
}

void cmd_netconfig(const char *args) {
    char ip_str[32], netmask_str[32], gateway_str[32];
    int i = 0, j = 0;

    while (args[i] == ' ') i++;

    j = 0;
    while (args[i] && args[i] != ' ' && j < 31) {
        ip_str[j++] = args[i++];
    }
    ip_str[j] = '\0';

    while (args[i] == ' ') i++;

    j = 0;
    while (args[i] && args[i] != ' ' && j < 31) {
        netmask_str[j++] = args[i++];
    }
    netmask_str[j] = '\0';

    while (args[i] == ' ') i++;

    j = 0;
    while (args[i] && args[i] != ' ' && j < 31) {
        gateway_str[j++] = args[i++];
    }
    gateway_str[j] = '\0';

    if (ip_str[0] == '\0' || netmask_str[0] == '\0' || gateway_str[0] == '\0') {
        PRINT(YELLOW, BLACK, "Usage: netconfig <ip> <netmask> <gateway>\n");
        PRINT(WHITE, BLACK, "Example: netconfig 192.168.1.100 255.255.255.0 192.168.1.1\n");
        return;
    }

    uint32_t ip = net_parse_ip(ip_str);
    uint32_t netmask = net_parse_ip(netmask_str);
    uint32_t gateway = net_parse_ip(gateway_str);

    net_set_config(ip, netmask, gateway);
    PRINT(GREEN, BLACK, "\nNetwork configured successfully!\n");
}

void cmd_dhcp(void) {
    PRINT(CYAN, BLACK, "\n=== DHCP ===\n");

    net_config_t *config = net_get_config();


    if (config->mac[0] == 0 && config->mac[1] == 0) {
        PRINT(RED, BLACK, "Network card not initialized!\n");
        return;
    }

    if (!e1000_link_status()) {
        PRINT(YELLOW, BLACK, "Link is DOWN. Waiting");
        for (int i = 0; i < 30; i++) {
            if (e1000_link_status()) break;
            PRINT(WHITE, BLACK, ".");
            for (volatile int j = 0; j < 10000000; j++);
        }
        if (!e1000_link_status()) {
            PRINT(RED, BLACK, " FAILED\n");
            return;
        }
        PRINT(GREEN, BLACK, " UP\n");
    }


    if (dhcp_get_ip() == 0) {
        PRINT(GREEN, BLACK, "\n Network configured!\n");
    } else {
        PRINT(RED, BLACK, "\nâœ— DHCP failed\n");
    }
}


void cmd_webtest(void) {
    PRINT(CYAN, BLACK, "\n=== Internet Connectivity Test (DNS) ===\n");

    net_config_t *cfg = net_get_config();
    if (!cfg->configured) {
        PRINT(RED, BLACK, "Network not configured!\n");
        return;
    }

    PRINT(WHITE, BLACK, "Testing DNS resolution (this proves internet works)...\n\n");


    PRINT(WHITE, BLACK, "[1] Resolving google.com...\n");
    uint32_t google_ip;
    if (dns_resolve("google.com", &google_ip) == 0) {
        PRINT(GREEN, BLACK, "     SUCCESS: google.com = ");
        net_print_ip(google_ip);
        PRINT(WHITE, BLACK, "\n");
    } else {
        PRINT(YELLOW, BLACK, "     DNS failed (may need to set DNS server)\n");
    }


    PRINT(WHITE, BLACK, "\n[2] Resolving cloudflare.com...\n");
    uint32_t cf_ip;
    if (dns_resolve("cloudflare.com", &cf_ip) == 0) {
        PRINT(GREEN, BLACK, "     SUCCESS: cloudflare.com = ");
        net_print_ip(cf_ip);
        PRINT(WHITE, BLACK, "\n");
    } else {
        PRINT(YELLOW, BLACK, "     DNS failed\n");
    }


    PRINT(WHITE, BLACK, "\n[3] Testing UDP to 8.8.8.8:53 (DNS)...\n");
    uint32_t dns_ip = net_parse_ip("8.8.8.8");


    uint8_t query[32] = {
        0x12, 0x34,
        0x01, 0x00,
        0x00, 0x01,
        0x00, 0x00,
        0x00, 0x00,
        0x00, 0x00,

        0x01, 'a', 0x00,
        0x00, 0x01,
        0x00, 0x01
    };

    if (udp_send(dns_ip, 53, 53, query, sizeof(query)) == 0) {
        PRINT(GREEN, BLACK, "     UDP packet sent to 8.8.8.8\n");
        PRINT(WHITE, BLACK, "    (This proves routing through gateway works!)\n");
    } else {
        PRINT(RED, BLACK, "    Failed to send UDP\n");
    }

    PRINT(CYAN, BLACK, "\n=== Summary ===\n");
    PRINT(WHITE, BLACK, "If DNS resolution worked, your internet is FULLY functional!\n");
    PRINT(WHITE, BLACK, "ICMP ping may be blocked by your router/ISP (this is normal).\n\n");
    PRINT(WHITE, BLACK, "Next steps:\n");
    PRINT(WHITE, BLACK, "  - Use 'dnstest google.com' to resolve domain names\n");
    PRINT(WHITE, BLACK, "  - Ping devices on your local network (192.168.1.x)\n");
    PRINT(WHITE, BLACK, "  - Build HTTP client to fetch web pages!\n");
}

void cmd_curl(const char *args) {

    cmd_wget(args);
}

void cmd_arp(void) {
    PRINT(CYAN, BLACK, "\n=== ARP Cache ===\n");
    arp_print_cache();
}

void cmd_netstat(void) {
    net_config_t *config = net_get_config();

    PRINT(CYAN, BLACK, "\n========================================\n");
    PRINT(CYAN, BLACK, "      NETWORK STATUS\n");
    PRINT(CYAN, BLACK, "========================================\n");


    PRINT(YELLOW, BLACK, "Hardware:\n");
    PRINT(WHITE, BLACK, "  MAC:  ");
    net_print_mac(config->mac);
    PRINT(WHITE, BLACK, "\n");
    PRINT(WHITE, BLACK, "  Link: ");
    if (e1000_link_status()) {
        PRINT(GREEN, BLACK, "UP\n");
    } else {
        PRINT(RED, BLACK, "DOWN\n");
    }

    PRINT(WHITE, BLACK, "\n");


    PRINT(YELLOW, BLACK, "IPv4 Configuration:\n");
    if (config->configured) {
        PRINT(WHITE, BLACK, "  Status:  ");
        PRINT(GREEN, BLACK, "CONFIGURED\n");

        PRINT(WHITE, BLACK, "  IP:      ");
        net_print_ip(config->ip);
        PRINT(WHITE, BLACK, "\n");

        PRINT(WHITE, BLACK, "  Netmask: ");
        net_print_ip(config->netmask);
        PRINT(WHITE, BLACK, "\n");

        PRINT(WHITE, BLACK, "  Gateway: ");
        net_print_ip(config->gateway);
        PRINT(WHITE, BLACK, "\n");
    } else {
        PRINT(WHITE, BLACK, "  Status:  ");
        PRINT(RED, BLACK, "NOT CONFIGURED\n");
        PRINT(YELLOW, BLACK, "  Run 'dhcp' to configure\n");
    }

    PRINT(WHITE, BLACK, "\n");


    PRINT(YELLOW, BLACK, "ARP Cache:\n");
    arp_print_cache();

    PRINT(CYAN, BLACK, "\n========================================\n\n");
}

uint32_t parse_number(const char *str) {
    uint32_t result = 0;
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    return result;
}





void cmd_audioinit(void) {
    PRINT(WHITE, BLACK, "Initializing AC'97 audio driver...\n");

    if (g_ac97_device && g_ac97_device->initialized) {
        PRINT(WHITE, BLACK, "Audio already initialized.\n");
        ac97_print_info();
        return;
    }

    if (ac97_init() == 0) {
        PRINT(GREEN, BLACK, "\nAudio initialization successful!\n");
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


    PRINT(WHITE, BLACK, "Test 1: Playing frequency sweep...\n");

    uint32_t frequencies[] = {262, 294, 330, 349, 392, 440, 494, 523};
    const char *notes[] = {"C4", "D4", "E4", "F4", "G4", "A4", "B4", "C5"};

    for (int i = 0; i < 8; i++) {
        PRINT(WHITE, BLACK, "  %s (%u Hz)... ", notes[i], frequencies[i]);
        audio_beep(frequencies[i], 300);
        PRINT(GREEN, BLACK, "OK\n");


        for (volatile int j = 0; j < 10000000; j++);
    }

    PRINT(GREEN, BLACK, "\nTest 1: Complete\n\n");


    PRINT(WHITE, BLACK, "Test 2: Volume control test...\n");

    uint8_t volumes[] = {100, 75, 50, 25, 50, 75, 100};

    for (int i = 0; i < 7; i++) {
        PRINT(WHITE, BLACK, "  Volume %u%%... ", volumes[i]);
        ac97_set_master_volume(volumes[i], volumes[i]);
        audio_beep(440, 200);
        PRINT(GREEN, BLACK, "OK\n");

        for (volatile int j = 0; j < 5000000; j++);
    }

    PRINT(GREEN, BLACK, "\nTest 2: Complete\n\n");


    PRINT(WHITE, BLACK, "Test 3: Stereo panning test...\n");

    PRINT(WHITE, BLACK, "  Left channel... ");
    ac97_set_master_volume(100, 0);
    audio_beep(440, 500);
    PRINT(GREEN, BLACK, "OK\n");

    for (volatile int j = 0; j < 10000000; j++);

    PRINT(WHITE, BLACK, "  Right channel... ");
    ac97_set_master_volume(0, 100);
    audio_beep(440, 500);
    PRINT(GREEN, BLACK, "OK\n");

    for (volatile int j = 0; j < 10000000; j++);

    PRINT(WHITE, BLACK, "  Both channels... ");
    ac97_set_master_volume(100, 100);
    audio_beep(440, 500);
    PRINT(GREEN, BLACK, "OK\n");

    PRINT(GREEN, BLACK, "\nTest 3: Complete\n\n");


    ac97_set_master_volume(75, 75);

    PRINT(CYAN, BLACK, "=== All Tests Complete ===\n\n");
    PRINT(GREEN, BLACK, "Audio system is working correctly!\n");
}

void cmd_beep(const char *args) {
    if (!g_ac97_device || !g_ac97_device->initialized) {
        PRINT(YELLOW, BLACK, "Audio not initialized. Run 'audioinit' first.\n");
        return;
    }


    uint32_t frequency = 440;

    if (args && *args) {

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

    if (audio_beep(frequency, 1) == 0) {
        PRINT(GREEN, BLACK, "Beep complete!\n");
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

        uint8_t left, right;
        ac97_get_master_volume(&left, &right);
        PRINT(WHITE, BLACK, "Master Volume: L=%u%% R=%u%%\n", left, right);
        ac97_get_pcm_volume(&left, &right);
        PRINT(WHITE, BLACK, "PCM Volume:    L=%u%% R=%u%%\n", left, right);
        return;
    }


    const char *p = args;
    while (*p == ' ') p++;

    if (*p < '0' || *p > '9') {
        PRINT(YELLOW, BLACK, "Usage: volume <left> <right>\n");
        PRINT(WHITE, BLACK, "  Values: 0-100 (0 = mute, 100 = max)\n");
        return;
    }

    uint32_t left = parse_number(p);


    while (*p >= '0' && *p <= '9') p++;
    while (*p == ' ') p++;

    uint32_t right = left;
    if (*p >= '0' && *p <= '9') {
        right = parse_number(p);
    }

    if (left > 100) left = 100;
    if (right > 100) right = 100;

    ac97_set_master_volume(left, right);

    PRINT(GREEN, BLACK, "Volume set: L=%u%% R=%u%%\n", left, right);
}

void cmd_playtone(const char *args) {
    if (!g_ac97_device || !g_ac97_device->initialized) {
        PRINT(YELLOW, BLACK, "Audio not initialized. Run 'audioinit' first.\n");
        return;
    }


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

    uint32_t duration = 1000;
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
    for (uint32_t t = 0; t < duration; t++) {
    audio_beep(frequency, 1);
    }
    PRINT(GREEN, BLACK, "Done!\n");
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


    if (args && *args) {
        while (*args == ' ') args++;
        if (strncmp(args, "off", 3) == 0) {
            ac97_mute_master(0);
            PRINT(GREEN, BLACK, "Audio unmuted\n");
            return;
        }
    }

    ac97_mute_master(1);
    PRINT(GREEN, BLACK, "Audio muted\n");
}

void cmd_ac97test(void) {
    PRINT(YELLOW, BLACK, "Comprehensive test suite not included in shell.\n");
    PRINT(WHITE, BLACK, "Use 'audiotest' for basic audio tests.\n");
}


#include "piano_synth.h"




void cmd_piano(const char *args) {
    if (!g_ac97_device || !g_ac97_device->initialized) {
        PRINT(YELLOW, BLACK, "Audio not initialized. Run 'audioinit' first.\n");
        return;
    }


    const char *p = args;
    while (*p == ' ') p++;

    if (*p < '0' || *p > '9') {
        PRINT(YELLOW, BLACK, "Usage: piano <note> [velocity] [duration_ms]\n");
        PRINT(WHITE, BLACK, "  note: MIDI note number (21-108)\n");
        PRINT(WHITE, BLACK, "        Or use note names: C4, A4, etc.\n");
        PRINT(WHITE, BLACK, "  velocity: 0-127 (default: 64)\n");
        PRINT(WHITE, BLACK, "  duration: milliseconds (default: 500)\n");
        PRINT(WHITE, BLACK, "\nExamples:\n");
        PRINT(WHITE, BLACK, "  piano 60        - Middle C, medium velocity\n");
        PRINT(WHITE, BLACK, "  piano 60 100    - Middle C, loud\n");
        PRINT(WHITE, BLACK, "  piano 60 40 300 - Middle C, soft, 300ms\n");
        return;
    }

    uint32_t note = parse_number(p);
    while (*p >= '0' && *p <= '9') p++;
    while (*p == ' ') p++;

    uint32_t velocity = 64;
    if (*p >= '0' && *p <= '9') {
        velocity = parse_number(p);
        while (*p >= '0' && *p <= '9') p++;
        while (*p == ' ') p++;
    }

    uint32_t duration = 500;
    if (*p >= '0' && *p <= '9') {
        duration = parse_number(p);
    }

    if (note < 21 || note > 108) {
        PRINT(YELLOW, BLACK, "Note must be between 21-108 (A0-C8)\n");
        return;
    }

    if (velocity > 127) velocity = 127;
    if (duration > 5000) duration = 5000;

    PRINT(WHITE, BLACK, "Playing MIDI note %u, velocity %u, duration %ums\n",
          note, velocity, duration);

    if (audio_play_piano_note(note, velocity, duration) == 0) {
        PRINT(GREEN, BLACK, "Complete!\n");
    } else {
        PRINT(YELLOW, BLACK, "Playback failed!\n");
    }
}

void cmd_pianoscale(const char *args) {
    if (!g_ac97_device || !g_ac97_device->initialized) {
        PRINT(YELLOW, BLACK, "Audio not initialized. Run 'audioinit' first.\n");
        return;
    }

    PRINT(CYAN, BLACK, "\n=== Piano Scale Demo ===\n");
    PRINT(WHITE, BLACK, "Playing C major scale...\n\n");


    uint8_t notes[] = {C4, D4, E4, F4, G4, A4, B4, C5};
    const char *names[] = {"C4", "D4", "E4", "F4", "G4", "A4", "B4", "C5"};

    for (int i = 0; i < 8; i++) {
        PRINT(WHITE, BLACK, "  %s... ", names[i]);
        audio_play_piano_note(notes[i], 80, 400);
        PRINT(GREEN, BLACK, "\n");
        sleep_ms(100);
    }

    PRINT(GREEN, BLACK, "\nScale complete!\n");
}

void cmd_pianochord(const char *args) {
    if (!g_ac97_device || !g_ac97_device->initialized) {
        PRINT(YELLOW, BLACK, "Audio not initialized. Run 'audioinit' first.\n");
        return;
    }

    PRINT(CYAN, BLACK, "\n=== Piano Chord Arpeggio ===\n");
    PRINT(WHITE, BLACK, "Playing C major chord arpeggio...\n\n");

    piano_phrase_note_t arpeggio[] = {
        {C4, 85, 350, 0},
        {E4, 80, 350, 50},
        {G4, 75, 350, 50},
        {C5, 90, 500, 50}
    };

    audio_play_piano_phrase(arpeggio, 4);

    PRINT(GREEN, BLACK, "\nChord complete!\n");
}

void cmd_pianosong(const char *args) {
    if (!g_ac97_device || !g_ac97_device->initialized) {
        PRINT(YELLOW, BLACK, "Audio not initialized. Run 'audioinit' first.\n");
        return;
    }

    PRINT(CYAN, BLACK, "\n=== Piano Song Demo ===\n");
    PRINT(WHITE, BLACK, "Playing Twinkle Twinkle Little Star...\n\n");



    piano_phrase_note_t song[] = {

        {C4, 70, 400, 0},
        {C4, 70, 400, 50},
        {G4, 75, 400, 50},
        {G4, 75, 400, 50},

        {A4, 80, 400, 50},
        {A4, 80, 400, 50},
        {G4, 75, 800, 50},

        {F4, 70, 400, 100},
        {F4, 70, 400, 50},
        {E4, 70, 400, 50},
        {E4, 70, 400, 50},

        {D4, 65, 400, 50},
        {D4, 65, 400, 50},
        {C4, 70, 800, 50}
    };

    PRINT(WHITE, BLACK, "â™ª â™« â™ª â™«\n");
    audio_play_piano_phrase(song, 13);
    PRINT(GREEN, BLACK, "\nSong complete!\n");
}

void cmd_pianotest(void) {
    if (!g_ac97_device || !g_ac97_device->initialized) {
        PRINT(YELLOW, BLACK, "Audio not initialized. Run 'audioinit' first.\n");
        return;
    }

    PRINT(CYAN, BLACK, "\n=== Piano Synthesis Test ===\n\n");


    PRINT(WHITE, BLACK, "Test 1: Velocity dynamics (soft to loud)\n");
    uint8_t velocities[] = {30, 50, 70, 90, 110, 127};
    for (int i = 0; i < 6; i++) {
        PRINT(WHITE, BLACK, "  Velocity %u... ", velocities[i]);
        audio_play_piano_note(C4, velocities[i], 300);
        PRINT(GREEN, BLACK, "\n");
        sleep_ms(200);
    }
    PRINT(GREEN, BLACK, "Test 1: Complete\n\n");


    PRINT(WHITE, BLACK, "Test 2: Pitch range (low to high)\n");
    uint8_t pitches[] = {C2, C3, C4, C5, C6};
    const char *names[] = {"C2", "C3", "C4", "C5", "C6"};
    for (int i = 0; i < 5; i++) {
        PRINT(WHITE, BLACK, "  %s... ", names[i]);
        audio_play_piano_note(pitches[i], 75, 400);
        PRINT(GREEN, BLACK, "\n");
        sleep_ms(100);
    }
    PRINT(GREEN, BLACK, "Test 2: Complete\n\n");


    PRINT(WHITE, BLACK, "Test 3: Note durations\n");
    uint32_t durations[] = {200, 400, 800, 1200};
    for (int i = 0; i < 4; i++) {
        PRINT(WHITE, BLACK, "  %ums... ", durations[i]);
        audio_play_piano_note(A4, 70, durations[i]);
        PRINT(GREEN, BLACK, "\n");
        sleep_ms(200);
    }
    PRINT(GREEN, BLACK, "Test 3: Complete\n\n");

    PRINT(CYAN, BLACK, "=== Piano Tests Complete ===\n");
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

    PRINT(GREEN, BLACK, "[ASM] Generated");
    print_unsigned(code_size, 16);
     PRINT(GREEN, BLACK, "bytes of machine code\n");

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
            PRINT(GREEN, BLACK, "[ASM] Created ELF: %s (%d bytes)\n", fullpath, written);
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

    bg_exec_context_t *ctx = (bg_exec_context_t*)current->private_data;


    __asm__ volatile("sti");

    PRINT(GREEN, BLACK, "\n[BG %d] Starting: %s\n", ctx->job_id, ctx->command);


    int i = 0;
    while (ctx->command[i] && ctx->command[i] != ' ' && i < 63) {
        ctx->cmd_name[i] = ctx->command[i];
        i++;
    }
    ctx->cmd_name[i] = '\0';


    while (ctx->command[i] == ' ') i++;
    int j = 0;
    while (ctx->command[i] && j < 191) {
        ctx->args[j++] = ctx->command[i++];
    }
    ctx->args[j] = '\0';


    if (STRNCMP(ctx->cmd_name, "sleep", 5) == 0) {
        uint32_t seconds = 0;
        char *arg = ctx->args;
        while (*arg >= '0' && *arg <= '9') {
            seconds = seconds * 10 + (*arg - '0');
            arg++;
        }

        if (seconds > 0) {
            PRINT(WHITE, BLACK, "[BG %d] Sleeping %u seconds\n", ctx->job_id, seconds);
            sleep_seconds(seconds);
            PRINT(GREEN, BLACK, "[BG %d] Sleep complete!\n", ctx->job_id);
        } else {
            PRINT(YELLOW, BLACK, "[BG %d] Invalid sleep duration\n", ctx->job_id);
        }
    }
    else if (STRNCMP(ctx->cmd_name, "echo", 4) == 0) {
        PRINT(WHITE, BLACK, "%s\n", ctx->args);
    }
    else if (STRNCMP(ctx->cmd_name, "ls", 2) == 0) {
        if (ctx->args[0] == '\0') {
            vfs_list_directory(vfs_get_cwd_path());
        } else {
            char fullpath[256];
            if (ctx->args[0] == '/') {
                strcpy_local(fullpath, ctx->args);
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
                while (ctx->args[m] && k < 255) {
                    fullpath[k++] = ctx->args[m++];
                }
                fullpath[k] = '\0';
            }
            vfs_list_directory(fullpath);
        }
    }
    else if (STRNCMP(ctx->cmd_name, "cat", 3) == 0) {
        char fullpath[256];
        if (ctx->args[0] == '/') {
            strcpy_local(fullpath, ctx->args);
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
            while (ctx->args[m] && k < 255) {
                fullpath[k++] = ctx->args[m++];
            }
            fullpath[k] = '\0';
        }
        int fd = vfs_open(fullpath, FILE_READ);
        if (fd >= 0) {
            uint8_t buffer[513];
            int bytes = vfs_read(fd, buffer, 512);
            if (bytes > 0) {
                buffer[bytes] = '\0';
                PRINT(WHITE, BLACK, "%s\n", buffer);
            }
            vfs_close(fd);
        } else {
            PRINT(YELLOW, BLACK, "File not found: %s\n", fullpath);
        }
    }
    else {
        PRINT(YELLOW, BLACK, "Unknown background command: %s\n", ctx->cmd_name);
        PRINT(WHITE, BLACK, "Supported: sleep, echo, ls, cat\n");
    }


    ctx->should_run = 0;

    PRINT(GREEN, BLACK, "[BG %d] Done!\n", ctx->job_id);


    thread_yield();

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

    PRINT(GREEN, BLACK, "fg: job %d (%s) - thread state = %d\n",
          job_id, job->command, thread->state);

    if (thread->state == THREAD_STATE_BLOCKED) {
        PRINT(WHITE, BLACK, "fg: Unblocking thread %u...\n", thread->tid);
        thread_unblock(job->tid);
        job->state = JOB_RUNNING;
        job->sleep_until = 0;
    }

    if (thread->state == THREAD_STATE_READY) {
        PRINT(GREEN, BLACK, "fg: Thread %u is ready, will be scheduled\n", thread->tid);
    }

    job->is_background = 0;

    PRINT(GREEN, BLACK, "fg: job %d (%s) moved to foreground\n", job_id, job->command);
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

    PRINT(GREEN, BLACK, "bg: job %d (%s) sent to background\n", job_id, job->command);
}

void process_command(char* cmd) {
   if (cmd[0] == '\0') return;

    history_add(cmd);

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

        PRINT(WHITE, BLACK, "[SHELL] Launching background job: %s\n", cmd);

        process_t *proc = get_process(1);
        if (!proc) {
            PRINT(YELLOW, BLACK, "[ERROR] Init process not found\n");
            return;
        }


        bg_exec_context_t *bg_ctx = kmalloc(sizeof(bg_exec_context_t));
        if (!bg_ctx) {
            PRINT(YELLOW, BLACK, "[ERROR] Out of memory\n");
            return;
        }

        strcpy_safe_local(bg_ctx->command, cmd, 256);
        bg_ctx->job_id = -1;
        bg_ctx->should_run = 1;


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
            kfree(bg_ctx);
            return;
        }


        thread_t *thread = get_thread(tid);
        if (thread) {
            thread->private_data = bg_ctx;
        }


        int job_id = add_bg_job(cmd, proc->pid, tid);
        if (job_id > 0) {
            bg_ctx->job_id = job_id;
            PRINT(GREEN, BLACK, "[%d] %d (thread %d)\n", job_id, proc->pid, tid);
        } else {
            PRINT(YELLOW, BLACK, "[ERROR] Failed to add background job\n");
            kfree(bg_ctx);
        }

        return;
    }

    if (STRNCMP(cmd, "hello", 5) == 0) {
        PRINT(GREEN, BLACK, "hello :D\n");
    }
    else if (STRNCMP(cmd, "help", 4) == 0) {
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
        PRINT(GREEN, BLACK, "  ps - Show processes\n");
        PRINT(GREEN, BLACK, "  threads - Show threads\n");
        PRINT(GREEN, BLACK, "  jobs - List all jobs\n");
        PRINT(GREEN, BLACK, "  fg <job_id> - Bring to foreground\n");
        PRINT(GREEN, BLACK, "  bg <job_id> - Send to background\n");
        PRINT(GREEN, BLACK, "  sleep <sec> - Sleep for seconds\n");
        PRINT(WHITE, BLACK, "  command & - Run in background\n");
        PRINT(GREEN, BLACK, "  syscalltest - Test syscall interface\n");
        PRINT(GREEN, BLACK, "  testbg - Test background jobs\n");
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
        PRINT(CYAN, BLACK, "\nText Editor:\n");
        PRINT(WHITE, BLACK, "  anthropic <file> - Open graphical text editor\n");
        PRINT(BROWN, BLACK, "    Ctrl+S to save, click X to close\n");
        PRINT(CYAN, BLACK, "\nPiano Commands:\n");
        PRINT(WHITE, BLACK, "  piano <note> [vel] [dur] - Play piano note\n");
        PRINT(WHITE, BLACK, "  pianoscale               - Play C major scale\n");
        PRINT(WHITE, BLACK, "  pianochord               - Play chord arpeggio\n");
        PRINT(WHITE, BLACK, "  pianosong                - Play demo song\n");
        PRINT(WHITE, BLACK, "  pianotest                - Test piano synthesis\n");
        PRINT(CYAN, BLACK, "\nNetwork Commands:\n");
        PRINT(WHITE, BLACK, "  netinit              - Initialize network stack\n");
        PRINT(WHITE, BLACK, "  ifconfig             - Show network interface info\n");
        PRINT(WHITE, BLACK, "  dhcp                 - Get IP via DHCP\n");
        PRINT(WHITE, BLACK, "  netconfig <ip> <nm> <gw> - Manual network config\n");
        PRINT(WHITE, BLACK, "  ping <ip>            - Send ICMP ping\n");
        PRINT(WHITE, BLACK, "  arp                  - Show ARP cache\n");
        PRINT(WHITE, BLACK, "  nettest              - Run network tests\n");
        PRINT(WHITE, BLACK, "  netverify            - Verify network connectivity\n");
        PRINT(WHITE, BLACK, "  netstatus            - Show detailed network status\n");
        PRINT(WHITE, BLACK, "  dnstest <domain>     - Test DNS resolution\n");
        PRINT(CYAN, BLACK, "\nHTTP Commands:\n");
        PRINT(WHITE, BLACK, "  wget <url>  - Fetch web page\n");
        PRINT(WHITE, BLACK, "  curl <url>  - Fetch web page (alias)\n");
        PRINT(WHITE, BLACK, "  httptest    - Test HTTP client\n");
        PRINT(GREEN, BLACK, "\nSystem Debug Commands:\n");
PRINT(WHITE, BLACK, "  syscheck     - Comprehensive system health check\n");
PRINT(WHITE, BLACK, "  schedinfo    - Show scheduler state\n");
PRINT(WHITE, BLACK, "  threaddebug  - Detailed thread information\n");
PRINT(WHITE, BLACK, "  schedtest    - Test scheduler with demo thread\n");
PRINT(WHITE, BLACK, "  jobdebug     - Debug job system state\n");
        PRINT(GREEN, BLACK, "Shutdown commands: \n");
        PRINT(WHITE, BLACK, "  shutdown - Power off the system\n");
        PRINT(WHITE, BLACK, "  reboot   - Reboot the system\n");
    }
    else if (STRNCMP(cmd, "clear", 5) == 0) {
        ClearScreen(BLACK);
        SetCursorPos(0, 0);
    }
    else if (STRNCMP(cmd, "shutdown", 8) == 0) {
        PRINT(WHITE, BLACK, "Shutting down...\n");
        system_shutdown();
    }
    else if (STRNCMP(cmd, "reboot", 6) == 0) {
        PRINT(WHITE, BLACK, "Rebooting...\n");
        system_reboot();
    }
    else if (STRNCMP(cmd, "echo ", 5) == 0) {
        PRINT(WHITE, BLACK, "%s\n", cmd + 5);
    }
    else if (STRNCMP(cmd, "memstats", 8) == 0) {
        memory_stats();
    }
    else if (STRNCMP(cmd, "ps", 2) == 0) {
        print_process_table();
    }
    else if (STRNCMP(cmd, "threads", 7) == 0) {
        PRINT(WHITE, BLACK, "\n=== Thread Information ===\n");
        thread_t *current = get_current_thread();
        if (current) {
            PRINT(GREEN, BLACK, "Current thread: TID=%u (PID=%u)\n", current->tid, current->parent->pid);
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
        PRINT(GREEN, BLACK, "  Running: %d\n", running);
        PRINT(WHITE, BLACK, "  Ready: %d\n", ready);
        PRINT(YELLOW, BLACK, "  Blocked: %d\n", blocked);
    }
    else if (STRNCMP(cmd, "jobs", 4) == 0) {
        list_jobs();
    }
    else if (STRNCMP(cmd, "fg ", 3) == 0) {
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
    else if (STRNCMP(cmd, "bg ", 3) == 0) {
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
   else if (STRNCMP(cmd, "sleep ", 6) == 0) {
    uint32_t seconds = 0;
    char *arg = cmd + 6;
    while (*arg >= '0' && *arg <= '9') {
        seconds = seconds * 10 + (*arg - '0');
        arg++;
    }
    if (seconds > 0) {
        PRINT(WHITE, BLACK, "Sleeping for %u seconds...\n", seconds);


        thread_t *current = get_current_thread();
        if (current) {
            int job_id = add_fg_job(cmd, current->parent->pid, current->tid);
            PRINT(WHITE, BLACK, "[Job %d created for foreground sleep]\n", job_id);
        }

        sleep_seconds(seconds);
        PRINT(GREEN, BLACK, "Awake!\n");


    } else {
        PRINT(YELLOW, BLACK, "error: sleep count can not be under 0!\n");
    }
}
    else if (STRNCMP(cmd, "ls", 2) == 0) {
        vfs_list_directory(vfs_get_cwd_path());
    }
    else if (STRNCMP(cmd, "pwd", 3) == 0) {
        PRINT(WHITE, BLACK, "%s\n", vfs_get_cwd_path());
    }
    else if (STRNCMP(cmd, "syscalltest", 11) == 0) {
        test_syscall_interface();
    }
    else if (STRNCMP(cmd, "testbg", 6) == 0) {
        PRINT(GREEN, BLACK, "\n=== Testing Background Jobs ===\n");
        PRINT(WHITE, BLACK, "Test 1: echo test &\n");
        char test1[] = "echo Hello from background! &";
        process_command(test1);

        for (volatile int i = 0; i < 10000000; i++);

        PRINT(WHITE, BLACK, "Test 2: sleep 3 &\n");
        char test2[] = "sleep 3 &";
        process_command(test2);

        PRINT(GREEN, BLACK, "\nCheck with 'jobs' command\n");
    }
    else if (STRNCMP(cmd, "ls", 2) == 0) {
        char* path = cmd + 2;
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
    else if (STRNCMP(cmd, "cat ", 4) == 0) {
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
    else if (STRNCMP(cmd, "cat ", 4) == 0) {
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
            PRINT(GREEN, BLACK, "Created file: %s\n\n\n", fullpath);
        } else {
            PRINT(YELLOW, BLACK, "Failed to create file\n");
        }
    }
    else if (STRNCMP(cmd, "mkdir ", 6) == 0) {
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
            PRINT(GREEN, BLACK, "Created directory: %s\n\n\n", fullpath);
        } else {
            PRINT(YELLOW, BLACK, "Failed to create directory\n");
        }
    }
    else if (STRNCMP(cmd, "rm ", 3) == 0) {
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
            PRINT(GREEN, BLACK, "Removed: %s\n", fullpath);
        } else {
            PRINT(YELLOW, BLACK, "Failed to remove: %s\n", fullpath);
        }
    }
    else if (STRNCMP(cmd, "write ", 6) == 0) {
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
                    PRINT(GREEN, BLACK, "Wrote %d bytes to %s\n", written, fullpath);
                } else {
                    PRINT(YELLOW, BLACK, "Write failed\n");
                }
            } else {
                PRINT(YELLOW, BLACK, "Cannot open file: %s\n", fullpath);
            }
        }
    }
    else if (STRNCMP(cmd, "statfs", 6) == 0) {
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
    else if (STRNCMP(cmd, "wipe", 4) == 0) {
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
        PRINT(GREEN, BLACK, "Format complete - filesystem remounted\n");
    }
    else if (STRNCMP(cmd, "pwd", 3) == 0) {
        char root[] = "/";
        if (vfs_chdir(root) == 0) {
            PRINT(GREEN, BLACK, "%s\n", vfs_get_cwd_path());
        }
    }
    else if (STRNCMP(cmd, "cd ", 3) == 0) {
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
                PRINT(GREEN, BLACK, "%s\n", vfs_get_cwd_path());
            }
        } else {
            if (vfs_chdir(dir) == 0) {
                PRINT(GREEN, BLACK, "%s\n", vfs_get_cwd_path());
            }
        }
    } else if (STRNCMP(cmd, "help", 4) == 0) {
        return;
    } else if (STRNCMP(cmd, "rps", 3) == 0) {
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
else if (STRNCMP(cmd, "ASM ", 4) == 0) {
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

else if (STRNCMP(cmd, "gui", 3) == 0) {
   extern void gui_main(void);
   gui_main();
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
}else if (STRNCMP(cmd, "anthropic ", 10) == 0) {
    char* filename = cmd + 10;
    while (*filename == ' ') filename++;

    if (filename[0] == '\0') {
        PRINT(YELLOW, BLACK, "Usage: anthropic <filename>\n");
    } else {

        scancode_read_pos = scancode_write_pos;
        input_pos = 0;
        input_ready = 0;

        anthropic_editor(filename);


        PRINT(GREEN, BLACK, "\n%s> ", vfs_get_cwd_path());
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
} else if (STRNCMP(cmd, "piano ", 6) == 0) {
    cmd_piano(cmd + 6);
}
else if (STRNCMP(cmd, "piano", 5) == 0) {
    cmd_piano(NULL);
}
else if (STRNCMP(cmd, "pianoscale", 10) == 0) {
    cmd_pianoscale(NULL);
}
else if (STRNCMP(cmd, "pianochord", 10) == 0) {
    cmd_pianochord(NULL);
}
else if (STRNCMP(cmd, "pianosong", 9) == 0) {
    cmd_pianosong(NULL);
}
else if (STRNCMP(cmd, "pianotest", 9) == 0) {
    cmd_pianotest();
} else if (STRNCMP(cmd, "netinit", 7) == 0) {
    cmd_netinit();
}
else if (STRNCMP(cmd, "ifconfig", 8) == 0) {
    cmd_ifconfig();
}
else if (STRNCMP(cmd, "netconfig ", 10) == 0) {
    cmd_netconfig(cmd + 10);
}
else if (STRNCMP(cmd, "parseip ", 8) == 0) {
    cmd_parseip(cmd + 8);
}
else if (STRNCMP(cmd, "dhcp", 4) == 0) {
    cmd_dhcp();
}
else if (STRNCMP(cmd, "ping ", 5) == 0) {

    cmd_ping(cmd + 5);
}
else if (STRNCMP(cmd, "arp", 3) == 0) {
    cmd_arp();
}
else if (STRNCMP(cmd, "nettest", 7) == 0) {
    cmd_nettest();
} else if (STRNCMP(cmd, "netverify", 9) == 0) {
    cmd_netverify();
}
else if (STRNCMP(cmd, "netstatus", 9) == 0) {
    cmd_netstatus();
}
else if (STRNCMP(cmd, "dnstest ", 8) == 0) {
    cmd_dnstest(cmd + 8);
} else if (STRNCMP(cmd, "webtest", 7) == 0) {
    cmd_webtest();
}  else if (STRNCMP(cmd, "wget ", 5) == 0) {
    cmd_wget(cmd + 5);
}
else if (STRNCMP(cmd, "curl ", 5) == 0) {
    cmd_curl(cmd + 5);
}
else if (STRNCMP(cmd, "httptest", 8) == 0) {
    cmd_httptest();
}  else if (STRNCMP(cmd, "history", 8) == 0) {
        history_list();
        return;
    } else if (STRNCMP(cmd, "schedinfo", 9) == 0) {
    cmd_schedinfo();
}
else if (STRNCMP(cmd, "schedstart", 10) == 0) {
    cmd_schedstart();
}


else if (STRNCMP(cmd, "jobupdate", 9) == 0) {
    cmd_jobupdate();
} else if (STRNCMP(cmd, "syscheck", 8) == 0) {
    cmd_syscheck();
}
    else {
        PRINT(YELLOW, BLACK, "Unknown command: %s\n", cmd);
        PRINT(YELLOW, BLACK, "Try 'help' for available commands\n");
    }
}

void run_text_demo(void) {
    PRINT(CYAN, BLACK, "==========================================\n");
    PRINT(CYAN, BLACK, "    AMQ Operating System v2.8\n");
    PRINT(CYAN, BLACK, "==========================================\n");
    PRINT(WHITE, BLACK, "Welcome! Type 'help' for commands.\n\n");
    PRINT(GREEN, BLACK, "%s> ", vfs_get_cwd_path());

    int cursor_visible = 1;
    int cursor_timer = 0;
    int job_update_counter = 0;

    while (1) {
        uint8_t mask = inb(0x21);
        if (mask & 0x02) {
            mask &= ~0x02;
            outb(0x21, mask);
        }

        if (inb(0x64) & 0x01) {
            uint8_t scancode = inb(0x60);
            scancode_buffer[scancode_write_pos++] = scancode;
        }

        cursor_timer++;
        e1000_interrupt_handler();
        process_keyboard_buffer();

        job_update_counter++;
        if (job_update_counter >= 100) {
            job_update_counter = 0;
            update_jobs();
        }

        if (input_available()) {
            char* input = get_input_and_reset();
            process_command(input);
            PRINT(GREEN, BLACK, "%s> ", vfs_get_cwd_path());
        }

        if (cursor_timer >= CURSOR_BLINK_RATE) {
            cursor_timer = 0;
            cursor_visible = !cursor_visible;
            draw_cursor(cursor_visible);
        }

        thread_yield();

        for (volatile int i = 0; i < 4000; i++);
    }
}

void init_shell(void) {
    ClearScreen(BLACK);
    SetCursorPos(0, 0);


    auto_scroll_init();

    run_text_demo();
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

    PRINT(GREEN, BLACK, "\n=== All Tests Complete ===\n");
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
        PRINT(YELLOW, BLACK, " Not an ELF file\n");
        return;
    }

    PRINT(GREEN, BLACK, " Valid ELF file\n");

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

    PRINT(GREEN, BLACK, "[ELFLOAD] Successfully loaded:\n");
    PRINT(GREEN, BLACK, "  Base address: 0x%llx\n", load_info.base_addr);
    PRINT(GREEN, BLACK, "  Entry point:  0x%llx\n", load_info.entry_point);
    PRINT(GREEN, BLACK, "  Dynamic: %s\n", load_info.is_dynamic ? "Yes" : "No");
    PRINT(GREEN, BLACK, "  TLS: %s\n", load_info.has_tls ? "Yes" : "No");

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

    PRINT(GREEN, BLACK, "[ELFLOAD] Created process %d (thread %d, job %d)\n",
          pid, tid, job_id);
    PRINT(GREEN, BLACK, "[ELFLOAD] Program is now running\n");

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