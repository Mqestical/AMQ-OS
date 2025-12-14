// ========== tcp.h ==========
#ifndef TCP_H
#define TCP_H

#include <stdint.h>

// TCP States
#define TCP_STATE_CLOSED      0
#define TCP_STATE_LISTEN      1
#define TCP_STATE_SYN_SENT    2
#define TCP_STATE_SYN_RCVD    3
#define TCP_STATE_ESTABLISHED 4
#define TCP_STATE_FIN_WAIT_1  5
#define TCP_STATE_FIN_WAIT_2  6
#define TCP_STATE_CLOSE_WAIT  7
#define TCP_STATE_CLOSING     8
#define TCP_STATE_LAST_ACK    9
#define TCP_STATE_TIME_WAIT   10

typedef struct tcp_socket tcp_socket_t;

void tcp_init(void);
void tcp_receive(uint32_t src_ip, uint8_t *data, uint16_t length);

tcp_socket_t* tcp_socket(void);
int tcp_connect(tcp_socket_t *sock, uint32_t dest_ip, uint16_t dest_port);
int tcp_send(tcp_socket_t *sock, const void *data, uint16_t length);
int tcp_close(tcp_socket_t *sock);
int tcp_get_state(tcp_socket_t *sock);

#endif // TCP_H
