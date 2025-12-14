// ========== dhcp.c - SIMPLE RELIABLE VERSION ==========
#include "dhcp.h"
#include "udp.h"
#include "net.h"
#include "print.h"
#include "string_helpers.h"
#include "memory.h"
#include "E1000.h"

static uint32_t xid = 0x12345678;
static volatile int got_offer = 0;
static volatile int got_ack = 0;
static uint32_t offer_ip = 0;
static uint32_t server_ip = 0;
static uint32_t mask = 0;
static uint32_t gw = 0;
static uint32_t dns = 0;

extern void dns_set_server(uint32_t server_ip);

static void dhcp_rx(uint32_t src_ip, uint16_t src_port, uint8_t *data, uint16_t len) {
    if (len < 244) return;
    if (data[0] != 2) return;
    
    // Check XID
    uint32_t pkt_xid = (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];
    if (pkt_xid != xid) return;
    
    // Get offered IP
    offer_ip = data[16] | (data[17] << 8) | (data[18] << 16) | (data[19] << 24);
    
    // Check magic cookie
    if (data[236] != 0x63 || data[237] != 0x82 || data[238] != 0x53 || data[239] != 0x63) return;
    
    // Parse options
    uint8_t msg_type = 0;
    int i = 240;
    
    while (i < len - 2 && data[i] != 0xFF) {
        if (data[i] == 0) { i++; continue; }
        
        uint8_t opt = data[i];
        uint8_t olen = data[i + 1];
        
        if (i + 2 + olen > len) break;
        
        if (opt == 53 && olen == 1) {
            msg_type = data[i + 2];
        }
        else if (opt == 54 && olen == 4) {
            server_ip = data[i+2] | (data[i+3] << 8) | (data[i+4] << 16) | (data[i+5] << 24);
        }
        else if (opt == 1 && olen == 4) {
            mask = data[i+2] | (data[i+3] << 8) | (data[i+4] << 16) | (data[i+5] << 24);
        }
        else if (opt == 3 && olen >= 4) {
            gw = data[i+2] | (data[i+3] << 8) | (data[i+4] << 16) | (data[i+5] << 24);
        }
        else if (opt == 6 && olen >= 4) {
            dns = data[i+2] | (data[i+3] << 8) | (data[i+4] << 16) | (data[i+5] << 24);
        }
        
        i += 2 + olen;
    }
    
    if (msg_type == 2) got_offer = 1;
    else if (msg_type == 5) got_ack = 1;
}

void dhcp_init(void) {
    udp_register_handler(68, dhcp_rx);
}

