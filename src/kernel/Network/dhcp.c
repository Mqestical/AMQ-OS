// ========== dhcp.c ==========
#include "dhcp.h"
#include "udp.h"
#include "net.h"
#include "print.h"
#include "string_helpers.h"
#include "memory.h"

#define DHCP_MAGIC 0x63825363

static uint32_t dhcp_xid = 0x12345678;
static uint32_t dhcp_server_ip = 0;
static uint32_t dhcp_offered_ip = 0;
static uint32_t dhcp_netmask = 0;
static uint32_t dhcp_gateway = 0;
static int dhcp_state = 0;

static void dhcp_handler(uint32_t src_ip, uint16_t src_port,
                        uint8_t *data, uint16_t length) {
    if (length < sizeof(dhcp_packet_t)) return;
    
    dhcp_packet_t *dhcp = (dhcp_packet_t*)data;
    
    if (net_htonl(dhcp->xid) != dhcp_xid) return;
    
    // Parse DHCP message type
    uint8_t msg_type = 0;
    uint8_t *opt = dhcp->options;
    
    while (opt < data + length && *opt != 0xFF) {
        if (*opt == 53) {  // DHCP Message Type
            msg_type = opt[2];
            break;
        }
        if (*opt == 0) {
            opt++;
        } else {
            opt += 2 + opt[1];
        }
    }
    
    if (msg_type == DHCP_OFFER) {
        PRINT(GREEN, BLACK, "[DHCP] Received OFFER\n");
        dhcp_offered_ip = dhcp->yiaddr;
        dhcp_server_ip = src_ip;
        
        // Parse options
        opt = dhcp->options;
        while (opt < data + length && *opt != 0xFF) {
            if (*opt == 1) {  // Subnet mask
                dhcp_netmask = *(uint32_t*)&opt[2];
            }
            else if (*opt == 3) {  // Router/Gateway
                dhcp_gateway = *(uint32_t*)&opt[2];
            }
            
            if (*opt == 0) {
                opt++;
            } else {
                opt += 2 + opt[1];
            }
        }
        
        PRINT(WHITE, BLACK, "  Offered IP: ");
        net_print_ip(dhcp_offered_ip);
        PRINT(WHITE, BLACK, "\n  Netmask: ");
        net_print_ip(dhcp_netmask);
        PRINT(WHITE, BLACK, "\n  Gateway: ");
        net_print_ip(dhcp_gateway);
        PRINT(WHITE, BLACK, "\n");
        
        dhcp_state = 1;
    }
    else if (msg_type == DHCP_ACK) {
        PRINT(GREEN, BLACK, "[DHCP] Received ACK - Configuration complete!\n");
        
        net_set_config(dhcp_offered_ip, dhcp_netmask, dhcp_gateway);
        dhcp_state = 2;
    }
}

void dhcp_init(void) {
    udp_register_handler(68, dhcp_handler);
}

int dhcp_discover(void) {
    PRINT(CYAN, BLACK, "[DHCP] Sending DISCOVER...\n");
    
    dhcp_packet_t packet = {0};
    packet.op = 1;  // BOOTREQUEST
    packet.htype = 1;  // Ethernet
    packet.hlen = 6;
    packet.xid = net_htonl(dhcp_xid);
    packet.magic = net_htonl(DHCP_MAGIC);
    
    net_config_t *config = net_get_config();
    for (int i = 0; i < 6; i++) {
        packet.chaddr[i] = config->mac[i];
    }
    
    // Options
    uint8_t *opt = packet.options;
    
    // DHCP Message Type = Discover
    *opt++ = 53; *opt++ = 1; *opt++ = DHCP_DISCOVER;
    
    // Parameter Request List
    *opt++ = 55; *opt++ = 3;
    *opt++ = 1;  // Subnet mask
    *opt++ = 3;  // Router
    *opt++ = 6;  // DNS
    
    // End
    *opt++ = 255;
    
    dhcp_state = 0;
    
    return udp_send(0xFFFFFFFF, 68, 67, &packet, sizeof(packet));
}

int dhcp_request(void) {
    if (dhcp_state != 1) return -1;
    
    PRINT(CYAN, BLACK, "[DHCP] Sending REQUEST...\n");
    
    dhcp_packet_t packet = {0};
    packet.op = 1;
    packet.htype = 1;
    packet.hlen = 6;
    packet.xid = net_htonl(dhcp_xid);
    packet.magic = net_htonl(DHCP_MAGIC);
    
    net_config_t *config = net_get_config();
    for (int i = 0; i < 6; i++) {
        packet.chaddr[i] = config->mac[i];
    }
    
    uint8_t *opt = packet.options;
    
    // DHCP Message Type = Request
    *opt++ = 53; *opt++ = 1; *opt++ = DHCP_REQUEST;
    
    // Requested IP
    *opt++ = 50; *opt++ = 4;
    *(uint32_t*)opt = dhcp_offered_ip;
    opt += 4;
    
    // Server Identifier
    *opt++ = 54; *opt++ = 4;
    *(uint32_t*)opt = dhcp_server_ip;
    opt += 4;
    
    // End
    *opt++ = 255;
    
    return udp_send(0xFFFFFFFF, 68, 67, &packet, sizeof(packet));
}

int dhcp_wait_complete(int timeout_ms) {
    for (int i = 0; i < timeout_ms / 10; i++) {
        if (dhcp_state == 2) return 0;
        for (volatile int j = 0; j < 100000; j++);
    }
    return -1;
}
