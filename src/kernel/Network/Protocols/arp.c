
#include "E1000.h"
#include "arp.h"
#include "net.h"
#include "print.h"
#include "string_helpers.h"
#include "memory.h"

#define ARP_CACHE_SIZE 32
#define ARP_CACHE_TTL 300

typedef struct {
    uint32_t ip;
    uint8_t  mac[6];
    uint32_t timestamp;
    int      valid;
} arp_cache_entry_t;

static arp_cache_entry_t arp_cache[ARP_CACHE_SIZE];
static uint32_t arp_time = 0;

void arp_init(void) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        arp_cache[i].valid = 0;
    }
}

static void arp_cache_add(uint32_t ip, uint8_t *mac) {

    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {

            for (int j = 0; j < 6; j++) {
                arp_cache[i].mac[j] = mac[j];
            }
            arp_cache[i].timestamp = arp_time++;
            return;
        }
    }


    int idx = -1;
    uint32_t oldest = 0xFFFFFFFF;

    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid) {
            idx = i;
            break;
        }
        if (arp_cache[i].timestamp < oldest) {
            oldest = arp_cache[i].timestamp;
            idx = i;
        }
    }

    if (idx >= 0) {
        arp_cache[idx].ip = ip;
        for (int i = 0; i < 6; i++) {
            arp_cache[idx].mac[i] = mac[i];
        }
        arp_cache[idx].timestamp = arp_time++;
        arp_cache[idx].valid = 1;
    }
}

static int arp_cache_lookup(uint32_t ip, uint8_t *mac) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            for (int j = 0; j < 6; j++) {
                mac[j] = arp_cache[i].mac[j];
            }
            return 0;
        }
    }
    return -1;
}

void arp_receive(uint8_t *data, uint16_t length) {
    if (length < sizeof(arp_packet_t)) return;

    arp_packet_t *arp = (arp_packet_t*)data;
    uint16_t op = net_htons(arp->operation);

    net_config_t *config = net_get_config();


    arp_cache_add(arp->sender_proto_addr, arp->sender_hw_addr);

    if (op == ARP_REQUEST && arp->target_proto_addr == config->ip) {

        arp_packet_t reply;
        reply.hw_type = net_htons(1);
        reply.proto_type = net_htons(ETH_TYPE_IPV4);
        reply.hw_addr_len = 6;
        reply.proto_addr_len = 4;
        reply.operation = net_htons(ARP_REPLY);

        for (int i = 0; i < 6; i++) {
            reply.sender_hw_addr[i] = config->mac[i];
            reply.target_hw_addr[i] = arp->sender_hw_addr[i];
        }
        reply.sender_proto_addr = config->ip;
        reply.target_proto_addr = arp->sender_proto_addr;

        net_send_ethernet(arp->sender_hw_addr, ETH_TYPE_ARP,
                         &reply, sizeof(reply));
    }
    else if (op == ARP_REPLY) {

        PRINT(GREEN, BLACK, "[ARP] Reply: ");
        net_print_ip(arp->sender_proto_addr);
        PRINT(WHITE, BLACK, " -> ");
        net_print_mac(arp->sender_hw_addr);
        PRINT(WHITE, BLACK, "\n");
    }
}

void arp_send_request(uint32_t target_ip) {
    net_config_t *config = net_get_config();

    arp_packet_t request;
    request.hw_type = net_htons(1);
    request.proto_type = net_htons(ETH_TYPE_IPV4);
    request.hw_addr_len = 6;
    request.proto_addr_len = 4;
    request.operation = net_htons(ARP_REQUEST);

    for (int i = 0; i < 6; i++) {
        request.sender_hw_addr[i] = config->mac[i];
        request.target_hw_addr[i] = 0;
    }
    request.sender_proto_addr = config->ip;
    request.target_proto_addr = target_ip;

    uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    net_send_ethernet(broadcast, ETH_TYPE_ARP, &request, sizeof(request));
}

int arp_resolve(uint32_t ip, uint8_t *mac) {

    if (arp_cache_lookup(ip, mac) == 0) {

        return 0;
    }

    PRINT(YELLOW, BLACK, "[ARP] Resolving ");
    net_print_ip(ip);
    PRINT(WHITE, BLACK, "...\n");


    for (int retry = 0; retry < 3; retry++) {
        arp_send_request(ip);


        for (int i = 0; i < 500; i++) {

            for (int p = 0; p < 20; p++) {
                e1000_interrupt_handler();
            }

            if (arp_cache_lookup(ip, mac) == 0) {
                PRINT(GREEN, BLACK, "[ARP] Resolved ");
                net_print_ip(ip);
                PRINT(WHITE, BLACK, " -> ");
                net_print_mac(mac);
                PRINT(WHITE, BLACK, "\n");
                return 0;
            }


            for (volatile int j = 0; j < 1000; j++);
        }

        if (retry < 2) {
            PRINT(YELLOW, BLACK, "[ARP] Retry %d/3\n", retry + 2);
        }
    }

    PRINT(RED, BLACK, "[ARP] Failed to resolve ");
    net_print_ip(ip);
    PRINT(WHITE, BLACK, "\n");
    return -1;
}

void arp_print_cache(void) {
    PRINT(CYAN, BLACK, "\n=== ARP Cache ===\n");
    int count = 0;

    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid) {
            net_print_ip(arp_cache[i].ip);
            PRINT(WHITE, BLACK, " -> ");
            net_print_mac(arp_cache[i].mac);
            PRINT(WHITE, BLACK, "\n");
            count++;
        }
    }

    if (count == 0) {
        PRINT(WHITE, BLACK, "(empty)\n");
    }
}