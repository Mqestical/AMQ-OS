// ========== http.c - FIXED WGET (Real-world Behavior) ==========
#include "http.h"
#include "tcp.h"
#include "dns.h"
#include "net.h"
#include "print.h"
#include "string_helpers.h"
#include "memory.h"
#include "E1000.h"
#include "vfs.h"
#define HTTP_RESPONSE_MAX 16384

static char http_response_buffer[HTTP_RESPONSE_MAX];
static volatile int http_response_len = 0;
static volatile int http_transfer_complete = 0;
static tcp_socket_t *http_socket = NULL;


static void extract_filename_from_url(const char *url, char *filename, int max_len) {
    const char *ptr = url;
    
    // Skip protocol
    if (STRNCMP(ptr, "http://", 7) == 0) {
        ptr += 7;
    } else if (STRNCMP(ptr, "https://", 8) == 0) {
        ptr += 8;
    }
    
    // Find last slash to get filename
    const char *last_slash = NULL;
    while (*ptr) {
        if (*ptr == '/') {
            last_slash = ptr;
        }
        ptr++;
    }
    
    // If we found a slash and there's content after it
    if (last_slash && *(last_slash + 1) != '\0') {
        int i = 0;
        last_slash++; // Move past the slash
        
        // Copy filename (stop at query parameters or fragment)
        while (*last_slash && *last_slash != '?' && *last_slash != '#' && i < max_len - 1) {
            filename[i++] = *last_slash++;
        }
        filename[i] = '\0';
        
        // If we got a valid filename, return
        if (i > 0) {
            return;
        }
    }
    
    // Default filename if we couldn't extract one
    const char *default_name = "index.html";
    int i = 0;
    while (default_name[i] && i < max_len - 1) {
        filename[i] = default_name[i];
        i++;
    }
    filename[i] = '\0';
}


// Parse URL: http://example.com/path -> host, path, port
static int parse_url(const char *url, char *host, char *path, uint16_t *port) {
    const char *ptr = url;
    
    // Skip http:// or https://
    if (STRNCMP(ptr, "http://", 7) == 0) {
        ptr += 7;
    } else if (STRNCMP(ptr, "https://", 8) == 0) {
        PRINT(YELLOW, BLACK, "[WGET] Warning: HTTPS not supported, attempting HTTP\n");
        ptr += 8;
    }
    
    // Extract host
    int i = 0;
    while (*ptr && *ptr != '/' && *ptr != ':' && i < 255) {
        host[i++] = *ptr++;
    }
    host[i] = '\0';
    
    if (i == 0) {
        PRINT(RED, BLACK, "[WGET] Error: No hostname in URL\n");
        return -1;
    }
    
    // Check for port
    *port = 80;  // default
    if (*ptr == ':') {
        ptr++;
        *port = 0;
        while (*ptr >= '0' && *ptr <= '9') {
            *port = (*port * 10) + (*ptr - '0');
            ptr++;
        }
        if (*port == 0 || *port > 65535) {
            PRINT(RED, BLACK, "[WGET] Error: Invalid port\n");
            return -1;
        }
    }
    
    // Extract path
    if (*ptr == '/' && *(ptr + 1) != '\0') {
        i = 0;
        while (*ptr && i < 255) {
            path[i++] = *ptr++;
        }
        path[i] = '\0';
    } else {
        path[0] = '/';
        path[1] = '\0';
    }
    
    return 0;
}

