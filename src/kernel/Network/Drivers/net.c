#include "net.h"
#include "E1000.h"
#include "arp.h"
#include "icmp.h"
#include "udp.h"
#include "tcp.h"
#include "print.h"
#include "string_helpers.h"
#include "memory.h"
#include "dns.h"

static net_config_t net_config = {0};
extern void dhcp_init(void);
void net_init(void) {
    PRINT(CYAN, BLACK, "\n[NET] Initializing network stack...\n");
    
    // Initialize E1000 driver
    if (e1000_init() != 0) {
        PRINT(YELLOW, BLACK, "[NET] Failed to initialize network card\n");
        return;
    }
    
    // Initialize protocols
    arp_init();
    icmp_init();
    udp_init();
    tcp_init();
    dns_init();
    dhcp_init();
    PRINT(GREEN, BLACK, "[NET] Network stack initialized\n");
}

void net_register_device(uint8_t *mac) {
    for (int i = 0; i < 6; i++) {
        net_config.mac[i] = mac[i];
    }
    net_config.configured = 0;
}

void net_set_config(uint32_t ip, uint32_t netmask, uint32_t gateway) {
    net_config.ip = ip;
    net_config.netmask = netmask;
    net_config.gateway = gateway;
    net_config.configured = 1;  // CRITICAL!
    
    PRINT(GREEN, BLACK, "[NET] Configuration updated:\n");
    PRINT(WHITE, BLACK, "  IP:      ");
    net_print_ip(ip);
    PRINT(WHITE, BLACK, "\n  Netmask: ");
    net_print_ip(netmask);
    PRINT(WHITE, BLACK, "\n  Gateway: ");
    net_print_ip(gateway);
    PRINT(WHITE, BLACK, "\n");
    PRINT(GREEN, BLACK, "[NET] Configured flag: %d\n", net_config.configured);
}

net_config_t* net_get_config(void) {
    return &net_config;
}

void net_receive_packet(uint8_t *data, uint16_t length) {
    if (length < sizeof(eth_frame_t)) return;
    
    eth_frame_t *frame = (eth_frame_t*)data;
    uint16_t ethertype = net_htons(frame->ethertype);
    
    // DEBUG: Print EtherType
    PRINT(YELLOW, BLACK, "[NET] RX EtherType: 0x%04x\n", ethertype);
    
    switch (ethertype) {
        case ETH_TYPE_ARP:  // 0x0806
            PRINT(CYAN, BLACK, "[NET] -> ARP packet\n");
            arp_receive(frame->payload, length - sizeof(eth_frame_t));
            return;  // CRITICAL: return here, don't fall through
            
        case ETH_TYPE_IPV4:  // 0x0800
       //     PRINT(CYAN, BLACK, "[NET] -> IPv4 packet\n");
            if (length >= sizeof(eth_frame_t) + sizeof(ipv4_header_t)) {
                ipv4_header_t *ip = (ipv4_header_t*)frame->payload;
                
                // Check if packet is for us (or broadcast)
                if (net_config.configured && 
                    ip->dest_ip != net_config.ip && 
                    ip->dest_ip != 0xFFFFFFFF) {
                    PRINT(YELLOW, BLACK, "[NET] Not for us, dropping\n");
                    return;
                }
                
                uint16_t ip_header_len = (ip->version_ihl & 0x0F) * 4;
                uint8_t *payload = frame->payload + ip_header_len;
                uint16_t payload_len = net_htons(ip->total_length) - ip_header_len;
                
            //    PRINT(WHITE, BLACK, "[NET] IP proto: %d\n", ip->protocol);
                
                switch (ip->protocol) {
                    case IP_PROTO_ICMP:  // 1
                    //    PRINT(CYAN, BLACK, "[NET] -> ICMP\n");
                        icmp_receive(ip->src_ip, payload, payload_len);
                        break;
                    case IP_PROTO_UDP:   // 17
                     //   PRINT(CYAN, BLACK, "[NET] -> UDP\n");
                        udp_receive(ip->src_ip, payload, payload_len);
                        break;
                    case IP_PROTO_TCP:   // 6
                    //    PRINT(CYAN, BLACK, "[NET] -> TCP\n");
                        tcp_receive(ip->src_ip, payload, payload_len);
                        break;
                    default:
                        PRINT(YELLOW, BLACK, "[NET] Unknown IP protocol: %d\n", ip->protocol);
                        break;
                }
            }
            break;
            
        default:
            PRINT(YELLOW, BLACK, "[NET] Unknown EtherType: 0x%04x\n", ethertype);
            break;
    }
}

