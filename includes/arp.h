// ========== arp.h ==========
#ifndef ARP_H
#define ARP_H

#include <stdint.h>

void arp_init(void);
void arp_receive(uint8_t *data, uint16_t length);
int arp_resolve(uint32_t ip, uint8_t *mac);
void arp_send_request(uint32_t target_ip);
void arp_print_cache(void);

#endif // ARP_H