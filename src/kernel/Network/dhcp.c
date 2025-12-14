// ========== dhcp.c (VirtualBox NAT Compatible - FIXED) ==========
#include "dhcp.h"
#include "udp.h"
#include "net.h"
#include "print.h"
#include "string_helpers.h"
#include "memory.h"
#include "E1000.h"  // Need this for polling

#define DHCP_MAGIC 0x63825363
#define DHCP_BOOTP_MIN_SIZE 300

static uint32_t dhcp_xid = 0x12345678;
static uint32_t dhcp_offered_ip = 0;
static uint32_t dhcp_server_ip = 0;
static uint32_t dhcp_netmask = 0;
static uint32_t dhcp_gateway = 0;
static uint32_t dhcp_dns = 0;
static volatile int dhcp_state = 0;

static void dhcp_handler(uint32_t src_ip, uint16_t src_port,
                        uint8_t *data, uint16_t length) {
    if (length < sizeof(dhcp_packet_t)) return;
    
    dhcp_packet_t *dhcp = (dhcp_packet_t*)data;
    
    if (dhcp->op != 2) return;
    if (net_htonl(dhcp->xid) != dhcp_xid) return;
    if (net_htonl(dhcp->magic) != DHCP_MAGIC) return;
    
    uint8_t msg_type = 0;
    uint8_t *opt = dhcp->options;
    uint8_t *end = data + length;
    
    while (opt < end - 2 && *opt != 0xFF) {
        if (*opt == 0) {
            opt++;
            continue;
        }
        
        if (opt + 1 + opt[1] >= end) break;
        
        if (*opt == 53 && opt[1] == 1) {
            msg_type = opt[2];
        }
        else if (*opt == 1 && opt[1] == 4) {
            dhcp_netmask = *(uint32_t*)&opt[2];
        }
        else if (*opt == 3 && opt[1] >= 4) {
            dhcp_gateway = *(uint32_t*)&opt[2];
        }
        else if (*opt == 6 && opt[1] >= 4) {
            dhcp_dns = *(uint32_t*)&opt[2];
        }
        else if (*opt == 54 && opt[1] == 4) {
            dhcp_server_ip = *(uint32_t*)&opt[2];
        }
        
        opt += 2 + opt[1];
    }
    
    if (msg_type == DHCP_OFFER) {
        dhcp_offered_ip = dhcp->yiaddr;
        dhcp_state = 1;
        
        PRINT(GREEN, BLACK, "[DHCP] Received OFFER: ");
        net_print_ip(dhcp_offered_ip);
        PRINT(WHITE, BLACK, " from ");
        net_print_ip(dhcp_server_ip);
        PRINT(WHITE, BLACK, "\n");
        PRINT(WHITE, BLACK, "  Netmask: ");
        net_print_ip(dhcp_netmask);
        PRINT(WHITE, BLACK, "\n  Gateway: ");
        net_print_ip(dhcp_gateway);
        PRINT(WHITE, BLACK, "\n");
    }
    else if (msg_type == DHCP_ACK) {
        dhcp_state = 2;
        PRINT(GREEN, BLACK, "[DHCP] Received ACK!\n");
        net_set_config(dhcp_offered_ip, dhcp_netmask, dhcp_gateway);
    }
    else if (msg_type == DHCP_NAK) {
        PRINT(RED, BLACK, "[DHCP] Received NAK\n");
        dhcp_state = 0;
    }
}

void dhcp_init(void) {
    udp_register_handler(68, dhcp_handler);
    PRINT(CYAN, BLACK, "[DHCP] Handler registered on port 68\n");
}

static void dhcp_build_packet(dhcp_packet_t *packet, uint8_t msg_type, 
                             uint32_t requested_ip, uint32_t server_ip) {
    net_config_t *config = net_get_config();
    
    for (int i = 0; i < sizeof(dhcp_packet_t); i++) {
        ((uint8_t*)packet)[i] = 0;
    }
    
    packet->op = 1;
    packet->htype = 1;
    packet->hlen = 6;
    packet->hops = 0;
    packet->xid = net_htonl(dhcp_xid);
    packet->secs = 0;
    packet->flags = net_htons(0x8000);  // Broadcast
    packet->ciaddr = 0;
    packet->yiaddr = 0;
    packet->siaddr = 0;
    packet->giaddr = 0;
    
    for (int i = 0; i < 6; i++) {
        packet->chaddr[i] = config->mac[i];
    }
    
    packet->magic = net_htonl(DHCP_MAGIC);
    
    uint8_t *opt = packet->options;
    
    // Message Type
    *opt++ = 53;
    *opt++ = 1;
    *opt++ = msg_type;
    
    if (msg_type == DHCP_REQUEST) {
        if (requested_ip != 0) {
            *opt++ = 50;
            *opt++ = 4;
            *(uint32_t*)opt = requested_ip;
            opt += 4;
        }
        
        if (server_ip != 0) {
            *opt++ = 54;
            *opt++ = 4;
            *(uint32_t*)opt = server_ip;
            opt += 4;
        }
    }
    
    // Parameter Request List
    *opt++ = 55;
    *opt++ = 4;
    *opt++ = 1;   // Subnet Mask
    *opt++ = 3;   // Router
    *opt++ = 6;   // DNS
    *opt++ = 15;  // Domain Name
    
    // Client Identifier
    *opt++ = 61;
    *opt++ = 7;
    *opt++ = 1;
    for (int i = 0; i < 6; i++) {
        *opt++ = config->mac[i];
    }
    
    *opt++ = 255;  // End
}

