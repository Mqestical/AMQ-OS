// ========== icmp.c ==========
#include "icmp.h"
#include "net.h"
#include "print.h"
#include "string_helpers.h"
#include "memory.h"

#define ICMP_PING_DATA_SIZE 56

static uint16_t ping_id = 0;
static uint16_t ping_seq = 0;

void icmp_init(void) {
    ping_id = 0x1234;
    ping_seq = 0;
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
        PRINT(GREEN, BLACK, "[ICMP] Ping reply from ");
        net_print_ip(src_ip);
        PRINT(WHITE, BLACK, ": id=%d seq=%d\n", 
              net_htons(icmp->id), net_htons(icmp->sequence));
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