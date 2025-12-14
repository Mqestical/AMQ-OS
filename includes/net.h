#ifndef NET_H
#define NET_H

#include <stdint.h>
#include <stddef.h>

// Ethernet frame structure
typedef struct {
    uint8_t  dest_mac[6];
    uint8_t  src_mac[6];
    uint16_t ethertype;
    uint8_t  payload[];
} __attribute__((packed)) eth_frame_t;

// Ethertypes
#define ETH_TYPE_IPV4   0x0800
#define ETH_TYPE_ARP    0x0806
#define ETH_TYPE_IPV6   0x86DD

// IPv4 header
typedef struct {
    uint8_t  version_ihl;
    uint8_t  dscp_ecn;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t header_checksum;
    uint32_t src_ip;
    uint32_t dest_ip;
} __attribute__((packed)) ipv4_header_t;

// IP Protocols
#define IP_PROTO_ICMP   1
#define IP_PROTO_TCP    6
#define IP_PROTO_UDP    17

// ICMP header
typedef struct {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t sequence;
} __attribute__((packed)) icmp_header_t;

// ICMP types
#define ICMP_TYPE_ECHO_REPLY    0
#define ICMP_TYPE_ECHO_REQUEST  8

// UDP header
typedef struct {
    uint16_t src_port;
    uint16_t dest_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed)) udp_header_t;

// TCP header
typedef struct {
    uint16_t src_port;
    uint16_t dest_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t  data_offset_reserved;
    uint8_t  flags;
    uint16_t window_size;
    uint16_t checksum;
    uint16_t urgent_ptr;
} __attribute__((packed)) tcp_header_t;

// TCP flags
#define TCP_FIN  0x01
#define TCP_SYN  0x02
#define TCP_RST  0x04
#define TCP_PSH  0x08
#define TCP_ACK  0x10
#define TCP_URG  0x20

// ARP header
typedef struct {
    uint16_t hw_type;
    uint16_t proto_type;
    uint8_t  hw_addr_len;
    uint8_t  proto_addr_len;
    uint16_t operation;
    uint8_t  sender_hw_addr[6];
    uint32_t sender_proto_addr;
    uint8_t  target_hw_addr[6];
    uint32_t target_proto_addr;
} __attribute__((packed)) arp_packet_t;

// ARP operations
#define ARP_REQUEST  1
#define ARP_REPLY    2

// Network configuration
typedef struct {
    uint8_t  mac[6];
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns;
    int      configured;
} net_config_t;

// Network functions
void net_init(void);
void net_register_device(uint8_t *mac);
void net_receive_packet(uint8_t *data, uint16_t length);
int net_send_ethernet(uint8_t *dest_mac, uint16_t ethertype, 
                      const void *payload, uint16_t length);

// Configuration
void net_set_config(uint32_t ip, uint32_t netmask, uint32_t gateway);
net_config_t* net_get_config(void);

// Utility functions
uint16_t net_checksum(const void *data, size_t length);
uint16_t net_htons(uint16_t n);
uint32_t net_htonl(uint32_t n);
void net_print_ip(uint32_t ip);
void net_print_mac(uint8_t *mac);
uint32_t net_parse_ip(const char *str);
int net_send_ipv4(uint32_t dest_ip, uint8_t protocol, const void *payload, uint16_t length);


#endif // NET_H