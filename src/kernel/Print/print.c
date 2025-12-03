#include "print.h"
#include "FONT.h"
#include "string_helpers.h"
#include <stdarg.h>
// Globals
Framebuffer fb;
Cursor cursor = {0, 0, WHITE, RED};
EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;

Buffer stdin_buf;
Buffer stdout_buf;
Buffer stderr_buf;

FileDescriptor fd_table[256];
extern char font8x8_basic[128][8];

void init_fds() {
    fd_table[STDIN].type = 0;
    fd_table[STDIN].buffer = &stdin_buf;
    fd_table[STDOUT].type = 1;
    fd_table[STDOUT].buffer = &stdout_buf;
    fd_table[STDERR].type = 2;
    fd_table[STDERR].buffer = &stderr_buf;
}

void init_graphics(EFI_SYSTEM_TABLE *ST) {
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    uefi_call_wrapper(ST->BootServices->LocateProtocol, 3, &gop_guid, NULL, (VOID**)&gop);

    fb.base = (uint32_t*)gop->Mode->FrameBufferBase;
    fb.width = gop->Mode->Info->HorizontalResolution;
    fb.height = gop->Mode->Info->VerticalResolution;
    fb.pitch = gop->Mode->Info->PixelsPerScanLine;
    fb.bytes_per_pixel = 4;
    
    cursor.x = 0;
    cursor.y = 0;
    cursor.fg_color = WHITE;
    cursor.bg_color = RED;
}

void put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= fb.width || y >= fb.height) return;
    fb.base[y * fb.pitch + x] = color;
}

void draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg) {
    // Bounds check
    if (x >= fb.width || y >= fb.height) return;
    if (x + 8 > fb.width || y + 8 > fb.height) return;
    
    // Get glyph data
    unsigned char index = (unsigned char)c;
    if (index >= 128) index = 0;
    
    char *glyph = font8x8_basic[index];

    
    // Draw 8x8 character
    for (int row = 0; row < 8; row++) {
    unsigned char row_data = (unsigned char)glyph[row];
    for (int col = 0; col < 8; col++) {
        int bit = (row_data >> col) & 1;
        uint32_t color = bit ? fg : bg;
        put_pixel(x + col, y + row, color);
    }
}
}

void draw_string(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t bg) {
    uint32_t start_x = x;
    while (*s) {
        if (*s == '\n') {
            y += 8;
            x = start_x;
        } else if (*s == '\r') {
            x = start_x;
        } else if (*s == '\t') {
            x = ((x / 8) + 4) * 8;
        } else {
            draw_char(x, y, *s, fg, bg);
            x += 8;
        }
        s++;
        
        // Wrap to next line if needed
        if (x + 8 > fb.width) {
            x = start_x;
            y += 8;
        }
    }
}

void ClearScreen(uint32_t color) {
    for (uint32_t y = 0; y < fb.height; y++) {
        for (uint32_t x = 0; x < fb.width; x++) {
            put_pixel(x, y, color);
        }
    }
    cursor.x = 0;
    cursor.y = 0;
}

void SetCursorPos(uint32_t x, uint32_t y) {
    cursor.x = x;
    cursor.y = y;
}

void SetColors(uint32_t fg, uint32_t bg) {
    cursor.fg_color = fg;
    cursor.bg_color = bg;
}

void printc(char c) {
    // Handle special characters first
    if (c == '\n') {
        cursor.x = 0;
        cursor.y += 8;
        // Wrap to top if we go off screen
        if (cursor.y + 8 > fb.height) {
            cursor.y = 0;
        }
        return;
    }

    if (c == '\r') {
        cursor.x = 0;
        return;
    }

    if (c == '\t') {
        cursor.x = ((cursor.x / 8) + 4) * 8;
        // Check for line wrap
        if (cursor.x + 8 > fb.width) {
            cursor.x = 0;
            cursor.y += 8;
            if (cursor.y + 8 > fb.height) {
                cursor.y = 0;
            }
        }
        return;
    }

    // Check if character will fit on current line
    if (cursor.x + 8 > fb.width) {
        // Wrap to next line
        cursor.x = 0;
        cursor.y += 8;
        // Wrap to top if needed
        if (cursor.y + 8 > fb.height) {
            cursor.y = 0;
        }
    }

    // Bounds check - if we're at an invalid position, reset
    if (cursor.y + 8 > fb.height) {
        cursor.y = 0;
    }
    if (cursor.x + 8 > fb.width) {
        cursor.x = 0;
    }

    // Draw the character
    draw_char(cursor.x, cursor.y, c, cursor.fg_color, cursor.bg_color);

    // Advance cursor by 8 pixels (one character width)
    cursor.x += 8;
}

void printcs(char *str) {
    if (!str) return;
    for (size_t i = 0; str[i] != '\0'; i++) {
        printc(str[i]);  // reuse single-char function
    }
}


