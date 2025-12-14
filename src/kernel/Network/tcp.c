// ========== tcp.c ==========
#include "tcp.h"
#include "net.h"
#include "print.h"
#include "string_helpers.h"
#include "memory.h"

#define MAX_TCP_SOCKETS 16

struct tcp_socket {
    uint32_t remote_ip;
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t  state;
    uint8_t  in_use;
};

static tcp_socket_t tcp_sockets[MAX_TCP_SOCKETS];
static uint16_t next_port = 49152;  // Ephemeral port range start

void tcp_init(void) {
    for (int i = 0; i < MAX_TCP_SOCKETS; i++) {
        tcp_sockets[i].in_use = 0;
        tcp_sockets[i].state = TCP_STATE_CLOSED;
    }
}

static uint16_t tcp_calculate_checksum(uint32_t src_ip, uint32_t dest_ip,
                                       const uint8_t *data, uint16_t length) {
    // TCP pseudo-header for checksum
    uint32_t sum = 0;
    
    // Pseudo-header
    sum += (src_ip >> 16) & 0xFFFF;
    sum += src_ip & 0xFFFF;
    sum += (dest_ip >> 16) & 0xFFFF;
    sum += dest_ip & 0xFFFF;
    sum += net_htons(IP_PROTO_TCP);
    sum += net_htons(length);
    
    // TCP header + data
    const uint16_t *words = (const uint16_t*)data;
    uint16_t count = length;
    
    while (count > 1) {
        sum += *words++;
        count -= 2;
    }
    
    if (count == 1) {
        sum += *(const uint8_t*)words;
    }
    
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return (uint16_t)~sum;
}

static int tcp_send_packet(tcp_socket_t *sock, uint8_t flags,
                          const void *data, uint16_t data_len) {
    uint16_t total_len = sizeof(tcp_header_t) + data_len;
    uint8_t *buffer = (uint8_t*)kmalloc(total_len);
    
    tcp_header_t *tcp = (tcp_header_t*)buffer;
    tcp->src_port = net_htons(sock->local_port);
    tcp->dest_port = net_htons(sock->remote_port);
    tcp->seq_num = net_htonl(sock->seq_num);
    tcp->ack_num = net_htonl(sock->ack_num);
    tcp->data_offset_reserved = (5 << 4);  // 5 DWORDs, no options
    tcp->flags = flags;
    tcp->window_size = net_htons(8192);
    tcp->checksum = 0;
    tcp->urgent_ptr = 0;
    
    // Copy data
    if (data_len > 0) {
        uint8_t *dest = buffer + sizeof(tcp_header_t);
        const uint8_t *src = (const uint8_t*)data;
        for (uint16_t i = 0; i < data_len; i++) {
            dest[i] = src[i];
        }
    }
    
    // Calculate checksum
    net_config_t *config = net_get_config();
    tcp->checksum = tcp_calculate_checksum(config->ip, sock->remote_ip,
                                           buffer, total_len);
    
    int result = net_send_ipv4(sock->remote_ip, IP_PROTO_TCP, buffer, total_len);
    
    kfree(buffer);
    return result;
}

tcp_socket_t* tcp_socket(void) {
    for (int i = 0; i < MAX_TCP_SOCKETS; i++) {
        if (!tcp_sockets[i].in_use) {
            tcp_sockets[i].in_use = 1;
            tcp_sockets[i].state = TCP_STATE_CLOSED;
            tcp_sockets[i].local_port = next_port++;
            tcp_sockets[i].seq_num = 1000;  // Initial sequence number
            return &tcp_sockets[i];
        }
    }
    return NULL;
}

