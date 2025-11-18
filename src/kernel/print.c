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
        if (cursor.y >= fb.height) {
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
        if (cursor.x >= fb.width) {
            cursor.x = 0;
            cursor.y += 8;
            if (cursor.y >= fb.height) {
                cursor.y = 0;
            }
        }
        return;
    }
    
    // Bounds check before drawing
    if (cursor.x >= fb.width || cursor.y >= fb.height) {
        return;
    }
    
    if (cursor.x + 8 > fb.width) {
        cursor.x = 0;
        cursor.y += 8;
        if (cursor.y >= fb.height) {
            cursor.y = 0;
        }
    }
    
    if (cursor.y + 8 > fb.height) {
        cursor.y = 0;
    }
    
    // Now draw the character
    draw_char(cursor.x, cursor.y, c, cursor.fg_color, cursor.bg_color);
    
    // Advance cursor by 8 pixels
    cursor.x += 8;
}



void buf_write(Buffer *buf, const char *str) {
    while (*str) {
        buf->data[buf->write_pos % 4096] = *str++;
        buf->write_pos++;
    }
}