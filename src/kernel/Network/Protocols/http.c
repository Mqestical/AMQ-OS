// ========== HTTP REQUEST DEBUGGER AND FIXED IMPLEMENTATION ==========

/*
 * APACHE 400 BAD REQUEST DIAGNOSIS
 * 
 * Apache returns "error 0x190" (hex for 400) when:
 * 1. Request line is malformed (missing space, invalid HTTP version)
 * 2. Headers contain invalid characters or formatting
 * 3. Request doesn't end with \r\n\r\n
 * 4. There are NUL bytes in the request
 * 5. Host header is missing or malformed (HTTP/1.1)
 * 
 * Common causes in embedded systems:
 * - Using \n instead of \r\n
 * - Path contains NUL byte or is empty
 * - Extra spaces in request line
 * - Missing space between method and path
 */

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

// Hex dump for debugging
static void hex_dump(const char *label, const uint8_t *data, int len) {
    PRINT(CYAN, BLACK, "[DEBUG] %s (%d bytes):\n", label, len);
    
    for (int i = 0; i < len; i++) {
        // Print offset
        if (i % 16 == 0) {
            PRINT(YELLOW, BLACK, "%llx: ", i);
        }
        
        // Print hex
        PRINT(WHITE, BLACK, "%llx ", data[i]);
        
        // Print ASCII representation at end of line
        if (i % 16 == 15 || i == len - 1) {
            // Pad if necessary
            for (int j = i % 16; j < 15; j++) {
                PRINT(WHITE, BLACK, "   ");
            }
            
            PRINT(WHITE, BLACK, " | ");
            
            // Print ASCII
            int start = i - (i % 16);
            int end = (i % 16 == 15) ? i + 1 : i + 1;
            
            for (int j = start; j < end; j++) {
                if (data[j] >= 32 && data[j] <= 126) {
                    PRINT(WHITE, BLACK, "%c", data[j]);
                } else if (data[j] == '\r') {
                    PRINT(MAGENTA, BLACK, "\\r");
                } else if (data[j] == '\n') {
                    PRINT(MAGENTA, BLACK, "\\n");
                } else {
                    PRINT(YELLOW, BLACK, ".");
                }
            }
            PRINT(WHITE, BLACK, "\n");
        }
    }
}