int dhcp_get_ip(void) {
    dhcp_packet_t *packet = (dhcp_packet_t*)kmalloc(sizeof(dhcp_packet_t));
    if (!packet) {
        PRINT(RED, BLACK, "[DHCP] Failed to allocate buffer\n");
        return -1;
    }
    
    dhcp_offered_ip = 0;
    dhcp_server_ip = 0;
    dhcp_netmask = 0;
    dhcp_gateway = 0;
    dhcp_dns = 0;
    dhcp_state = 0;
    
    dhcp_xid = 0x12340000 | ((uint32_t)&packet & 0xFFFF);
    
    PRINT(CYAN, BLACK, "\n=== DHCP Client Starting ===\n");
    PRINT(WHITE, BLACK, "[DHCP] XID: 0x%08x\n", dhcp_xid);
    
    // Wait a bit after link up before starting DHCP
    PRINT(WHITE, BLACK, "[DHCP] Waiting for network to settle...\n");
    for (volatile int i = 0; i < 100; i++) {
        e1000_interrupt_handler();  // Poll for packets
        for (volatile int j = 0; j < 100000; j++);
    }
    
    // ===== DISCOVER =====
    PRINT(WHITE, BLACK, "[DHCP] Sending DISCOVER...\n");
    
    dhcp_build_packet(packet, DHCP_DISCOVER, 0, 0);
    
    if (udp_send(0xFFFFFFFF, 68, 67, packet, sizeof(dhcp_packet_t)) != 0) {
        PRINT(RED, BLACK, "[DHCP] Failed to send DISCOVER\n");
        kfree(packet);
        return -1;
    }
    
    // Wait for OFFER with active polling
    PRINT(WHITE, BLACK, "[DHCP] Waiting for OFFER");
    int timeout = 1000;  // 10 seconds
    for (int i = 0; i < timeout; i++) {
        // CRITICAL: Poll E1000 for received packets!
        e1000_interrupt_handler();
        
        if (dhcp_state == 1) {
            PRINT(WHITE, BLACK, " OK\n");
            break;
        }
        if (i % 100 == 0) PRINT(WHITE, BLACK, ".");
        for (volatile int j = 0; j < 100000; j++);
    }
    
    if (dhcp_state != 1) {
        PRINT(RED, BLACK, " TIMEOUT\n");
        PRINT(YELLOW, BLACK, "[DHCP] Troubleshooting:\n");
        PRINT(YELLOW, BLACK, "  - Check VirtualBox NAT is enabled\n");
        PRINT(YELLOW, BLACK, "  - Check 'Cable Connected' is checked\n");
        PRINT(YELLOW, BLACK, "  - Link status: %s\n", 
              e1000_link_status() ? "UP" : "DOWN");
        kfree(packet);
        return -1;
    }
    
    // Delay before REQUEST
    PRINT(WHITE, BLACK, "[DHCP] Preparing REQUEST...\n");
    for (volatile int i = 0; i < 100; i++) {
        e1000_interrupt_handler();
        for (volatile int j = 0; j < 100000; j++);
    }
    
    // ===== REQUEST =====
    PRINT(WHITE, BLACK, "[DHCP] Sending REQUEST for ");
    net_print_ip(dhcp_offered_ip);
    PRINT(WHITE, BLACK, "\n");
    
    dhcp_state = 0;
    dhcp_build_packet(packet, DHCP_REQUEST, dhcp_offered_ip, dhcp_server_ip);
    
    if (udp_send(0xFFFFFFFF, 68, 67, packet, sizeof(dhcp_packet_t)) != 0) {
        PRINT(RED, BLACK, "[DHCP] Failed to send REQUEST\n");
        kfree(packet);
        return -1;
    }
    
    // Wait for ACK with active polling
    PRINT(WHITE, BLACK, "[DHCP] Waiting for ACK");
    timeout = 1000;
    for (int i = 0; i < timeout; i++) {
        // CRITICAL: Poll E1000 for received packets!
        e1000_interrupt_handler();
        
        if (dhcp_state == 2) {
            PRINT(WHITE, BLACK, " OK\n");
            break;
        }
        if (i % 100 == 0) PRINT(WHITE, BLACK, ".");
        for (volatile int j = 0; j < 100000; j++);
    }
    
    kfree(packet);
    
    if (dhcp_state != 2) {
        PRINT(RED, BLACK, " TIMEOUT\n");
        return -1;
    }
    
    PRINT(GREEN, BLACK, "\n=== DHCP Complete ===\n");
    net_config_t *config = net_get_config();
    PRINT(WHITE, BLACK, "  IP:      ");
    net_print_ip(config->ip);
    PRINT(WHITE, BLACK, "\n  Netmask: ");
    net_print_ip(config->netmask);
    PRINT(WHITE, BLACK, "\n  Gateway: ");
    net_print_ip(config->gateway);
    PRINT(WHITE, BLACK, "\n");
    
    if (dhcp_dns != 0) {
        PRINT(WHITE, BLACK, "  DNS:     ");
        net_print_ip(dhcp_dns);
        PRINT(WHITE, BLACK, "\n");
    }
    
    return 0;
}

int dhcp_discover(void) { return 0; }
int dhcp_request(void) { return 0; }
int dhcp_wait_complete(int timeout_ms) { return 0; }