// ========== icmp.h ==========
#ifndef ICMP_H
#define ICMP_H

#include <stdint.h>

void icmp_init(void);
void icmp_receive(uint32_t src_ip, uint8_t *data, uint16_t length);
int icmp_send_ping(uint32_t dest_ip, uint16_t id, uint16_t seq);

#endif // ICMP_H