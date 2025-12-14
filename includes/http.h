// ========== http.h ==========
#ifndef HTTP_H
#define HTTP_H

#include <stdint.h>

// HTTP GET request
int http_get(const char *url, char *output, int max_len);

// Callback for TCP data (called from tcp.c)
void http_tcp_data_received(uint8_t *data, int len);

void cmd_httptest(void);

#endif // HTTP_H