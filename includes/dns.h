// ========== dns.h ==========
#ifndef DNS_H
#define DNS_H

#include <stdint.h>

#define DNS_PORT 53
#define DNS_MAX_NAME 255
#define DNS_TIMEOUT_MS 5000

// DNS Header Flags
#define DNS_FLAG_QR     (1 << 15)  // Query/Response
#define DNS_FLAG_OPCODE (0xF << 11) // Operation code
#define DNS_FLAG_AA     (1 << 10)  // Authoritative answer
#define DNS_FLAG_TC     (1 << 9)   // Truncated
#define DNS_FLAG_RD     (1 << 8)   // Recursion desired
#define DNS_FLAG_RA     (1 << 7)   // Recursion available
#define DNS_FLAG_RCODE  (0xF)      // Response code

// DNS Types
#define DNS_TYPE_A      1   // IPv4 address
#define DNS_TYPE_AAAA   28  // IPv6 address
#define DNS_TYPE_CNAME  5   // Canonical name

// DNS Classes
#define DNS_CLASS_IN    1   // Internet

typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t questions;
    uint16_t answers;
    uint16_t authority;
    uint16_t additional;
} __attribute__((packed)) dns_header_t;

// Initialize DNS client
void dns_init(void);

// Set DNS server (default: 8.8.8.8)
void dns_set_server(uint32_t server_ip);

// Resolve domain name to IP address
// Returns 0 on success, -1 on failure
int dns_resolve(const char *hostname, uint32_t *ip_out);

#endif // DNS_H