#include <efi.h>
#include <efilib.h>
#ifndef STRING_HELPERS_H
#define STRING_HELPERS_H

extern int strcmp(const char* s1, const char* s2);
extern int strncmp(const char* s1, const char* s2, int n);


#define SAFE_STR(dest, literal) do { \
    char _tmp[] = literal; \
    int _i = 0; \
    while (_tmp[_i] && _i < sizeof(dest) - 1) { \
        dest[_i] = _tmp[_i]; \
        _i++; \
    } \
    dest[_i] = '\0'; \
} while(0)


#define PRINT(fg, bg, literal, ...) do { \
    char _msg[128]; \
    char _tmp[] = literal; \
    int _i = 0; \
    while (_tmp[_i] && _i < 127) { \
        _msg[_i] = _tmp[_i]; \
        _i++; \
    } \
    _msg[_i] = '\0'; \
    printk(fg, bg, _msg, ##__VA_ARGS__); \
} while(0)

#define ASM_ERROR(ctx, literal) do { \
    char _tmp[] = literal; \
    asm_error(ctx, _tmp); \
} while(0)

#define STRNCMP(intermediate, literal, n) ({      \
    char _lit_buf[128];                                \
    char _tmp[] = literal;                             \
    int _i = 0;                                        \
    while (_tmp[_i] && _i < 127) {                     \
        _lit_buf[_i] = _tmp[_i];                       \
        _i++;                                          \
    }                                                  \
    _lit_buf[_i] = '\0';                               \
    strncmp((const char *)(intermediate), _lit_buf, n);\
})


#define STRCMP(a, b) ({                   \
    char _msg[128];                       \
    char _tmp[] = a;                      \
    int _i = 0;                           \
    while (_tmp[_i] && _i < 127) {        \
        _msg[_i] = _tmp[_i];              \
        _i++;                             \
    }                                     \
    _msg[_i] = '\0';                      \
    strcmp(_msg, b);                      \
})

#define STRLEN(s) ({           \
    const char *_tmp = (s);    \
    size_t _len = 0;           \
    while (_tmp[_len]) _len++; \
    _len;                      \
})

#define STRCPY(dest, src) ({           \
    char *_d = (dest);                 \
    const char *_s = (src);            \
    while ((*_d++ = *_s++));           \
    (dest);                             \
})

#define STRCAT(dest, src) ({           \
    char *_d = (dest);                 \
    const char *_s = (src);            \
    while (*_d) _d++;                   \
    while ((*_d++ = *_s++));           \
    (dest);                             \
})


#endif