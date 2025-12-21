
#include "tcp.h"
#include "net.h"
#include "print.h"
#include "string_helpers.h"
#include "memory.h"
#include "E1000.h"

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
static uint16_t next_port = 49152;

void tcp_init(void) {
    for (int i = 0; i < MAX_TCP_SOCKETS; i++) {
        tcp_sockets[i].in_use = 0;
        tcp_sockets[i].state = TCP_STATE_CLOSED;
    }
    PRINT(GREEN, BLACK, "[TCP] Initialized\n");
}

static uint16_t tcp_calculate_checksum(uint32_t src_ip, uint32_t dest_ip,
                                       const uint8_t *data, uint16_t length) {
    uint32_t sum = 0;

    sum += (src_ip >> 16) & 0xFFFF;
    sum += src_ip & 0xFFFF;
    sum += (dest_ip >> 16) & 0xFFFF;
    sum += dest_ip & 0xFFFF;
    sum += net_htons(IP_PROTO_TCP);
    sum += net_htons(length);

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
    tcp->data_offset_reserved = (5 << 4);
    tcp->flags = flags;
    tcp->window_size = net_htons(8192);
    tcp->checksum = 0;
    tcp->urgent_ptr = 0;

    if (data_len > 0) {
        uint8_t *dest = buffer + sizeof(tcp_header_t);
        const uint8_t *src = (const uint8_t*)data;
        for (uint16_t i = 0; i < data_len; i++) {
            dest[i] = src[i];
        }
    }

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
            tcp_sockets[i].seq_num = 1000;

            PRINT(GREEN, BLACK, "[TCP] Created socket on port %d\n", tcp_sockets[i].local_port);
            return &tcp_sockets[i];
        }
    }

    PRINT(RED, BLACK, "[TCP] No free sockets!\n");
    return NULL;
}

int tcp_connect(tcp_socket_t *sock, uint32_t dest_ip, uint16_t dest_port) {
    if (!sock || sock->state != TCP_STATE_CLOSED) {
        PRINT(RED, BLACK, "[TCP] Invalid socket or bad state\n");
        return -1;
    }

    sock->remote_ip = dest_ip;
    sock->remote_port = dest_port;
    sock->state = TCP_STATE_SYN_SENT;

    PRINT(CYAN, BLACK, "[TCP] Connecting to ");
    net_print_ip(dest_ip);
    PRINT(WHITE, BLACK, ":%d from port %d\n", dest_port, sock->local_port);
    PRINT(WHITE, BLACK, "[TCP] Sending SYN...\n");

    if (tcp_send_packet(sock, TCP_SYN, NULL, 0) != 0) {
        PRINT(RED, BLACK, "[TCP] Failed to send SYN\n");
        sock->state = TCP_STATE_CLOSED;
        return -1;
    }

    sock->seq_num++;

    PRINT(WHITE, BLACK, "[TCP] Waiting for SYN-ACK");


    int timeout = 5000;
    int last_state = sock->state;

    for (int i = 0; i < timeout; i++) {
        for (int j = 0; j < 100; j++) {
            e1000_interrupt_handler();

            if (sock->state == TCP_STATE_ESTABLISHED) {
                PRINT(WHITE, BLACK, "\n");
                PRINT(GREEN, BLACK, "[TCP] Connected! (took ~%dms)\n", i);
                return 0;
            }


            if (sock->state != last_state) {
                PRINT(WHITE, BLACK, "\n[TCP] State changed: %d -> %d\n", last_state, sock->state);
                last_state = sock->state;
            }
        }

        for (volatile int k = 0; k < 100; k++);

        if (i % 500 == 0 && i > 0) {
            PRINT(WHITE, BLACK, ".");
        }
    }

    PRINT(WHITE, BLACK, "\n");
    PRINT(RED, BLACK, "[TCP] Connection timeout (state=%d)\n", sock->state);
    sock->state = TCP_STATE_CLOSED;
    return -1;
}