// Validate HTTP request before sending
static int validate_http_request(const char *request, int len) {
    PRINT(CYAN, BLACK, "\n[VALIDATION] Checking HTTP request...\n");
    
    int errors = 0;
    
    // Check 1: Must start with valid method
    if (len < 4 || !(request[0] == 'G' && request[1] == 'E' && request[2] == 'T' && request[3] == ' ')) {
        PRINT(RED, BLACK, "[VALIDATION] ✗ Doesn't start with 'GET '\n");
        errors++;
    } else {
        PRINT(GREEN, BLACK, "[VALIDATION] ✓ Starts with 'GET '\n");
    }
    
    // Check 2: Find request line end
    int req_line_end = -1;
    for (int i = 0; i < len - 1; i++) {
        if (request[i] == '\r' && request[i+1] == '\n') {
            req_line_end = i;
            break;
        }
    }
    
    if (req_line_end == -1) {
        PRINT(RED, BLACK, "[VALIDATION] ✗ No \\r\\n found in request line\n");
        errors++;
    } else {
        PRINT(GREEN, BLACK, "[VALIDATION] ✓ Request line ends at byte %d\n", req_line_end);
        
        // Extract and display request line
        PRINT(WHITE, BLACK, "[VALIDATION] Request line: '");
        for (int i = 0; i < req_line_end; i++) {
            PRINT(WHITE, BLACK, "%c", request[i]);
        }
        PRINT(WHITE, BLACK, "'\n");
        
        // Check for HTTP version
        if (req_line_end > 8) {
            int has_http = 0;
            for (int i = 0; i <= req_line_end - 8; i++) {
                if (request[i] == 'H' && request[i+1] == 'T' && request[i+2] == 'T' && request[i+3] == 'P') {
                    has_http = 1;
                    PRINT(GREEN, BLACK, "[VALIDATION] ✓ Found 'HTTP' at position %d\n", i);
                    break;
                }
            }
            if (!has_http) {
                PRINT(RED, BLACK, "[VALIDATION] ✗ No 'HTTP' found in request line\n");
                errors++;
            }
        }
    }
    
    // Check 3: Must end with \r\n\r\n
    if (len >= 4) {
        if (request[len-4] == '\r' && request[len-3] == '\n' && 
            request[len-2] == '\r' && request[len-1] == '\n') {
            PRINT(GREEN, BLACK, "[VALIDATION] ✓ Ends with \\r\\n\\r\\n\n");
        } else {
            PRINT(RED, BLACK, "[VALIDATION] ✗ Doesn't end with \\r\\n\\r\\n\n");
            PRINT(YELLOW, BLACK, "[VALIDATION] Last 4 bytes: 0x%llx 0x%llx 0x%llx 0x%llx\n",
                  (uint8_t)request[len-4], (uint8_t)request[len-3], 
                  (uint8_t)request[len-2], (uint8_t)request[len-1]);
            errors++;
        }
    }
    
    // Check 4: Look for Host header (required for HTTP/1.1)
    int has_host = 0;
    for (int i = 0; i < len - 5; i++) {
        if ((request[i] == '\r' && request[i+1] == '\n' && 
             request[i+2] == 'H' && request[i+3] == 'o' && request[i+4] == 's' && request[i+5] == 't') ||
            (i == 0 && request[i] == 'H' && request[i+1] == 'o' && request[i+2] == 's' && request[i+3] == 't')) {
            has_host = 1;
            PRINT(GREEN, BLACK, "[VALIDATION] ✓ Found Host header at position %d\n", i);
            break;
        }
    }
    
    if (!has_host) {
        PRINT(YELLOW, BLACK, "[VALIDATION] ⚠ No Host header found (required for HTTP/1.1)\n");
    }
    
    // Check 5: Check for NUL bytes
    for (int i = 0; i < len - 1; i++) {  // -1 because last byte might be intentional NUL
        if (request[i] == '\0') {
            PRINT(RED, BLACK, "[VALIDATION] ✗ NUL byte found at position %d\n", i);
            errors++;
            break;
        }
    }
    
    PRINT(CYAN, BLACK, "\n[VALIDATION] Summary: %d error(s) found\n\n", errors);
    return errors;
}

static void extract_filename_from_url(const char *url, char *filename, int max_len) {
    const char *ptr = url;
    
    if (STRNCMP(ptr, "http://", 7) == 0) {
        ptr += 7;
    } else if (STRNCMP(ptr, "https://", 8) == 0) {
        ptr += 8;
    }
    
    const char *last_slash = NULL;
    while (*ptr) {
        if (*ptr == '/') {
            last_slash = ptr;
        }
        ptr++;
    }
    
    if (last_slash && *(last_slash + 1) != '\0') {
        int i = 0;
        last_slash++;
        
        while (*last_slash && *last_slash != '?' && *last_slash != '#' && i < max_len - 1) {
            filename[i++] = *last_slash++;
        }
        filename[i] = '\0';
        
        if (i > 0) {
            return;
        }
    }
    
    const char *default_name = "index.html";
    int i = 0;
    while (default_name[i] && i < max_len - 1) {
        filename[i] = default_name[i];
        i++;
    }
    filename[i] = '\0';
}

