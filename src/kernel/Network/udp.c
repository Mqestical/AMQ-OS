// ========== udp.c ==========
#include "udp.h"
#include "net.h"
#include "memory.h"
#include "print.h"
#include "string_helpers.h"

#define MAX_UDP_HANDLERS 8

typedef struct {
    uint16_t port;
    udp_handler_t handler;
} udp_handler_entry_t;

static udp_handler_entry_t udp_handlers[MAX_UDP_HANDLERS];

void udp_init(void) {
    for (int i = 0; i < MAX_UDP_HANDLERS; i++) {
        udp_handlers[i].port = 0;
        udp_handlers[i].handler = NULL;
    }
}

void udp_register_handler(uint16_t port, udp_handler_t handler) {
    for (int i = 0; i < MAX_UDP_HANDLERS; i++) {
        if (udp_handlers[i].port == 0) {
            udp_handlers[i].port = port;
            udp_handlers[i].handler = handler;
            return;
        }
    }
}

void udp_receive(uint32_t src_ip, uint8_t *data, uint16_t length) {
    if (length < sizeof(udp_header_t)) {
        PRINT(RED, BLACK, "[UDP] Packet too short: %d\n", length);
        return;
    }
    
    udp_header_t *udp = (udp_header_t*)data;
    uint16_t dest_port = net_htons(udp->dest_port);
    uint16_t src_port = net_htons(udp->src_port);
    uint16_t udp_len = net_htons(udp->length);
    
    PRINT(CYAN, BLACK, "[UDP] ");
    net_print_ip(src_ip);
    PRINT(WHITE, BLACK, ":%d -> :%d (len=%d)\n", src_port, dest_port, udp_len);
    
    uint8_t *payload = data + sizeof(udp_header_t);
    uint16_t payload_len = udp_len - sizeof(udp_header_t);
    
    // Sanity check
    if (payload_len > length - sizeof(udp_header_t)) {
        PRINT(RED, BLACK, "[UDP] Invalid length field\n");
        return;
    }
    
    // Find handler
    for (int i = 0; i < MAX_UDP_HANDLERS; i++) {
        if (udp_handlers[i].port == dest_port && udp_handlers[i].handler) {
            PRINT(GREEN, BLACK, "[UDP] Dispatching to handler for port %d\n", dest_port);
            udp_handlers[i].handler(src_ip, src_port, payload, payload_len);
            return;
        }
    }
    
    PRINT(YELLOW, BLACK, "[UDP] No handler for port %d\n", dest_port);
}

int udp_send(uint32_t dest_ip, uint16_t src_port, uint16_t dest_port,
             const void *data, uint16_t length) {
    uint16_t total_len = sizeof(udp_header_t) + length;
    uint8_t *buffer = (uint8_t*)kmalloc(total_len);
    
    udp_header_t *udp = (udp_header_t*)buffer;
    udp->src_port = net_htons(src_port);
    udp->dest_port = net_htons(dest_port);
    udp->length = net_htons(total_len);
    udp->checksum = 0;  // Optional for IPv4
    
    // Copy data
    uint8_t *dest = buffer + sizeof(udp_header_t);
    const uint8_t *src = (const uint8_t*)data;
    for (uint16_t i = 0; i < length; i++) {
        dest[i] = src[i];
    }
    
    int result = net_send_ipv4(dest_ip, IP_PROTO_UDP, buffer, total_len);
    
    kfree(buffer);
    return result;
}