int tcp_send(tcp_socket_t *sock, const void *data, uint16_t length) {
    if (!sock || sock->state != TCP_STATE_ESTABLISHED) {
        PRINT(RED, BLACK, "[TCP] Cannot send - socket not established (state=%d)\n",
              sock ? sock->state : -1);
        return -1;
    }

    PRINT(CYAN, BLACK, "[TCP] Sending %d bytes\n", length);

    if (tcp_send_packet(sock, TCP_PSH | TCP_ACK, data, length) != 0) {
        PRINT(RED, BLACK, "[TCP] Send failed\n");
        return -1;
    }

    sock->seq_num += length;

    PRINT(GREEN, BLACK, "[TCP] Sent successfully\n");
    return 0;
}

int tcp_close(tcp_socket_t *sock) {
    if (!sock) return -1;

    PRINT(YELLOW, BLACK, "[TCP] Closing socket (state=%d)\n", sock->state);

    if (sock->state == TCP_STATE_ESTABLISHED) {
        sock->state = TCP_STATE_FIN_WAIT_1;
        tcp_send_packet(sock, TCP_FIN | TCP_ACK, NULL, 0);
        sock->seq_num++;
    }


    for (int i = 0; i < 1000; i++) {
        for (int p = 0; p < 10; p++) {
            e1000_interrupt_handler();
        }
        if (sock->state == TCP_STATE_CLOSED) break;
        for (volatile int j = 0; j < 1000; j++);
    }

    sock->in_use = 0;
    sock->state = TCP_STATE_CLOSED;

    PRINT(GREEN, BLACK, "[TCP] Socket closed\n");
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

    PRINT(CYAN, BLACK, "[TCP] RX from ");
    net_print_ip(src_ip);
    PRINT(WHITE, BLACK, ":%d -> :%d flags=0x%02x seq=%u ack=%u\n",
          src_port, dest_port, tcp->flags, seq, ack);


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

    if (!sock) {
        PRINT(YELLOW, BLACK, "[TCP] No matching socket\n");
        return;
    }

    PRINT(WHITE, BLACK, "[TCP] Socket found, state=%d\n", sock->state);


    if (sock->state == TCP_STATE_SYN_SENT) {
        if (tcp->flags & TCP_SYN && tcp->flags & TCP_ACK) {
            PRINT(GREEN, BLACK, "[TCP] Got SYN-ACK!\n");
            sock->ack_num = seq + 1;
            sock->state = TCP_STATE_ESTABLISHED;
            PRINT(WHITE, BLACK, "[TCP] Sending final ACK\n");
            tcp_send_packet(sock, TCP_ACK, NULL, 0);
            PRINT(GREEN, BLACK, "[TCP] Connection established!\n");
        }
    }
    else if (sock->state == TCP_STATE_ESTABLISHED) {
        if (tcp->flags & TCP_FIN) {
            PRINT(YELLOW, BLACK, "[TCP] Got FIN\n");
            sock->ack_num = seq + 1;
            sock->state = TCP_STATE_CLOSE_WAIT;
            tcp_send_packet(sock, TCP_ACK, NULL, 0);
            tcp_send_packet(sock, TCP_FIN | TCP_ACK, NULL, 0);
            sock->seq_num++;
            sock->state = TCP_STATE_LAST_ACK;
        }
        else if (tcp->flags & TCP_ACK) {
            uint16_t header_len = (tcp->data_offset_reserved >> 4) * 4;
            uint16_t data_len = length - header_len;

            if (data_len > 0) {
                uint8_t *payload = data + header_len;

                PRINT(GREEN, BLACK, "[TCP] Received %d bytes of data\n", data_len);

                extern void http_tcp_data_received(uint8_t *data, int len);
                http_tcp_data_received(payload, data_len);

                sock->ack_num = seq + data_len;
                tcp_send_packet(sock, TCP_ACK, NULL, 0);
            }
        }
    }
    else if (sock->state == TCP_STATE_LAST_ACK) {
        if (tcp->flags & TCP_ACK) {
            PRINT(GREEN, BLACK, "[TCP] Connection fully closed\n");
            sock->state = TCP_STATE_CLOSED;
        }
    }
}