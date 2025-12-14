// ========== icmp.c (VirtualBox Compatible) ==========
#include "icmp.h"
#include "net.h"
#include "print.h"
#include "string_helpers.h"
#include "memory.h"

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

void icmp_receive(uint32_t src_ip, uint8_t *data, uint16_t length) {
    if (length < sizeof(icmp_header_t)) return;
    
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
        
        PRINT(WHITE, BLACK, "[ICMP] Replied to ping from ");
        net_print_ip(src_ip);
        PRINT(WHITE, BLACK, "\n");
    }
    else if (icmp->type == ICMP_TYPE_ECHO_REPLY) {
        uint16_t recv_id = net_htons(icmp->id);
        uint16_t recv_seq = net_htons(icmp->sequence);
        
        PRINT(GREEN, BLACK, "[ICMP] Reply from ");
        net_print_ip(src_ip);
        PRINT(WHITE, BLACK, ": id=0x%04x seq=%d\n", recv_id, recv_seq);
        
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
    expected_id = id;
    expected_seq = seq;
    waiting_for_reply = 1;
    reply_received = 0;
    
    int iterations = timeout_ms / 10;
    for (int i = 0; i < iterations; i++) {
        if (reply_received) {
            waiting_for_reply = 0;
            return 1;
        }
        for (volatile int j = 0; j < 100000; j++);
    }
    
    waiting_for_reply = 0;
    return 0;
}

icmp_reply_t* icmp_get_last_reply(void) {
    if (reply_count > 0) {
        return &replies[reply_count - 1];
    }
    return NULL;
}