int http_get(const char *url, char *output, int max_len) {
    char host[256];
    char path[256];
    uint16_t port;
    
    PRINT(CYAN, BLACK, "[WGET] Parsing URL...\n");
    
    // Parse URL
    if (parse_url(url, host, path, &port) != 0) {
        return -1;
    }
    
    PRINT(WHITE, BLACK, "[WGET] Host: %s\n", host);
    PRINT(WHITE, BLACK, "[WGET] Path: %s\n", path);
    PRINT(WHITE, BLACK, "[WGET] Port: %d\n", port);
    
    // Resolve hostname
    PRINT(CYAN, BLACK, "[WGET] Resolving %s...\n", host);
    uint32_t ip;
    if (dns_resolve(host, &ip) != 0) {
        PRINT(RED, BLACK, "[WGET] ERROR: DNS resolution failed\n");
        PRINT(YELLOW, BLACK, "[WGET] Possible causes:\n");
        PRINT(WHITE, BLACK, "  - DNS server not configured (run 'dhcp' first)\n");
        PRINT(WHITE, BLACK, "  - Hostname doesn't exist\n");
        PRINT(WHITE, BLACK, "  - Network connectivity issue\n");
        return -1;
    }
    
    PRINT(GREEN, BLACK, "[WGET] Resolved to ");
    net_print_ip(ip);
    PRINT(WHITE, BLACK, "\n");
    
    // Create socket
    PRINT(CYAN, BLACK, "[WGET] Creating TCP socket...\n");
    http_socket = tcp_socket();
    if (!http_socket) {
        PRINT(RED, BLACK, "[WGET] ERROR: Failed to create socket\n");
        return -1;
    }
    PRINT(GREEN, BLACK, "[WGET] Socket created\n");
    
    // Reset state
    http_response_len = 0;
    http_transfer_complete = 0;
    
    // Connect
    PRINT(CYAN, BLACK, "[WGET] Connecting to ");
    net_print_ip(ip);
    PRINT(WHITE, BLACK, ":%d...\n", port);
    
    if (tcp_connect(http_socket, ip, port) != 0) {
        PRINT(RED, BLACK, "[WGET] ERROR: Connection failed\n");
        PRINT(YELLOW, BLACK, "[WGET] Possible causes:\n");
        PRINT(WHITE, BLACK, "  - Host unreachable\n");
        PRINT(WHITE, BLACK, "  - Port blocked or closed\n");
        PRINT(WHITE, BLACK, "  - Gateway not configured\n");
        http_socket = NULL;
        return -1;
    }
    
    PRINT(GREEN, BLACK, "[WGET] Connected!\n");
    
    // Build HTTP/1.0 request (like real wget)
    char request[1024];
    int req_len = 0;
    
    // GET /path HTTP/1.0\r\n
    const char *get = "GET ";
    while (*get) request[req_len++] = *get++;
    
    const char *p = path;
    while (*p) request[req_len++] = *p++;
    
    const char *http10 = " HTTP/1.0\r\n";
    while (*http10) request[req_len++] = *http10++;
    
    // Host: hostname\r\n
    const char *host_hdr = "Host: ";
    while (*host_hdr) request[req_len++] = *host_hdr++;
    
    const char *h = host;
    while (*h) request[req_len++] = *h++;
    
    const char *crlf = "\r\n";
    while (*crlf) request[req_len++] = *crlf++;
    
    // User-Agent: wget/1.0 (like real wget includes this)
    const char *ua = "User-Agent: wget/1.0\r\n";
    while (*ua) request[req_len++] = *ua++;
    
    // Connection: close\r\n
    const char *conn = "Connection: close\r\n";
    while (*conn) request[req_len++] = *conn++;
    
    // Empty line to end headers
    request[req_len++] = '\r';
    request[req_len++] = '\n';
    request[req_len] = '\0';
    
    PRINT(CYAN, BLACK, "[WGET] Sending HTTP request (%d bytes)...\n", req_len);
    PRINT(YELLOW, BLACK, "[WGET] Request:\n%s", request);
    
    if (tcp_send(http_socket, request, req_len) != 0) {
        PRINT(RED, BLACK, "[WGET] ERROR: Failed to send request\n");
        tcp_close(http_socket);
        http_socket = NULL;
        return -1;
    }
    
    PRINT(GREEN, BLACK, "[WGET] Request sent!\n");
    
    // Wait for response (like real wget with progress)
    PRINT(CYAN, BLACK, "[WGET] Receiving response");
    
    int timeout = 10000;  // 10 seconds total timeout
    int no_data_count = 0;
    int last_len = 0;
    int dots_printed = 0;
    
    for (int i = 0; i < timeout; i++) {
        // Aggressive network polling
        for (int p = 0; p < 50; p++) {
            e1000_interrupt_handler();
        }
        
        // Check if we got new data
        if (http_response_len > last_len) {
            last_len = http_response_len;
            no_data_count = 0;
            
            // Print progress dots
            if ((i % 100) == 0 && dots_printed < 50) {
                PRINT(WHITE, BLACK, ".");
                dots_printed++;
            }
        } else {
            no_data_count++;
        }
        
        // Check if connection closed
        int state = tcp_get_state(http_socket);
        if (state == TCP_STATE_CLOSED) {
            if (http_response_len > 0) {
                PRINT(WHITE, BLACK, " done\n");
                PRINT(GREEN, BLACK, "[WGET] Transfer complete (connection closed)\n");
                break;
            } else {
                PRINT(WHITE, BLACK, " failed\n");
                PRINT(RED, BLACK, "[WGET] ERROR: Connection closed without data\n");
                tcp_close(http_socket);
                http_socket = NULL;
                return -1;
            }
        }
        
        // If we have data and no new data for a while, consider it complete
        if (http_response_len > 0 && no_data_count > 500) {
            PRINT(WHITE, BLACK, " done\n");
            PRINT(YELLOW, BLACK, "[WGET] Transfer appears complete (no new data)\n");
            break;
        }
        
        // Small delay
        for (volatile int j = 0; j < 5000; j++);
    }
    
    // Clean up
    tcp_close(http_socket);
    http_socket = NULL;
    
    if (http_response_len == 0) {
        PRINT(RED, BLACK, "[WGET] ERROR: No data received\n");
        return -1;
    }
    
    // Copy to output buffer
    int copy_len = (http_response_len < max_len - 1) ? http_response_len : (max_len - 1);
    for (int i = 0; i < copy_len; i++) {
        output[i] = http_response_buffer[i];
    }
    output[copy_len] = '\0';
    
    PRINT(GREEN, BLACK, "[WGET] Received %d bytes total\n", http_response_len);
    
    return http_response_len;
}

