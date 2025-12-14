// ========== dns.c ==========
#include "dns.h"
#include "udp.h"
#include "net.h"
#include "print.h"
#include "string_helpers.h"
#include "memory.h"

static uint32_t dns_server = 0x08080808;  // 8.8.8.8 (Google DNS)
static uint32_t dns_resolved_ip = 0;
static int dns_waiting = 0;
static uint16_t dns_query_id = 0x1234;

static void dns_handler(uint32_t src_ip, uint16_t src_port,
                        uint8_t *data, uint16_t length) {
    if (length < sizeof(dns_header_t)) return;
    
    dns_header_t *header = (dns_header_t*)data;
    uint16_t id = net_htons(header->id);
    uint16_t flags = net_htons(header->flags);
    uint16_t answers = net_htons(header->answers);
    
    // Check if this is a response to our query
    if (id != dns_query_id || !(flags & DNS_FLAG_QR)) {
        return;
    }
    
    // Check for errors
    if ((flags & DNS_FLAG_RCODE) != 0) {
        PRINT(YELLOW, BLACK, "[DNS] Query failed (error code: %d)\n", 
              flags & DNS_FLAG_RCODE);
        dns_waiting = 0;
        return;
    }
    
    if (answers == 0) {
        PRINT(YELLOW, BLACK, "[DNS] No answers received\n");
        dns_waiting = 0;
        return;
    }
    
    // Skip question section
    uint8_t *ptr = data + sizeof(dns_header_t);
    uint8_t *end = data + length;
    
    // Skip question name
    while (ptr < end && *ptr != 0) {
        if ((*ptr & 0xC0) == 0xC0) {  // Compressed name
            ptr += 2;
            break;
        }
        ptr += *ptr + 1;
    }
    if (ptr < end && *ptr == 0) ptr++;  // Skip null terminator
    ptr += 4;  // Skip QTYPE and QCLASS
    
    // Parse answers
    for (int i = 0; i < answers && ptr < end; i++) {
        // Skip answer name
        while (ptr < end && *ptr != 0) {
            if ((*ptr & 0xC0) == 0xC0) {  // Compressed name
                ptr += 2;
                break;
            }
            ptr += *ptr + 1;
        }
        if (ptr < end && *ptr == 0) ptr++;
        
        if (ptr + 10 > end) break;
        
        uint16_t type = (ptr[0] << 8) | ptr[1];
        uint16_t data_len = (ptr[8] << 8) | ptr[9];
        ptr += 10;
        
        if (ptr + data_len > end) break;
        
        if (type == DNS_TYPE_A && data_len == 4) {
            // Found IPv4 address
            dns_resolved_ip = *(uint32_t*)ptr;
            dns_waiting = 0;
            
            PRINT(GREEN, BLACK, "[DNS] Resolved to ");
            net_print_ip(dns_resolved_ip);
            PRINT(WHITE, BLACK, "\n");
            return;
        }
        
        ptr += data_len;
    }
    
    dns_waiting = 0;
}

void dns_init(void) {
    udp_register_handler(DNS_PORT, dns_handler);
}

void dns_set_server(uint32_t server_ip) {
    dns_server = server_ip;
    PRINT(WHITE, BLACK, "[DNS] Server set to ");
    net_print_ip(server_ip);
    PRINT(WHITE, BLACK, "\n");
}

static int dns_encode_name(const char *hostname, uint8_t *buffer) {
    uint8_t *start = buffer;
    const char *ptr = hostname;
    
    while (*ptr) {
        uint8_t *len_ptr = buffer++;
        uint8_t len = 0;
        
        while (*ptr && *ptr != '.') {
            *buffer++ = *ptr++;
            len++;
        }
        
        *len_ptr = len;
        
        if (*ptr == '.') ptr++;
    }
    
    *buffer++ = 0;  // Null terminator
    return buffer - start;
}

int dns_resolve(const char *hostname, uint32_t *ip_out) {
    if (!hostname || !ip_out) return -1;
    
    PRINT(CYAN, BLACK, "[DNS] Resolving '%s'...\n", hostname);
    
    // Build DNS query
    uint16_t buffer_size = sizeof(dns_header_t) + DNS_MAX_NAME + 16;
    uint8_t *buffer = (uint8_t*)kmalloc(buffer_size);
    
    // Build header
    dns_header_t *header = (dns_header_t*)buffer;
    dns_query_id = (dns_query_id + 1) & 0xFFFF;
    header->id = net_htons(dns_query_id);
    header->flags = net_htons(DNS_FLAG_RD);  // Recursion desired
    header->questions = net_htons(1);
    header->answers = 0;
    header->authority = 0;
    header->additional = 0;
    
    // Encode hostname
    uint8_t *ptr = buffer + sizeof(dns_header_t);
    int name_len = dns_encode_name(hostname, ptr);
    ptr += name_len;
    
    // Add QTYPE (A record) and QCLASS (IN)
    *ptr++ = 0;
    *ptr++ = DNS_TYPE_A;
    *ptr++ = 0;
    *ptr++ = DNS_CLASS_IN;
    
    uint16_t total_len = ptr - buffer;
    
    // Reset state
    dns_resolved_ip = 0;
    dns_waiting = 1;
    
    // Send DNS query
    PRINT(WHITE, BLACK, "[DNS] Sending query to ");
    net_print_ip(dns_server);
    PRINT(WHITE, BLACK, "\n");
    
    int result = udp_send(dns_server, DNS_PORT, DNS_PORT, buffer, total_len);
    kfree(buffer);
    
    if (result != 0) {
        PRINT(YELLOW, BLACK, "[DNS] Failed to send query\n");
        return -1;
    }
    
    // Wait for response
    int timeout = DNS_TIMEOUT_MS / 10;
    for (int i = 0; i < timeout && dns_waiting; i++) {
        for (volatile int j = 0; j < 100000; j++);
    }
    
    if (dns_waiting) {
        PRINT(YELLOW, BLACK, "[DNS] Timeout waiting for response\n");
        dns_waiting = 0;
        return -1;
    }
    
    if (dns_resolved_ip == 0) {
        PRINT(YELLOW, BLACK, "[DNS] Resolution failed\n");
        return -1;
    }
    
    *ip_out = dns_resolved_ip;
    return 0;
}