int dhcp_get_ip(void) {
    PRINT(CYAN, BLACK, "\n=== DHCP ===\n");
    
    // Clear state
    got_offer = 0;
    got_ack = 0;
    offer_ip = 0;
    server_ip = 0;
    mask = 0;
    gw = 0;
    dns = 0;
    xid++;
    
    uint8_t *buf = (uint8_t*)kmalloc(548);
    for (int i = 0; i < 548; i++) buf[i] = 0;
    
    net_config_t *cfg = net_get_config();
    
    // Build DISCOVER
    buf[0] = 1;
    buf[1] = 1;
    buf[2] = 6;
    buf[4] = (xid >> 24) & 0xFF;
    buf[5] = (xid >> 16) & 0xFF;
    buf[6] = (xid >> 8) & 0xFF;
    buf[7] = xid & 0xFF;
    buf[10] = 0x80;
    
    for (int i = 0; i < 6; i++) buf[28 + i] = cfg->mac[i];
    
    buf[236] = 0x63;
    buf[237] = 0x82;
    buf[238] = 0x53;
    buf[239] = 0x63;
    
    int opt = 240;
    buf[opt++] = 53; buf[opt++] = 1; buf[opt++] = 1;
    buf[opt++] = 55; buf[opt++] = 4; buf[opt++] = 1; buf[opt++] = 3; buf[opt++] = 6; buf[opt++] = 15;
    buf[opt++] = 255;
    
    PRINT(WHITE, BLACK, "Sending DISCOVER...\n");
    
    // Send DISCOVER
    for (int retry = 0; retry < 3; retry++) {
        udp_send(0xFFFFFFFF, 68, 67, buf, 548);
        
        for (int i = 0; i < 2000; i++) {
            for (int p = 0; p < 50; p++) e1000_interrupt_handler();
            if (got_offer) goto send_request;
            for (volatile int j = 0; j < 5000; j++);
        }
    }
    
    PRINT(RED, BLACK, "No OFFER received\n");
    kfree(buf);
    return -1;
    
send_request:
    PRINT(GREEN, BLACK, "Got OFFER: ");
    net_print_ip(offer_ip);
    PRINT(WHITE, BLACK, "\n");
    
    // Wait
    for (int i = 0; i < 100; i++) {
        e1000_interrupt_handler();
        for (volatile int j = 0; j < 10000; j++);
    }
    
    // Build REQUEST
    for (int i = 0; i < 548; i++) buf[i] = 0;
    
    buf[0] = 1;
    buf[1] = 1;
    buf[2] = 6;
    buf[4] = (xid >> 24) & 0xFF;
    buf[5] = (xid >> 16) & 0xFF;
    buf[6] = (xid >> 8) & 0xFF;
    buf[7] = xid & 0xFF;
    buf[10] = 0x80;
    
    for (int i = 0; i < 6; i++) buf[28 + i] = cfg->mac[i];
    
    buf[236] = 0x63;
    buf[237] = 0x82;
    buf[238] = 0x53;
    buf[239] = 0x63;
    
    opt = 240;
    buf[opt++] = 53; buf[opt++] = 1; buf[opt++] = 3;
    buf[opt++] = 50; buf[opt++] = 4;
    buf[opt++] = offer_ip & 0xFF;
    buf[opt++] = (offer_ip >> 8) & 0xFF;
    buf[opt++] = (offer_ip >> 16) & 0xFF;
    buf[opt++] = (offer_ip >> 24) & 0xFF;
    buf[opt++] = 54; buf[opt++] = 4;
    buf[opt++] = server_ip & 0xFF;
    buf[opt++] = (server_ip >> 8) & 0xFF;
    buf[opt++] = (server_ip >> 16) & 0xFF;
    buf[opt++] = (server_ip >> 24) & 0xFF;
    buf[opt++] = 55; buf[opt++] = 4; buf[opt++] = 1; buf[opt++] = 3; buf[opt++] = 6; buf[opt++] = 15;
    buf[opt++] = 255;
    
    PRINT(WHITE, BLACK, "Sending REQUEST...\n");
    
    // Send REQUEST
    for (int retry = 0; retry < 3; retry++) {
        udp_send(0xFFFFFFFF, 68, 67, buf, 548);
        
        for (int i = 0; i < 2000; i++) {
            for (int p = 0; p < 50; p++) e1000_interrupt_handler();
            if (got_ack) goto done;
            for (volatile int j = 0; j < 5000; j++);
        }
    }
    
    PRINT(RED, BLACK, "No ACK received\n");
    kfree(buf);
    return -1;
    
done:
    kfree(buf);
    
    PRINT(GREEN, BLACK, "Got ACK!\n\n");
    
    // Apply config
    net_set_config(offer_ip, mask, gw);
    
    // Set DNS
    if (dns != 0) {
        dns_set_server(dns);
    } else {
        dns_set_server(gw);
    }
    
    PRINT(GREEN, BLACK, "=== SUCCESS ===\n");
    PRINT(WHITE, BLACK, "IP:      "); net_print_ip(offer_ip); PRINT(WHITE, BLACK, "\n");
    PRINT(WHITE, BLACK, "Gateway: "); net_print_ip(gw); PRINT(WHITE, BLACK, "\n");
    PRINT(WHITE, BLACK, "DNS:     "); net_print_ip(dns != 0 ? dns : gw); PRINT(WHITE, BLACK, "\n\n");
    
    return 0;
}

int dhcp_discover(void) { return 0; }
int dhcp_request(void) { return 0; }
int dhcp_wait_complete(int timeout_ms) { return 0; }