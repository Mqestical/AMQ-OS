// ========== icmp.c - PRODUCTION QUALITY ==========
#include "icmp.h"
#include "net.h"
#include "print.h"
#include "string_helpers.h"
#include "memory.h"
#include "E1000.h"

#define ICMP_PING_DATA_SIZE 56
#define MAX_REPLIES 16

static uint16_t ping_id = 0;
static uint16_t ping_seq = 0;
static icmp_reply_t replies[MAX_REPLIES];
static int reply_count = 0;
static volatile int waiting_for_reply = 0;
static volatile uint16_t expected_id = 0;
static volatile uint16_t expected_seq = 0;
static volatile int reply_received = 0;

void icmp_init(void) {
    ping_id = 0x1234;
    ping_seq = 0;
    reply_count = 0;
    waiting_for_reply = 0;
    reply_received = 0;
}

// Helper to check if reply exists in array
static int check_reply_in_array(uint16_t id, uint16_t seq) {
    for (int i = 0; i < reply_count; i++) {
        if (replies[i].id == id && replies[i].sequence == seq) {
            return 1;
        }
    }
    return 0;
}

void icmp_receive(uint32_t src_ip, uint8_t *data, uint16_t length) {
    if (length < sizeof(icmp_header_t)) {
        return;
    }
    
    icmp_header_t *icmp = (icmp_header_t*)data;
    
    if (icmp->type == ICMP_TYPE_ECHO_REQUEST) {
        // Send echo reply
        icmp_header_t reply;
        reply.type = ICMP_TYPE_ECHO_REPLY;
        reply.code = 0;
        reply.checksum = 0;
        reply.id = icmp->id;
        reply.sequence = icmp->sequence;
        
        // Copy data payload
        uint16_t data_len = length - sizeof(icmp_header_t);
        uint8_t *full_reply = (uint8_t*)kmalloc(length);
        
        uint8_t *dest = full_reply;
        uint8_t *src = (uint8_t*)&reply;
        for (int i = 0; i < sizeof(icmp_header_t); i++) {
            dest[i] = src[i];
        }
        
        src = data + sizeof(icmp_header_t);
        dest = full_reply + sizeof(icmp_header_t);
        for (int i = 0; i < data_len; i++) {
            dest[i] = src[i];
        }
        
        // Calculate checksum
        icmp_header_t *hdr = (icmp_header_t*)full_reply;
        hdr->checksum = net_checksum(full_reply, length);
        
        net_send_ipv4(src_ip, IP_PROTO_ICMP, full_reply, length);
        
        kfree(full_reply);
    }
    else if (icmp->type == ICMP_TYPE_ECHO_REPLY) {
        uint16_t recv_id = net_htons(icmp->id);
        uint16_t recv_seq = net_htons(icmp->sequence);
        
        // Check if already stored (avoid duplicates)
        if (check_reply_in_array(recv_id, recv_seq)) {
            return;
        }
        
        // Store reply
        if (reply_count < MAX_REPLIES) {
            replies[reply_count].src_ip = src_ip;
            replies[reply_count].id = recv_id;
            replies[reply_count].sequence = recv_seq;
            replies[reply_count].timestamp = 0;
            reply_count++;
        }
        
        // Check if this is the reply we're waiting for
        if (waiting_for_reply && recv_id == expected_id && recv_seq == expected_seq) {
            reply_received = 1;
        }
    }
}

int icmp_send_ping(uint32_t dest_ip, uint16_t id, uint16_t seq) {
    uint16_t total_len = sizeof(icmp_header_t) + ICMP_PING_DATA_SIZE;
    uint8_t *buffer = (uint8_t*)kmalloc(total_len);
    
    icmp_header_t *icmp = (icmp_header_t*)buffer;
    icmp->type = ICMP_TYPE_ECHO_REQUEST;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->id = net_htons(id);
    icmp->sequence = net_htons(seq);
    
    // Fill data with pattern
    uint8_t *data = buffer + sizeof(icmp_header_t);
    for (int i = 0; i < ICMP_PING_DATA_SIZE; i++) {
        data[i] = 0x41 + (i % 26);  // A-Z pattern
    }
    
    // Calculate checksum
    icmp->checksum = net_checksum(buffer, total_len);
    
    int result = net_send_ipv4(dest_ip, IP_PROTO_ICMP, buffer, total_len);
    
    kfree(buffer);
    return result;
}

