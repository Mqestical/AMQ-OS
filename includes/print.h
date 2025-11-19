#ifndef PRINT_H
#define PRINT_H

#include <efi.h>
#include <efilib.h>
#include <stddef.h>
#include <stdint.h>

#define STDIN  0
#define STDOUT 1
#define STDERR 2

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

// Graphics functions
void init_graphics(EFI_SYSTEM_TABLE *ST);
void put_pixel(uint32_t x, uint32_t y, uint32_t color);
void draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
void draw_string(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t bg);

// Print functions
void printc(char c);
void printk(uint32_t text_fg, uint32_t text_bg, const char *format, ...);

// Screen control functions
void SetCursorPos(uint32_t x, uint32_t y);
void SetColors(uint32_t fg, uint32_t bg);
void ClearScreen(uint32_t color);

// File descriptor functions
void init_fds();
void buf_write(Buffer *buf, const char *str);

#endif