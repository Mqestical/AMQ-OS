
#include "E1000.h"
#include "dns.h"
#include "udp.h"
#include "net.h"
#include "print.h"
#include "string_helpers.h"
#include "memory.h"

#define DNS_CLIENT_PORT 53535

static uint32_t dns_server = 0;
static uint32_t dns_resolved_ip = 0;
static volatile int dns_waiting = 0;
static uint16_t dns_query_id = 0x1234;

static void dns_handler(uint32_t src_ip, uint16_t src_port, uint8_t *data, uint16_t length) {
    PRINT(CYAN, BLACK, "[DNS] Got response, length=%d\n", length);

    if (length < 12) {
        PRINT(RED, BLACK, "[DNS] Packet too short\n");
        return;
    }


    PRINT(WHITE, BLACK, "[DNS] First 32 bytes: ");
    for (int i = 0; i < 32 && i < length; i++) {
        if (i > 0 && i % 8 == 0) PRINT(WHITE, BLACK, " ");
        print_unsigned(data[i],16);
    }
    PRINT(WHITE, BLACK, "\n");


    uint16_t id = (data[0] << 8) | data[1];
    uint16_t flags = (data[2] << 8) | data[3];
    uint16_t qdcount = (data[4] << 8) | data[5];
    uint16_t ancount = (data[6] << 8) | data[7];
    uint16_t nscount = (data[8] << 8) | data[9];
    uint16_t arcount = (data[10] << 8) | data[11];

    PRINT(WHITE, BLACK, "[DNS] ID=0x%04x Flags=0x%04x QD=%d AN=%d NS=%d AR=%d\n",
          id, flags, qdcount, ancount, nscount, arcount);


    if (id != dns_query_id) {
        PRINT(YELLOW, BLACK, "[DNS] Not our query (expected 0x%04x)\n", dns_query_id);
        return;
    }


    int rcode = flags & 0x000F;
    if (rcode != 0) {
        PRINT(RED, BLACK, "[DNS] Server error code: %d\n", rcode);
        dns_waiting = 0;
        return;
    }


    if (!(flags & 0x8000)) {
        PRINT(YELLOW, BLACK, "[DNS] Not a response\n");
        return;
    }

    if (ancount == 0) {
        PRINT(YELLOW, BLACK, "[DNS] No answers\n");
        dns_waiting = 0;
        return;
    }

    PRINT(GREEN, BLACK, "[DNS] Valid response with %d answer(s)\n", ancount);


    uint8_t *ptr = data + 12;
    uint8_t *end = data + length;


    for (int q = 0; q < qdcount && ptr < end; q++) {

        while (ptr < end && *ptr != 0) {
            if ((*ptr & 0xC0) == 0xC0) {

                ptr += 2;
                break;
            } else {

                ptr += *ptr + 1;
            }
        }


        if (ptr < end && *ptr == 0) ptr++;


        ptr += 4;
    }

    if (ptr >= end) {
        PRINT(RED, BLACK, "[DNS] Malformed packet (questions overflow)\n");
        dns_waiting = 0;
        return;
    }

    PRINT(WHITE, BLACK, "[DNS] Parsing answers at offset %d\n", (int)(ptr - data));


    for (int a = 0; a < ancount && ptr < end; a++) {
        PRINT(WHITE, BLACK, "[DNS] Answer #%d at offset %d\n", a + 1, (int)(ptr - data));


        if ((*ptr & 0xC0) == 0xC0) {
            PRINT(WHITE, BLACK, "[DNS]   Name: compressed pointer\n");
            ptr += 2;
        } else {
            PRINT(WHITE, BLACK, "[DNS]   Name: ");
            while (ptr < end && *ptr != 0) {
                if ((*ptr & 0xC0) == 0xC0) {
                    ptr += 2;
                    break;
                }
                int len = *ptr++;
                for (int i = 0; i < len && ptr < end; i++) {
                    PRINT(WHITE, BLACK, "%c", *ptr++);
                }
                if (ptr < end && *ptr != 0) {
                    PRINT(WHITE, BLACK, ".");
                }
            }
            PRINT(WHITE, BLACK, "\n");

            if (ptr < end && *ptr == 0) ptr++;
        }


        if (ptr + 10 > end) {
            PRINT(RED, BLACK, "[DNS] Not enough data for answer fields\n");
            break;
        }


        uint16_t type = (ptr[0] << 8) | ptr[1];
        uint16_t class = (ptr[2] << 8) | ptr[3];
        uint32_t ttl = (ptr[4] << 24) | (ptr[5] << 16) | (ptr[6] << 8) | ptr[7];
        uint16_t rdlen = (ptr[8] << 8) | ptr[9];
        ptr += 10;

        PRINT(WHITE, BLACK, "[DNS]   Type=%d Class=%d TTL=%d DataLen=%d\n",
              type, class, ttl, rdlen);


        if (ptr + rdlen > end) {
            PRINT(RED, BLACK, "[DNS] Not enough data for rdata\n");
            break;
        }


        if (type == 1 && rdlen == 4) {
            dns_resolved_ip = ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24);

            PRINT(GREEN, BLACK, "[DNS] *** Found A record: ");
            net_print_ip(dns_resolved_ip);
            PRINT(WHITE, BLACK, " ***\n");

            dns_waiting = 0;
            return;
        }


        ptr += rdlen;
    }

    if (dns_resolved_ip == 0) {
        PRINT(YELLOW, BLACK, "[DNS] No A records found\n");
        dns_waiting = 0;
    }
}