// Called by TCP layer when data arrives
void http_tcp_data_received(uint8_t *data, int len) {
    if (http_response_len + len < HTTP_RESPONSE_MAX) {
        for (int i = 0; i < len; i++) {
            http_response_buffer[http_response_len++] = data[i];
        }
    } else {
        PRINT(YELLOW, BLACK, "[WGET] Warning: Response buffer full, truncating\n");
        int remaining = HTTP_RESPONSE_MAX - http_response_len;
        for (int i = 0; i < remaining; i++) {
            http_response_buffer[http_response_len++] = data[i];
        }
    }
}

// ========== WGET COMMAND (Like Real Wget) ==========

void cmd_wget(const char *url) {
    if (!url || url[0] == '\0') {
        PRINT(CYAN, BLACK, "\nUsage: wget <url> [-o output_file]\n");
        PRINT(WHITE, BLACK, "Examples:\n");
        PRINT(WHITE, BLACK, "  wget http://example.com\n");
        PRINT(WHITE, BLACK, "  wget http://example.com/page.html\n");
        PRINT(WHITE, BLACK, "  wget http://example.com -o myfile.html\n");
        PRINT(WHITE, BLACK, "  wget http://httpbin.org/ip\n");
        return;
    }
    
    // Check if network is configured
    net_config_t *config = net_get_config();
    if (!config->configured) {
        PRINT(RED, BLACK, "\n[WGET] ERROR: Network not configured!\n");
        PRINT(YELLOW, BLACK, "[WGET] Run 'dhcp' first to configure network\n");
        return;
    }
    
    // Parse arguments to find output filename
    char url_only[512];
    char output_filename[256];
    int has_custom_output = 0;
    
    // Extract URL and check for -o flag
    int i = 0;
    while (url[i] && url[i] != ' ' && i < 511) {
        url_only[i] = url[i];
        i++;
    }
    url_only[i] = '\0';
    
    // Check for -o flag
    while (url[i] == ' ') i++;
    if (url[i] == '-' && url[i+1] == 'o') {
        i += 2;
        while (url[i] == ' ') i++;
        
        int j = 0;
        while (url[i] && url[i] != ' ' && j < 255) {
            output_filename[j++] = url[i++];
        }
        output_filename[j] = '\0';
        
        if (j > 0) {
            has_custom_output = 1;
        }
    }
    
    // If no custom output, extract from URL
    if (!has_custom_output) {
        extract_filename_from_url(url_only, output_filename, 256);
    }
    
    PRINT(CYAN, BLACK, "\n========================================\n");
    PRINT(CYAN, BLACK, "           WGET\n");
    PRINT(CYAN, BLACK, "========================================\n");
    PRINT(WHITE, BLACK, "URL: %s\n", url_only);
    PRINT(WHITE, BLACK, "Saving to: %s\n", output_filename);
    PRINT(CYAN, BLACK, "----------------------------------------\n");
    
    char response[8192];
    int len = http_get(url_only, response, sizeof(response));
    
    if (len > 0) {
        PRINT(GREEN, BLACK, "[WGET] Downloaded %d bytes\n\n", len);
        
        // Find the body (after headers)
        int body_start = 0;
        for (int i = 0; i < len - 3; i++) {
            if (response[i] == '\r' && response[i+1] == '\n' && 
                response[i+2] == '\r' && response[i+3] == '\n') {
                body_start = i + 4;
                break;
            }
        }
        
        // Prepare full path
        char fullpath[512];
        const char *cwd = vfs_get_cwd_path();
        
        // Build full path
        int idx = 0;
        if (output_filename[0] == '/') {
            // Absolute path
            while (output_filename[idx] && idx < 511) {
                fullpath[idx] = output_filename[idx];
                idx++;
            }
        } else {
            // Relative path - prepend cwd
            int j = 0;
            while (cwd[j] && idx < 510) {
                fullpath[idx++] = cwd[j++];
            }
            if (idx > 0 && fullpath[idx-1] != '/') {
                fullpath[idx++] = '/';
            }
            j = 0;
            while (output_filename[j] && idx < 511) {
                fullpath[idx++] = output_filename[j++];
            }
        }
        fullpath[idx] = '\0';
        
        PRINT(WHITE, BLACK, "[WGET] Saving to: %s\n", fullpath);
        
        // Create the file if it doesn't exist
        int fd = vfs_open(fullpath, FILE_WRITE);
        if (fd < 0) {
            PRINT(WHITE, BLACK, "[WGET] Creating file...\n");
            if (vfs_create(fullpath, FILE_READ | FILE_WRITE) != 0) {
                PRINT(RED, BLACK, "[WGET] ERROR: Failed to create file\n");
                goto show_preview;
            }
            fd = vfs_open(fullpath, FILE_WRITE);
        }
        
        if (fd >= 0) {
            // Write content to file
            int write_len = (body_start > 0) ? (len - body_start) : len;
            uint8_t *write_data = (body_start > 0) ? 
                (uint8_t*)(response + body_start) : (uint8_t*)response;
            
            int written = vfs_write(fd, write_data, write_len);
            vfs_close(fd);
            
            if (written > 0) {
                PRINT(GREEN, BLACK, "[WGET] File saved successfully (%d bytes)\n\n", written);
            } else {
                PRINT(RED, BLACK, "[WGET] âœ— Write failed\n");
                goto show_preview;
            }
        } else {
            PRINT(RED, BLACK, "[WGET] ERROR: Cannot open file for writing\n");
            goto show_preview;
        }
        
show_preview:
        // Show preview of content
        PRINT(CYAN, BLACK, "========================================\n");
        PRINT(CYAN, BLACK, "           PREVIEW\n");
        PRINT(CYAN, BLACK, "========================================\n");
        
        if (body_start > 0) {
            // Show headers
            PRINT(YELLOW, BLACK, "--- Headers ---\n");
            for (int i = 0; i < body_start - 2 && i < 400; i++) {
                PRINT(WHITE, BLACK, "%c", response[i]);
            }
            PRINT(WHITE, BLACK, "\n");
            
            // Show body preview
            PRINT(GREEN, BLACK, "--- Body (first 512 bytes) ---\n");
            int body_len = len - body_start;
            int show_len = body_len < 512 ? body_len : 512;
            
            for (int i = 0; i < show_len; i++) {
                PRINT(WHITE, BLACK, "%c", response[body_start + i]);
            }
            
            if (body_len > 512) {
                PRINT(YELLOW, BLACK, "\n\n... (truncated, full content saved to file)\n");
            }
        } else {
            // No clear separation, show first 512 bytes
            int show_len = len < 512 ? len : 512;
            for (int i = 0; i < show_len; i++) {
                PRINT(WHITE, BLACK, "%c", response[i]);
            }
            if (len > 512) {
                PRINT(YELLOW, BLACK, "\n\n... (truncated)\n");
            }
        }
        
        PRINT(CYAN, BLACK, "\n========================================\n");
        PRINT(GREEN, BLACK, " Download complete\n");
        PRINT(WHITE, BLACK, "File: %s (%d bytes)\n", output_filename, 
              body_start > 0 ? (len - body_start) : len);
        PRINT(CYAN, BLACK, "========================================\n\n");
        
    } else {
        PRINT(CYAN, BLACK, "========================================\n");
        PRINT(RED, BLACK, "âœ— FAILED: Could not retrieve URL\n");
        PRINT(CYAN, BLACK, "========================================\n\n");
    }
}