int icmp_wait_reply(uint16_t id, uint16_t seq, int timeout_ms) {
    // Check if reply already arrived before we started waiting
    if (check_reply_in_array(id, seq)) {
        return 1;
    }
    
    expected_id = id;
    expected_seq = seq;
    waiting_for_reply = 1;
    reply_received = 0;
    
    // Poll with reasonable frequency - 1000 iterations = ~1 second on most hardware
    int iterations = timeout_ms * 10;  // 10 iterations per ms
    
    for (int i = 0; i < iterations; i++) {
        // Poll network stack
        for (int j = 0; j < 100; j++) {
            e1000_interrupt_handler();
            
            // Check both flag AND array
            if (reply_received || check_reply_in_array(id, seq)) {
                waiting_for_reply = 0;
                return 1;
            }
        }
        
        // Small delay between polls
        for (volatile int k = 0; k < 100; k++);
    }
    
    waiting_for_reply = 0;
    
    // Final check
    if (check_reply_in_array(id, seq)) {
        return 1;
    }
    
    return 0;
}

icmp_reply_t* icmp_get_last_reply(void) {
    if (reply_count > 0) {
        return &replies[reply_count - 1];
    }
    return NULL;
}

void cmd_ping(const char *target) {
    PRINT(CYAN, BLACK, "\n=== PING %s ===\n", target);
    
    // Parse or resolve target
    uint32_t dest_ip = net_parse_ip(target);
    
    if (dest_ip == 0) {
        PRINT(RED, BLACK, "Invalid IP address\n");
        return;
    }
    
    PRINT(WHITE, BLACK, "PING ");
    net_print_ip(dest_ip);
    PRINT(WHITE, BLACK, " with %d bytes of data\n\n", ICMP_PING_DATA_SIZE);
    
    // Clear old replies
    reply_count = 0;
    
    uint16_t id = ping_id++;
    int sent = 0;
    int received = 0;
    
    // Send 4 pings with 1 second intervals (standard behavior)
    for (int i = 0; i < 4; i++) {
        uint16_t seq = ping_seq++;
        
        // Send ping
        if (icmp_send_ping(dest_ip, id, seq) == 0) {
            sent++;
            
            PRINT(WHITE, BLACK, "Sent ping #%d (seq=%d)... ", i + 1, seq);
            
            // Wait up to 1 second for reply
            if (icmp_wait_reply(id, seq, 1000)) {
                // Got reply
                PRINT(GREEN, BLACK, "Reply received!\n");
                received++;
            } else {
                // Timeout
                PRINT(YELLOW, BLACK, "Timeout\n");
            }
        } else {
            PRINT(RED, BLACK, "Failed to send ping #%d\n", i + 1);
        }
        
        // Wait 1 second before next ping (except after last one)
        // This matches standard ping behavior
        if (i < 3) {
            PRINT(WHITE, BLACK, "Waiting 1 second...\n");
            
            // Delay ~1 second (1000ms worth of iterations)
            for (int delay = 0; delay < 1000; delay++) {
                // Still poll network during delay to process any traffic
                for (int p = 0; p < 10; p++) {
                    e1000_interrupt_handler();
                }
                
                // Small delay
                for (volatile int d = 0; d < 10000; d++);
            }
            
            PRINT(WHITE, BLACK, "\n");
        }
    }
    
    // Print statistics (like real ping)
    PRINT(CYAN, BLACK, "\n=== STATISTICS ===\n");
    PRINT(WHITE, BLACK, "Sent: %d, Received: %d, Loss: %d%%\n", 
          sent, received, sent > 0 ? ((sent - received) * 100 / sent) : 0);
    
    if (received > 0) {
        PRINT(GREEN, BLACK, "Success!\n");
    } else {
        PRINT(YELLOW, BLACK, "All packets lost\n");
    }
}

// alt to use later
void cmd_ping_fast(const char *target) {
    PRINT(CYAN, BLACK, "\n=== FAST PING %s ===\n", target);
    
    uint32_t dest_ip = net_parse_ip(target);
    
    if (dest_ip == 0) {
        PRINT(RED, BLACK, "Invalid IP address\n");
        return;
    }
    
    reply_count = 0;
    uint16_t id = ping_id++;
    int sent = 0;
    int received = 0;
    
    for (int i = 0; i < 4; i++) {
        uint16_t seq = ping_seq++;
        
        if (icmp_send_ping(dest_ip, id, seq) == 0) {
            sent++;
            
            if (icmp_wait_reply(id, seq, 2000)) {
                PRINT(GREEN, BLACK, "✓ Ping #%d OK\n", i + 1);
                received++;
            } else {
                PRINT(YELLOW, BLACK, "✗ Ping #%d Timeout\n", i + 1);
            }
        }
        
        // Short delay between pings
        for (int d = 0; d < 100; d++) {
            for (int p = 0; p < 10; p++) {
                e1000_interrupt_handler();
            }
            for (volatile int j = 0; j < 1000; j++);
        }
    }
    
    PRINT(WHITE, BLACK, "\nSent: %d, Received: %d, Loss: %d%%\n", 
          sent, received, sent > 0 ? ((sent - received) * 100 / sent) : 0);
}