int tcp_connect(tcp_socket_t *sock, uint32_t dest_ip, uint16_t dest_port) {
    if (!sock || sock->state != TCP_STATE_CLOSED) return -1;
    
    sock->remote_ip = dest_ip;
    sock->remote_port = dest_port;
    sock->state = TCP_STATE_SYN_SENT;
    
    // Send SYN
    PRINT(WHITE, BLACK, "[TCP] Sending SYN to ");
    net_print_ip(dest_ip);
    PRINT(WHITE, BLACK, ":%d\n", dest_port);
    
    tcp_send_packet(sock, TCP_SYN, NULL, 0);
    sock->seq_num++;
    
    // Wait for SYN-ACK
    for (int i = 0; i < 500; i++) {
        if (sock->state == TCP_STATE_ESTABLISHED) {
            PRINT(GREEN, BLACK, "[TCP] Connection established!\n");
            return 0;
        }
        for (volatile int j = 0; j < 100000; j++);
    }
    
    PRINT(YELLOW, BLACK, "[TCP] Connection timeout\n");
    sock->state = TCP_STATE_CLOSED;
    return -1;
}

int tcp_send(tcp_socket_t *sock, const void *data, uint16_t length) {
    if (!sock || sock->state != TCP_STATE_ESTABLISHED) return -1;
    
    tcp_send_packet(sock, TCP_PSH | TCP_ACK, data, length);
    sock->seq_num += length;
    
    return 0;
}

int tcp_close(tcp_socket_t *sock) {
    if (!sock) return -1;
    
    if (sock->state == TCP_STATE_ESTABLISHED) {
        sock->state = TCP_STATE_FIN_WAIT_1;
        tcp_send_packet(sock, TCP_FIN | TCP_ACK, NULL, 0);
        sock->seq_num++;
    }
    
    // Wait a bit for closure
    for (int i = 0; i < 100; i++) {
        if (sock->state == TCP_STATE_CLOSED) break;
        for (volatile int j = 0; j < 100000; j++);
    }
    
    sock->in_use = 0;
    sock->state = TCP_STATE_CLOSED;
    
    return 0;
}

int tcp_get_state(tcp_socket_t *sock) {
    return sock ? sock->state : TCP_STATE_CLOSED;
}

void tcp_receive(uint32_t src_ip, uint8_t *data, uint16_t length) {
    if (length < sizeof(tcp_header_t)) return;
    
    tcp_header_t *tcp = (tcp_header_t*)data;
    uint16_t src_port = net_htons(tcp->src_port);
    uint16_t dest_port = net_htons(tcp->dest_port);
    uint32_t seq = net_htonl(tcp->seq_num);
    uint32_t ack = net_htonl(tcp->ack_num);
    
    // Find matching socket
    tcp_socket_t *sock = NULL;
    for (int i = 0; i < MAX_TCP_SOCKETS; i++) {
        if (tcp_sockets[i].in_use &&
            tcp_sockets[i].remote_ip == src_ip &&
            tcp_sockets[i].remote_port == src_port &&
            tcp_sockets[i].local_port == dest_port) {
            sock = &tcp_sockets[i];
            break;
        }
    }
    
    if (!sock) return;
    
    // State machine
    if (sock->state == TCP_STATE_SYN_SENT) {
        if (tcp->flags & TCP_SYN && tcp->flags & TCP_ACK) {
            sock->ack_num = seq + 1;
            sock->state = TCP_STATE_ESTABLISHED;
            
            // Send ACK
            tcp_send_packet(sock, TCP_ACK, NULL, 0);
        }
    }
    else if (sock->state == TCP_STATE_ESTABLISHED) {
        if (tcp->flags & TCP_FIN) {
            sock->ack_num = seq + 1;
            sock->state = TCP_STATE_CLOSE_WAIT;
            
            // Send ACK
            tcp_send_packet(sock, TCP_ACK, NULL, 0);
            
            // Send FIN
            tcp_send_packet(sock, TCP_FIN | TCP_ACK, NULL, 0);
            sock->seq_num++;
            sock->state = TCP_STATE_LAST_ACK;
        }
        else if (tcp->flags & TCP_ACK) {
            uint16_t data_len = length - ((tcp->data_offset_reserved >> 4) * 4);
            if (data_len > 0) {
                sock->ack_num = seq + data_len;
                tcp_send_packet(sock, TCP_ACK, NULL, 0);
            }
        }
    }
    else if (sock->state == TCP_STATE_LAST_ACK) {
        if (tcp->flags & TCP_ACK) {
            sock->state = TCP_STATE_CLOSED;
        }
    }
}