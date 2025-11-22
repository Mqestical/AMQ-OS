#ifndef PRINT_H
#define PRINT_H

#include <efi.h>
#include <efilib.h>
#include <stddef.h>
#include <stdint.h>

#define STDIN  0
#define STDOUT 1
#define STDERR 2

// ----------- ATTRIBUTES MACROS -------------
#define NO_THROW   __attribute__((nothrow))
#define NON_NULL(...) __attribute__((nonnull(__VA_ARGS__)))
#define OPT_O3    __attribute__((optimize("O3")))
#define PRINTF_FMT(fmt_idx, arg_idx) __attribute__((format(printf, fmt_idx, arg_idx)))

// --------------------------------------------

typedef struct {
    uint32_t *base;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bytes_per_pixel;
} Framebuffer;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t fg_color;
    uint32_t bg_color;
} Cursor;

typedef struct {
    char data[4096];
    size_t write_pos;
    size_t read_pos;
} Buffer;

typedef struct {
    int fd;
    int type;
    void *buffer;
    size_t pos;
    size_t size;
} FileDescriptor;

// Globals
extern Framebuffer fb;
extern Cursor cursor;
extern EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
extern Buffer stdin_buf;
extern Buffer stdout_buf;
extern Buffer stderr_buf;
extern FileDescriptor fd_table[256];

// New: runtime flag indicating whether UEFI Console services may still be used.
// Set to 1 by default; start.c will clear it after ExitBootServices.
extern int uefi_active;

// ---------------- GRAPHICS FUNCTIONS ------------------
// Safe to O3 optimize (pure framebuffer memory writes)
void init_graphics(EFI_SYSTEM_TABLE *ST) NO_THROW;  // Don't O3 UEFI calls
void put_pixel(uint32_t x, uint32_t y, uint32_t color) NO_THROW OPT_O3;
/* draw_char takes a char glyph (not a pointer) so NON_NULL doesn't apply here */
void draw_char(uint32_t x, uint32_t y, char c,
               uint32_t fg, uint32_t bg) NO_THROW OPT_O3;
void draw_string(uint32_t x, uint32_t y, const char *s,
                 uint32_t fg, uint32_t bg) NO_THROW OPT_O3 NON_NULL(3);

// ---------------- PRINT FUNCTIONS ----------------------
void printc(char c) NO_THROW OPT_O3;

// printk gets printf-style checking:
void printk(uint32_t text_fg, uint32_t text_bg,
            const char *format, ...)
            NO_THROW NON_NULL(3) PRINTF_FMT(3, 4);

        #define PRINTK(fg, bg, msg) \
    do { \
        const char *_msg = msg; \
        printk(fg, bg, _msg); \
    } while (0)


            void printcs(char *str);
             void print_signed(long long num);
                void print_unsigned(unsigned long long num, int base);

// ---------------- SCREEN CONTROL -----------------------
void SetCursorPos(uint32_t x, uint32_t y) NO_THROW;
void SetColors(uint32_t fg, uint32_t bg) NO_THROW;
void ClearScreen(uint32_t color) NO_THROW OPT_O3;
// ---------------- FILE DESCRIPTOR FUNCTIONS ------------
void init_fds() NO_THROW;
void buf_write(Buffer *buf, const char *str) NO_THROW NON_NULL(1,2);

#endif