static int parse_url(const char *url, char *host, char *path, uint16_t *port) {
    const char *ptr = url;
    
    if (STRNCMP(ptr, "http://", 7) == 0) {
        ptr += 7;
    } else if (STRNCMP(ptr, "https://", 8) == 0) {
        PRINT(YELLOW, BLACK, "[WGET] Warning: HTTPS not supported\n");
        ptr += 8;
    }
    
    // Extract host
    int i = 0;
    while (*ptr && *ptr != '/' && *ptr != ':' && i < 255) {
        host[i++] = *ptr++;
    }
    host[i] = '\0';
    
    if (i == 0) {
        PRINT(RED, BLACK, "[WGET] Error: No hostname\n");
        return -1;
    }
    
    // Port
    *port = 80;
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
    
    // Path - CRITICAL: ALWAYS set to "/" if empty
    if (*ptr == '\0') {
        // No path at all - use "/"
        path[0] = '/';
        path[1] = '\0';
        PRINT(WHITE, BLACK, "[PARSE] No path in URL, using '/'\n");
    } else if (*ptr == '/' && *(ptr + 1) == '\0') {
        // Just "/" - use it
        path[0] = '/';
        path[1] = '\0';
        PRINT(WHITE, BLACK, "[PARSE] URL ends with '/', using '/'\n");
    } else if (*ptr == '/') {
        // Path exists - copy it
        i = 0;
        while (*ptr && i < 255) {
            path[i++] = *ptr++;
        }
        path[i] = '\0';
        PRINT(WHITE, BLACK, "[PARSE] Extracted path: '%s'\n", path);
    } else {
        // Shouldn't happen, but be safe
        path[0] = '/';
        path[1] = '\0';
        PRINT(YELLOW, BLACK, "[PARSE] Unexpected format, defaulting to '/'\n");
    }
    
    return 0;
}

