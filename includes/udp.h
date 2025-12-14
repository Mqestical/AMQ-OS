
// ========== udp.h ==========
#ifndef UDP_H
#define UDP_H

#include <stdint.h>

typedef void (*udp_handler_t)(uint32_t src_ip, uint16_t src_port, 
                               uint8_t *data, uint16_t length);

void udp_init(void);
void udp_receive(uint32_t src_ip, uint8_t *data, uint16_t length);
int udp_send(uint32_t dest_ip, uint16_t src_port, uint16_t dest_port,
             const void *data, uint16_t length);
void udp_register_handler(uint16_t port, udp_handler_t handler);

#endif // UDP_H