// ========== SIMPLIFIED TEST COMMAND ==========

void cmd_httptest(void) {
    PRINT(CYAN, BLACK, "\n========================================\n");
    PRINT(CYAN, BLACK, "        HTTP CLIENT TEST\n");
    PRINT(CYAN, BLACK, "========================================\n");
    
    // Check network configuration
    net_config_t *config = net_get_config();
    if (!config->configured) {
        PRINT(RED, BLACK, "ERROR: Network not configured!\n");
        PRINT(YELLOW, BLACK, "Run 'dhcp' command first\n");
        PRINT(CYAN, BLACK, "========================================\n");
        return;
    }
    
    PRINT(WHITE, BLACK, "Testing with http://example.com\n");
    PRINT(CYAN, BLACK, "----------------------------------------\n");
    
    char response[4096];
    int len = http_get("http://example.com", response, sizeof(response));
    
    if (len > 0) {
        PRINT(CYAN, BLACK, "----------------------------------------\n");
        PRINT(GREEN, BLACK, "✓ SUCCESS!\n");
        PRINT(WHITE, BLACK, "Received %d bytes\n\n", len);
        
        // Show first 400 chars
        PRINT(WHITE, BLACK, "First 400 characters:\n");
        PRINT(CYAN, BLACK, "---\n");
        
        int show_len = len < 400 ? len : 400;
        for (int i = 0; i < show_len; i++) {
            PRINT(WHITE, BLACK, "%c", response[i]);
        }
        
        PRINT(CYAN, BLACK, "\n---\n");
    } else {
        PRINT(RED, BLACK, "✗ FAILED\n");
    }
    
    PRINT(CYAN, BLACK, "========================================\n\n");
}