int net_send_ethernet(uint8_t *dest_mac, uint16_t ethertype, 
                      const void *payload, uint16_t length) {
    uint16_t total_len = sizeof(eth_frame_t) + length;
    uint8_t *buffer = (uint8_t*)kmalloc(total_len);
    
    eth_frame_t *frame = (eth_frame_t*)buffer;
    
    // Fill ethernet header
    for (int i = 0; i < 6; i++) {
        frame->dest_mac[i] = dest_mac[i];
        frame->src_mac[i] = net_config.mac[i];
    }
    frame->ethertype = net_htons(ethertype);
    
    // Copy payload
    uint8_t *dest = buffer + sizeof(eth_frame_t);
    const uint8_t *src = (const uint8_t*)payload;
    for (uint16_t i = 0; i < length; i++) {
        dest[i] = src[i];
    }
    
    // Send via E1000
    int result = e1000_send_packet(buffer, total_len);
    
    kfree(buffer);
    return result;
}
int net_send_ipv4(uint32_t dest_ip, uint8_t protocol, 
                  const void *payload, uint16_t length) {
    net_config_t *config = net_get_config();
    uint32_t src_ip = config->configured ? config->ip : 0x00000000;
    
    // LOOPBACK: If destination is our own IP, handle locally  
    if (dest_ip == src_ip && src_ip != 0) {
        PRINT(GREEN, BLACK, "[NET] Loopback - direct local delivery\n");
        
        // For ICMP Echo Request, convert to Echo Reply immediately
        if (protocol == IP_PROTO_ICMP) {
            icmp_header_t *icmp = (icmp_header_t*)payload;
            if (icmp->type == ICMP_TYPE_ECHO_REQUEST) {
                PRINT(CYAN, BLACK, "[NET] Loopback ping - converting to reply\n");
                
                // Make a copy and convert to reply
                uint8_t *reply = (uint8_t*)kmalloc(length);
                for (uint16_t i = 0; i < length; i++) reply[i] = ((uint8_t*)payload)[i];
                
                icmp_header_t *reply_hdr = (icmp_header_t*)reply;
                reply_hdr->type = ICMP_TYPE_ECHO_REPLY;
                reply_hdr->checksum = 0;
                reply_hdr->checksum = net_checksum(reply, length);
                
                // Deliver reply directly
                icmp_receive(src_ip, reply, length);
                
                kfree(reply);
                return 0;
            }
        }
        
        // For other protocols, deliver as-is
        switch (protocol) {
            case IP_PROTO_ICMP:
                icmp_receive(src_ip, (uint8_t*)payload, length);
                break;
            case IP_PROTO_UDP:
                udp_receive(src_ip, (uint8_t*)payload, length);
                break;
            case IP_PROTO_TCP:
                tcp_receive(src_ip, (uint8_t*)payload, length);
                break;
        }
        
        return 0;
    }
    
    // Build IP packet
    uint16_t total_len = sizeof(ipv4_header_t) + length;
    uint8_t *buffer = (uint8_t*)kmalloc(total_len);
    
    ipv4_header_t *ip = (ipv4_header_t*)buffer;
    ip->version_ihl = 0x45;
    ip->dscp_ecn = 0;
    ip->total_length = net_htons(total_len);
    ip->identification = 0;
    ip->flags_fragment = 0;
    ip->ttl = 64;
    ip->protocol = protocol;
    ip->header_checksum = 0;
    ip->src_ip = src_ip;
    ip->dest_ip = dest_ip;
    
    ip->header_checksum = net_checksum(ip, sizeof(ipv4_header_t));
    
    // Copy payload
    uint8_t *dest = buffer + sizeof(ipv4_header_t);
    const uint8_t *src = (const uint8_t*)payload;
    for (uint16_t i = 0; i < length; i++) {
        dest[i] = src[i];
    }
    
    // Get destination MAC
    uint8_t dest_mac[6];
    
    if (dest_ip == 0xFFFFFFFF) {
        // BROADCAST
        for (int i = 0; i < 6; i++) {
            dest_mac[i] = 0xFF;
        }
    } else {
        // Check if on same subnet
        uint32_t subnet_dest = dest_ip & config->netmask;
        uint32_t subnet_us = src_ip & config->netmask;
        
        uint32_t route_ip;
        if (subnet_dest == subnet_us) {
            // Same subnet - ARP for dest directly
            route_ip = dest_ip;
        } else {
            // Different subnet - use gateway
            if (config->gateway == 0) {
                PRINT(RED, BLACK, "[NET] No gateway configured!\n");
                kfree(buffer);
                return -1;
            }
            route_ip = config->gateway;
        }
        
        // ARP resolve (cache will prevent spam)
        if (arp_resolve(route_ip, dest_mac) != 0) {
            PRINT(RED, BLACK, "[NET] ARP failed for ");
            net_print_ip(route_ip);
            PRINT(WHITE, BLACK, "\n");
            kfree(buffer);
            return -1;
        }
    }
    
    int result = net_send_ethernet(dest_mac, ETH_TYPE_IPV4, buffer, total_len);
    kfree(buffer);
    return result;
}
uint16_t net_checksum(const void *data, size_t length) {
    const uint16_t *words = (const uint16_t*)data;
    uint32_t sum = 0;
    
    while (length > 1) {
        sum += *words++;
        length -= 2;
    }
    
    if (length == 1) {
        sum += *(const uint8_t*)words;
    }
    
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return (uint16_t)~sum;
}

uint16_t net_htons(uint16_t n) {
    return ((n & 0xFF) << 8) | ((n >> 8) & 0xFF);
}

uint32_t net_htonl(uint32_t n) {
    return ((n & 0xFF) << 24) | 
           ((n & 0xFF00) << 8) |
           ((n >> 8) & 0xFF00) |
           ((n >> 24) & 0xFF);
}

void net_print_ip(uint32_t ip) {
    PRINT(WHITE, BLACK, "%d.%d.%d.%d",
          (ip >> 0) & 0xFF,
          (ip >> 8) & 0xFF,
          (ip >> 16) & 0xFF,
          (ip >> 24) & 0xFF);
}

void net_print_mac(uint8_t *mac) {
    PRINT(WHITE, BLACK, "%02x:%02x:%02x:%02x:%02x:%02x",
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

uint32_t net_parse_ip(const char *str) {
    uint32_t octets[4] = {0};
    int idx = 0;
    
    for (int i = 0; str[i] && idx < 4; i++) {
        if (str[i] >= '0' && str[i] <= '9') {
            octets[idx] = octets[idx] * 10 + (str[i] - '0');
        } else if (str[i] == '.') {
            idx++;
        }
    }
    
    return octets[0] | (octets[1] << 8) | 
           (octets[2] << 16) | (octets[3] << 24);
}