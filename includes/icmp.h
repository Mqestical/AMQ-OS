// ========== icmp.h ==========
#ifndef ICMP_H
#define ICMP_H

#include <stdint.h>
#include "net.h"  // Use icmp_header_t from net.h

#define ICMP_TYPE_ECHO_REPLY   0
#define ICMP_TYPE_ECHO_REQUEST 8

typedef struct {
    uint32_t src_ip;
    uint16_t id;
    uint16_t sequence;
    uint32_t timestamp;
} icmp_reply_t;

// Initialize ICMP
void icmp_init(void);

// Receive ICMP packet
void icmp_receive(uint32_t src_ip, uint8_t *data, uint16_t length);

// Send ping (ICMP echo request)
int icmp_send_ping(uint32_t dest_ip, uint16_t id, uint16_t seq);

// Wait for ping reply with timeout (milliseconds)
// Returns 1 if reply received, 0 if timeout
int icmp_wait_reply(uint16_t id, uint16_t seq, int timeout_ms);

// Get last reply info
icmp_reply_t* icmp_get_last_reply(void);

#endif // ICMP_H