int http_get(const char *url, char *output, int max_len) {
    char host[256];
    char path[256];
    uint16_t port;
    
    PRINT(CYAN, BLACK, "\n[WGET] Starting request for: %s\n", url);
    
    if (parse_url(url, host, path, &port) != 0) {
        return -1;
    }
    
    PRINT(WHITE, BLACK, "[WGET] Parsed - Host: '%s', Path: '%s', Port: %d\n", host, path, port);
    
    // Resolve
    uint32_t ip;
    if (dns_resolve(host, &ip) != 0) {
        PRINT(RED, BLACK, "[WGET] DNS failed\n");
        return -1;
    }
    
    PRINT(GREEN, BLACK, "[WGET] Resolved to ");
    net_print_ip(ip);
    PRINT(WHITE, BLACK, "\n");
    
    // Create socket
    http_socket = tcp_socket();
    if (!http_socket) {
        PRINT(RED, BLACK, "[WGET] Socket creation failed\n");
        return -1;
    }
    
    http_response_len = 0;
    http_transfer_complete = 0;
    
    // Connect
    if (tcp_connect(http_socket, ip, port) != 0) {
        PRINT(RED, BLACK, "[WGET] Connection failed\n");
        http_socket = NULL;
        return -1;
    }
    
    PRINT(GREEN, BLACK, "[WGET] Connected!\n\n");
    
    // Build request - ULTRA CAREFUL VERSION
    char request[1024];
    int pos = 0;
    
    // Method
    request[pos++] = 'G';
    request[pos++] = 'E';
    request[pos++] = 'T';
    request[pos++] = ' ';
    
    // Path (already validated to not be empty)
    for (int i = 0; path[i] != '\0' && pos < 1000; i++) {
        request[pos++] = path[i];
    }
    
    // Space
    request[pos++] = ' ';
    
    // HTTP version
    request[pos++] = 'H';
    request[pos++] = 'T';
    request[pos++] = 'T';
    request[pos++] = 'P';
    request[pos++] = '/';
    request[pos++] = '1';
    request[pos++] = '.';
    request[pos++] = '1';
    
    // CRLF
    request[pos++] = '\r';
    request[pos++] = '\n';
    
    // Host header
    request[pos++] = 'H';
    request[pos++] = 'o';
    request[pos++] = 's';
    request[pos++] = 't';
    request[pos++] = ':';
    request[pos++] = ' ';
    
    for (int i = 0; host[i] != '\0' && pos < 1000; i++) {
        request[pos++] = host[i];
    }
    
    request[pos++] = '\r';
    request[pos++] = '\n';
    
    // User-Agent header
    const char *ua = "User-Agent: CustomOS/1.0\r\n";
    for (int i = 0; ua[i] != '\0' && pos < 1000; i++) {
        request[pos++] = ua[i];
    }
    
    // Accept header
    const char *accept = "Accept: */*\r\n";
    for (int i = 0; accept[i] != '\0' && pos < 1000; i++) {
        request[pos++] = accept[i];
    }
    
    // Connection header
    const char *conn = "Connection: close\r\n";
    for (int i = 0; conn[i] != '\0' && pos < 1000; i++) {
        request[pos++] = conn[i];
    }
    
    // Final CRLF (end of headers)
    request[pos++] = '\r';
    request[pos++] = '\n';
    
    // DON'T add null terminator - HTTP is binary safe
    
    PRINT(CYAN, BLACK, "========================================\n");
    PRINT(CYAN, BLACK, "  HTTP REQUEST DEBUG\n");
    PRINT(CYAN, BLACK, "========================================\n");
    PRINT(WHITE, BLACK, "Request length: %d bytes\n\n", pos);
    
    // Show human-readable version
    PRINT(YELLOW, BLACK, "Human-readable (with markers):\n");
    PRINT(WHITE, BLACK, "---\n");
    for (int i = 0; i < pos; i++) {
        if (request[i] == '\r') {
            PRINT(MAGENTA, BLACK, "[CR]");
        } else if (request[i] == '\n') {
            PRINT(MAGENTA, BLACK, "[LF]\n");
        } else {
            PRINT(WHITE, BLACK, "%c", request[i]);
        }
    }
    PRINT(WHITE, BLACK, "---\n\n");
    
    // Show hex dump
    hex_dump("HTTP Request", (uint8_t*)request, pos);
    
    // Validate
    validate_http_request(request, pos);
    
    PRINT(CYAN, BLACK, "========================================\n\n");
    
    // Send
    if (tcp_send(http_socket, request, pos) != 0) {
        PRINT(RED, BLACK, "[WGET] Send failed\n");
        tcp_close(http_socket);
        http_socket = NULL;
        return -1;
    }
    
    PRINT(GREEN, BLACK, "[WGET] Request sent!\n");
    PRINT(CYAN, BLACK, "[WGET] Waiting for response");
    
    // Wait for response
    int timeout = 10000;
    int no_data_count = 0;
    int last_len = 0;
    
    for (int i = 0; i < timeout; i++) {
        for (int p = 0; p < 50; p++) {
            e1000_interrupt_handler();
        }
        
        if (http_response_len > last_len) {
            last_len = http_response_len;
            no_data_count = 0;
            
            if ((http_response_len / 100) > (last_len / 100)) {
                PRINT(WHITE, BLACK, ".");
            }
        } else {
            no_data_count++;
            
            if (no_data_count > 1000 && http_response_len > 0) {
                http_transfer_complete = 1;
                break;
            }
        }
        
        if (http_transfer_complete) {
            break;
        }
        
        for (volatile int j = 0; j < 100; j++);
    }
    
    PRINT(WHITE, BLACK, "\n");
    
    tcp_close(http_socket);
    http_socket = NULL;
    
    if (http_response_len == 0) {
        PRINT(RED, BLACK, "[WGET] No response received\n");
        return -1;
    }
    
    int copy_len = (http_response_len < max_len) ? http_response_len : max_len;
    for (int i = 0; i < copy_len; i++) {
        output[i] = http_response_buffer[i];
    }
    
    if (copy_len < max_len) {
        output[copy_len] = '\0';
    }
    
    return copy_len;
}