static void debug_print_hex(unsigned char val) {
    const char hex[] = "0123456789ABCDEF";
    printc(hex[val >> 4]);
    printc(hex[val & 0x0F]);
}
void print_unsigned(unsigned long long num, int base) {
    char buf[32];
    int i = 0;
    
    if (num == 0) {
        printc('0');
        return;
    }
    
    while (num > 0) {
        int digit = num % base;
        buf[i++] = (digit < 10) ? ('0' + digit) : ('a' + (digit - 10));
        num /= base;
    }
    
    // Print in reverse (most significant digit first)
    while (i > 0) {
        printc(buf[--i]);
    }
}

// Helper to print signed integer
void print_signed(long long num) {
    if (num < 0) {
        printc('-');
        num = -num;
    }
    print_unsigned((unsigned long long)num, 10);
}

// Helper to copy string literal to stack (works around .rodata issue)
static void safe_print_str(const char* str) {
    if (!str) {
        printc('(');
        printc('n');
        printc('u');
        printc('l');
        printc('l');
        printc(')');
        return;
    }
    
    // Copy to local buffer to ensure it's accessible
    char buf[256];
    int i = 0;
    
    // Copy string to stack
    while (str[i] && i < 255) {
        buf[i] = str[i];
        i++;
    }
    buf[i] = '\0';
    
    // Now print from stack
    for (int j = 0; j < i; j++) {
        printc(buf[j]);
    }
}

void printk(uint32_t text_fg, uint32_t text_bg, const char *format, ...) {
    if (!format) return;
    
    // Save and set colors
    uint32_t old_fg = cursor.fg_color;
    uint32_t old_bg = cursor.bg_color;
    cursor.fg_color = text_fg;
    cursor.bg_color = text_bg;
    
    // Copy format string to stack to avoid .rodata issues
    char fmt[512];
    int fmt_len = 0;
    while (format[fmt_len] && fmt_len < 511) {
        fmt[fmt_len] = format[fmt_len];
        fmt_len++;
    }
    fmt[fmt_len] = '\0';
    
    va_list args;
    va_start(args, format);
    
    int i = 0;
    while (i < fmt_len) {
        if (fmt[i] != '%') {
            printc(fmt[i]);
            i++;
            continue;
        }
        
        // Found '%'
        i++;
        if (i >= fmt_len) break;
        
        // Handle 'll' prefix for long long
        int is_longlong = 0;
        if (fmt[i] == 'l') {
            if (i + 1 < fmt_len && fmt[i + 1] == 'l') {
                is_longlong = 1;
                i += 2;
            } else {
                is_longlong = 1;
                i++;
            }
        }
        
        if (i >= fmt_len) break;
        
        switch (fmt[i]) {
            case 's': {
                const char *str = va_arg(args, const char*);
                safe_print_str(str);
                break;
            }
            
            case 'd': {
                long long val = is_longlong ? 
                    va_arg(args, long long) : 
                    (long long)va_arg(args, int);
                print_signed(val);
                break;
            }
            
            case 'u': {
                unsigned long long val = is_longlong ?
                    va_arg(args, unsigned long long) :
                    (unsigned long long)va_arg(args, unsigned int);
                print_unsigned(val, 10);
                break;
            }
            
            case 'x': {
                unsigned long long val = is_longlong ?
                    va_arg(args, unsigned long long) :
                    (unsigned long long)va_arg(args, unsigned int);
                print_unsigned(val, 16);
                break;
            }
            
            case 'X': {
                unsigned long long val = is_longlong ?
                    va_arg(args, unsigned long long) :
                    (unsigned long long)va_arg(args, unsigned int);
                // For uppercase, we'd need a modified print_unsigned
                // For now, just use lowercase
                print_unsigned(val, 16);
                break;
            }
            
            case 'p': {
                void *ptr = va_arg(args, void*);
                printc('0');
                printc('x');
                print_unsigned((unsigned long long)ptr, 16);
                break;
            }
            
            case 'c': {
                char ch = (char)va_arg(args, int);
                printc(ch);
                break;
            }
            
            case '%': {
                printc('%');
                break;
            }
            
            default: {
                printc('%');
                printc(fmt[i]);
                break;
            }
        }
        i++;
    }
    
    va_end(args);
    
    // Restore colors
    cursor.fg_color = old_fg;
    cursor.bg_color = old_bg;
}

// Helper macro to make usage easier with string literals
// Use this instead of direct PRINT calls:


void buf_write(Buffer *buf, const char *str) {
    while (*str) {
        buf->data[buf->write_pos % 4096] = *str++;
        buf->write_pos++;
    }
}

void test_PRINT(void) {
    ClearScreen(BLACK);
    SetCursorPos(0, 0);
    
    int num = 42;
    char* ptr = (char*)0xDEADBEEF;
    char* str = "Hello";
    
    PRINT(WHITE, BLACK, "Testing PRINT:\n");
    PRINT(WHITE, BLACK, "Integer: %d\n", num);
    PRINT(WHITE, BLACK, "Hex: 0x%x\n", num);
    PRINT(WHITE, BLACK, "Pointer: %p\n", ptr);
    PRINT(WHITE, BLACK, "String: %s\n", str);
    PRINT(WHITE, BLACK, "Char: %c\n", 'A');
    PRINT(WHITE, BLACK, "Multiple: %d %s %p\n", 123, "test", (void*)0x1234);
}