void dns_init(void) {
    udp_register_handler(DNS_CLIENT_PORT, dns_handler);
    PRINT(GREEN, BLACK, "[DNS] Initialized on port %d\n", DNS_CLIENT_PORT);
}

void dns_set_server(uint32_t server_ip) {
    dns_server = server_ip;
    PRINT(GREEN, BLACK, "[DNS] Server set to ");
    net_print_ip(server_ip);
    PRINT(WHITE, BLACK, "\n");
}

static int dns_encode_name(const char *hostname, uint8_t *buffer) {
    uint8_t *start = buffer;
    const char *ptr = hostname;

    while (*ptr) {
        uint8_t *len_ptr = buffer++;
        uint8_t len = 0;
        while (*ptr && *ptr != '.') {
            *buffer++ = *ptr++;
            len++;
        }
        *len_ptr = len;
        if (*ptr == '.') ptr++;
    }
    *buffer++ = 0;
    return buffer - start;
}


static int is_ip_address(const char *str) {
    int dots = 0;
    int digits = 0;

    for (const char *p = str; *p; p++) {
        if (*p == '.') {
            if (digits == 0 || digits > 3) return 0;
            dots++;
            digits = 0;
        } else if (*p >= '0' && *p <= '9') {
            digits++;
        } else {
            return 0;
        }
    }

    return (dots == 3 && digits > 0 && digits <= 3);
}

int dns_resolve(const char *hostname, uint32_t *ip_out) {
    if (!hostname || !ip_out) {
        PRINT(RED, BLACK, "[DNS] Invalid parameters\n");
        return -1;
    }

    PRINT(CYAN, BLACK, "[DNS] Resolving '%s'\n", hostname);


    if (is_ip_address(hostname)) {
        *ip_out = net_parse_ip(hostname);
        PRINT(GREEN, BLACK, "[DNS] Already an IP: ");
        net_print_ip(*ip_out);
        PRINT(WHITE, BLACK, "\n");
        return 0;
    }


    if (dns_server == 0) {
        PRINT(YELLOW, BLACK, "[DNS] No server, using 8.8.8.8\n");
        dns_server = 0x08080808;
    }

    PRINT(WHITE, BLACK, "[DNS] Using server ");
    net_print_ip(dns_server);
    PRINT(WHITE, BLACK, "\n");


    uint8_t buffer[512];


    dns_query_id++;
    buffer[0] = (dns_query_id >> 8) & 0xFF;
    buffer[1] = dns_query_id & 0xFF;
    buffer[2] = 0x01;
    buffer[3] = 0x00;
    buffer[4] = 0x00;
    buffer[5] = 0x01;
    buffer[6] = 0x00;
    buffer[7] = 0x00;
    buffer[8] = 0x00;
    buffer[9] = 0x00;
    buffer[10] = 0x00;
    buffer[11] = 0x00;


    int name_len = dns_encode_name(hostname, buffer + 12);
    uint8_t *ptr = buffer + 12 + name_len;


    *ptr++ = 0x00;
    *ptr++ = 0x01;


    *ptr++ = 0x00;
    *ptr++ = 0x01;

    uint16_t total_len = ptr - buffer;

    PRINT(WHITE, BLACK, "[DNS] Query packet: %d bytes, ID=0x%04x\n", total_len, dns_query_id);

    dns_resolved_ip = 0;
    dns_waiting = 1;


    for (int attempt = 0; attempt < 3; attempt++) {
        if (attempt > 0) {
            PRINT(YELLOW, BLACK, "[DNS] Retry %d/3\n", attempt + 1);
        }


        PRINT(WHITE, BLACK, "[DNS] Sending query to ");
        net_print_ip(dns_server);
        PRINT(WHITE, BLACK, ":53\n");

        udp_send(dns_server, DNS_CLIENT_PORT, 53, buffer, total_len);


        int timeout = 2000;

        PRINT(WHITE, BLACK, "[DNS] Waiting for response");

        for (int i = 0; i < timeout && dns_waiting; i++) {

            for (int p = 0; p < 100; p++) {
                e1000_interrupt_handler();
                if (!dns_waiting) {
                    PRINT(WHITE, BLACK, "\n");
                    goto done;
                }
            }


            for (volatile int j = 0; j < 100; j++);


            if (i % 200 == 0) {
                PRINT(WHITE, BLACK, ".");
            }
        }

        PRINT(WHITE, BLACK, "\n");

        if (!dns_waiting) goto done;
    }

done:
    if (dns_resolved_ip == 0) {
        PRINT(RED, BLACK, "[DNS] Failed after 3 attempts\n");
        return -1;
    }

    *ip_out = dns_resolved_ip;
    PRINT(GREEN, BLACK, "[DNS] Success: ");
    net_print_ip(dns_resolved_ip);
    PRINT(WHITE, BLACK, "\n");
    return 0;
}