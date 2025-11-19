#include "print.h"
#include "FONT.h"

// Globals
Framebuffer fb;
Cursor cursor = {0, 0, 0xFFFFFFFF, 0xFF000000};
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
    cursor.fg_color = 0xFFFFFFFF;
    cursor.bg_color = 0xFF000000;
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

void printk(uint32_t text_fg, uint32_t text_bg, const char *format, ...) {
    // Save original colors
    uint32_t old_fg = cursor.fg_color;
    uint32_t old_bg = cursor.bg_color;
    
    // Set colors from arguments
    cursor.fg_color = text_fg;
    cursor.bg_color = text_bg;
    
    // Parse format string with va_args
    __builtin_va_list args;
    __builtin_va_start(args, format);
    
    char buffer[1024];
    int buf_pos = 0;
    
    const char *p = format;
    while (*p && buf_pos < 1023) {
        if (*p == '%') {
            p++;
            if (*p == 'd' || *p == 'i') {
                // Integer
                int val = __builtin_va_arg(args, int);
                int is_negative = val < 0;
                if (is_negative) val = -val;
                
                char num_buf[20];
                int num_pos = 0;
                
                if (val == 0) {
                    num_buf[num_pos++] = '0';
                } else {
                    while (val > 0) {
                        num_buf[num_pos++] = '0' + (val % 10);
                        val /= 10;
                    }
                }
                
                if (is_negative) num_buf[num_pos++] = '-';
                
                // Reverse into buffer
                for (int i = num_pos - 1; i >= 0 && buf_pos < 1023; i--) {
                    buffer[buf_pos++] = num_buf[i];
                }
            } else if (*p == 'u') {
                // Unsigned integer
                unsigned int val = __builtin_va_arg(args, unsigned int);
                
                char num_buf[20];
                int num_pos = 0;
                
                if (val == 0) {
                    num_buf[num_pos++] = '0';
                } else {
                    while (val > 0) {
                        num_buf[num_pos++] = '0' + (val % 10);
                        val /= 10;
                    }
                }
                
                for (int i = num_pos - 1; i >= 0 && buf_pos < 1023; i--) {
                    buffer[buf_pos++] = num_buf[i];
                }
            } else if (*p == 'x') {
                // Hexadecimal (lowercase)
                unsigned int val = __builtin_va_arg(args, unsigned int);
                
                char num_buf[20];
                int num_pos = 0;
                
                if (val == 0) {
                    num_buf[num_pos++] = '0';
                } else {
                    while (val > 0) {
                        int digit = val % 16;
                        num_buf[num_pos++] = digit < 10 ? '0' + digit : 'a' + digit - 10;
                        val /= 16;
                    }
                }
                
                for (int i = num_pos - 1; i >= 0 && buf_pos < 1023; i--) {
                    buffer[buf_pos++] = num_buf[i];
                }
            } else if (*p == 'X') {
                // Hexadecimal (uppercase)
                unsigned int val = __builtin_va_arg(args, unsigned int);
                
                char num_buf[20];
                int num_pos = 0;
                
                if (val == 0) {
                    num_buf[num_pos++] = '0';
                } else {
                    while (val > 0) {
                        int digit = val % 16;
                        num_buf[num_pos++] = digit < 10 ? '0' + digit : 'A' + digit - 10;
                        val /= 16;
                    }
                }
                
                for (int i = num_pos - 1; i >= 0 && buf_pos < 1023; i--) {
                    buffer[buf_pos++] = num_buf[i];
                }
            } else if (*p == 'l') {
                // Long specifier
                p++;
                if (*p == 'l') {
                    // long long
                    p++;
                    if (*p == 'u') {
                        // unsigned long long
                        unsigned long long val = __builtin_va_arg(args, unsigned long long);
                        
                        char num_buf[30];
                        int num_pos = 0;
                        
                        if (val == 0) {
                            num_buf[num_pos++] = '0';
                        } else {
                            while (val > 0) {
                                num_buf[num_pos++] = '0' + (val % 10);
                                val /= 10;
                            }
                        }
                        
                        for (int i = num_pos - 1; i >= 0 && buf_pos < 1023; i--) {
                            buffer[buf_pos++] = num_buf[i];
                        }
                    } else if (*p == 'x') {
                        // long long hex
                        unsigned long long val = __builtin_va_arg(args, unsigned long long);
                        
                        char num_buf[30];
                        int num_pos = 0;
                        
                        if (val == 0) {
                            num_buf[num_pos++] = '0';
                        } else {
                            while (val > 0) {
                                int digit = val % 16;
                                num_buf[num_pos++] = digit < 10 ? '0' + digit : 'a' + digit - 10;
                                val /= 16;
                            }
                        }
                        
                        for (int i = num_pos - 1; i >= 0 && buf_pos < 1023; i--) {
                            buffer[buf_pos++] = num_buf[i];
                        }
                    }
                }
            } else if (*p == 's') {
                // String
                const char *str = __builtin_va_arg(args, const char*);
                if (!str) str = "(null)";
                while (*str && buf_pos < 1023) {
                    buffer[buf_pos++] = *str++;
                }
            } else if (*p == 'c') {
                // Character
                char c = (char)__builtin_va_arg(args, int);
                buffer[buf_pos++] = c;
            } else if (*p == '%') {
                // Literal %
                buffer[buf_pos++] = '%';
            }
            p++;
        } else {
    if (*p == '\\') {  
        p++;
        if (*p == 'n') buffer[buf_pos++] = '\n';
        else if (*p == 'r') buffer[buf_pos++] = '\r';
        else if (*p == 't') buffer[buf_pos++] = '\t';
        else buffer[buf_pos++] = *p;
        p++;
    } else {
        buffer[buf_pos++] = *p++;
    }
}

    }
    
    buffer[buf_pos] = '\0';
    
    __builtin_va_end(args);
    
    // Print the buffer using printc
    for (int i = 0; i < buf_pos; i++) {
        printc(buffer[i]);
    }
    
    // Restore original colors
    cursor.fg_color = old_fg;
    cursor.bg_color = old_bg;
}

void buf_write(Buffer *buf, const char *str) {
    while (*str) {
        buf->data[buf->write_pos % 4096] = *str++;
        buf->write_pos++;
    }
}