void http_tcp_data_received(uint8_t *data, int len) {
    if (http_response_len + len < HTTP_RESPONSE_MAX) {
        for (int i = 0; i < len; i++) {
            http_response_buffer[http_response_len++] = data[i];
        }
    } else {
        PRINT(YELLOW, BLACK, "[WGET] Buffer full\n");
        int remaining = HTTP_RESPONSE_MAX - http_response_len;
        for (int i = 0; i < remaining; i++) {
            http_response_buffer[http_response_len++] = data[i];
        }
    }
}


void cmd_wget(const char *url) {
    if (!url || url[0] == '\0') {
        PRINT(CYAN, BLACK, "\nUsage: wget <url> [-o filename]\n");
        return;
    }
    
    net_config_t *config = net_get_config();
    if (!config->configured) {
        PRINT(RED, BLACK, "\nNetwork not configured!\n");
        return;
    }
    
    // Parse: get URL and optional filename
    char url_only[512];
    char filename[256];
    int has_filename = 0;
    
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
        while (url[i] && url[i] != ' ' && j < 254) {
            filename[j++] = url[i++];
        }
        filename[j] = '\0';
        has_filename = (j > 0);
    }
    
    // Default filename if not specified
    if (!has_filename) {
        extract_filename_from_url(url_only, filename, 255);
    }
    
    // Simple path: /filename (always in root)
    char path[256];
    path[0] = '/';
    int idx = 1;
    
    // Skip leading / if filename has it
    int start = (filename[0] == '/') ? 1 : 0;
    while (filename[start] && idx < 255) {
        path[idx++] = filename[start++];
    }
    path[idx] = '\0';
    
    PRINT(CYAN, BLACK, "\nWGET: %s -> %s\n\n", url_only, path);
    
    // Download
    char response[8192];
    int len = http_get(url_only, response, sizeof(response));
    
    if (len <= 0) {
        PRINT(RED, BLACK, "Download failed\n");
        return;
    }
    
    PRINT(GREEN, BLACK, "Downloaded %d bytes\n", len);
    
    // Skip HTTP headers
    int body = 0;
    for (int i = 0; i < len - 3; i++) {
        if (response[i] == '\r' && response[i+1] == '\n' && 
            response[i+2] == '\r' && response[i+3] == '\n') {
            body = i + 4;
            break;
        }
    }
    
    uint8_t *data = (body > 0) ? (uint8_t*)(response + body) : (uint8_t*)response;
    int size = (body > 0) ? (len - body) : len;
    
    PRINT(WHITE, BLACK, "Content: %d bytes\n", size);
    
    // Write file
    PRINT(WHITE, BLACK, "Creating %s... ", path);
    
    if (vfs_create(path, FILE_READ | FILE_WRITE) != 0) {
        PRINT(RED, BLACK, "FAILED (create)\n");
        goto preview;
    }
    
    int fd = vfs_open(path, FILE_WRITE);
    if (fd < 0) {
        PRINT(RED, BLACK, "FAILED (open, fd=%d)\n", fd);
        goto preview;
    }
    
    int written = vfs_write(fd, data, size);
    vfs_close(fd);
    
    if (written > 0) {
        PRINT(GREEN, BLACK, "OK (%d bytes)\n", written);
        PRINT(GREEN, BLACK, "\nSaved to %s\n", path);
        PRINT(WHITE, BLACK, "Use: cat %s\n\n", path);
    } else {
        PRINT(RED, BLACK, "FAILED (write=%d)\n", written);
    }
    
preview:
    // Show what we got
    PRINT(CYAN, BLACK, "\n--- PREVIEW ---\n");
    int show = (size < 400) ? size : 400;
    for (int i = 0; i < show; i++) {
        PRINT(WHITE, BLACK, "%c", data[i]);
    }
    if (size > 400) {
        PRINT(YELLOW, BLACK, "\n... (%d more)\n", size - 400);
    }
    PRINT(CYAN, BLACK, "\n---------------\n\n");
}


void cmd_httptest(void) {
    cmd_wget("http://example.com");
}