
#ifndef STRING_HELPERS_H
#define STRING_HELPERS_H

// ============================================================================
// SAFE STRING COPY MACRO
// ============================================================================
// Use this instead of direct string literals to avoid .rodata issues

#define SAFE_STR(dest, literal) do { \
    char _tmp[] = literal; \
    int _i = 0; \
    while (_tmp[_i] && _i < sizeof(dest) - 1) { \
        dest[_i] = _tmp[_i]; \
        _i++; \
    } \
    dest[_i] = '\0'; \
} while(0)

// ============================================================================
// SAFE PRINTK MACRO
// ============================================================================
// Use this for printing with string literals

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
#endif