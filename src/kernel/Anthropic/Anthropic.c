#include "anthropic.h"
#include "print.h"
#include "string_helpers.h"
#include "vfs.h"
#include "memory.h"
#include "mouse.h"
#include "IO.h"

#define MAX_LINES 1000
#define MAX_LINE_LENGTH 200
#define EDITOR_BG 0x1E3A8A
#define EDITOR_TEXT 0xFFFFFF
#define LINE_NUM_COLOR 0x60A5FA
#define BUTTON_COLOR 0xEF4444
#define BUTTON_HOVER 0xDC2626
#define PROMPT_BG 0x374151
#define PROMPT_BORDER 0x60A5FA
#define INPUT_BG 0x1F2937

extern void process_keyboard_buffer(void);

extern volatile uint8_t scancode_buffer[256];
extern volatile uint8_t scancode_read_pos;
extern volatile uint8_t scancode_write_pos;

extern int mouse_button_state = 0;

typedef struct {
    char lines[MAX_LINES][MAX_LINE_LENGTH];
    int line_count;
    int cursor_line;
    int cursor_col;
    int scroll_offset;
    int modified;
    char filename[256];
    
    int x, y, width, height;
    int text_start_x, text_start_y;
    int line_num_width;
    int char_width, char_height;
    int close_x, close_y, close_size;
    int visible_lines;
    
    int needs_full_redraw;
    int last_mx, last_my;
    uint32_t saved_pixels[16*16];
    int cursor_saved;
    int saved_cx, saved_cy;
} editor_state_t;
static void editor_handle_key(editor_state_t* ed, uint8_t scancode, int shift_held);
// Static variables for mouse position
static int internal_cursor_x = 512;
static int internal_cursor_y = 384;
static int mouse_initialized = 0;

static void draw_simple_text(int x, int y, const char* text, uint32_t color) {
    // Draw simple 5x7 pixel letters
    for (int i = 0; text[i]; i++) {
        int cx = x + i * 6;
        // Draw a simple filled rectangle for each character (visible)
        for (int dy = 0; dy < 7; dy++) {
            for (int dx = 0; dx < 5; dx++) {
                put_pixel(cx + dx, y + dy, color);
            }
        }
    }
}


static void update_mouse_position_only(void) {
    static uint8_t packet[3];
    static uint8_t packet_index = 0;
    
    if (!mouse_initialized) {
        while (inb(0x64) & 2);
        outb(0x64, 0xD4);
        outb(0x60, 0xF4);
        
        while (!(inb(0x64) & 1));
        inb(0x60);
        
        mouse_initialized = 1;
        packet_index = 0;
        
        internal_cursor_x = fb.width / 2;
        internal_cursor_y = fb.height / 2;
    }
    
    // Just like your working mouse.c - simple and direct
    while (inb(0x64) & 1) {
        packet[packet_index++] = inb(0x60);
        
        if (packet_index == 3) {
            uint8_t b1 = packet[0];
            uint8_t b2 = packet[1];
            uint8_t b3 = packet[2];
            
            // Store button state
            mouse_button_state = b1 & 0x01;
            
            int delta_x = b2;
            int delta_y = b3;
            
            if (b1 & 0x10) delta_x |= 0xFFFFFF00;
            if (b1 & 0x20) delta_y |= 0xFFFFFF00;
            
            delta_y = -delta_y;
            
            internal_cursor_x += delta_x;
            internal_cursor_y += delta_y;
            
            if (internal_cursor_x < 0) internal_cursor_x = 0;
            if (internal_cursor_y < 0) internal_cursor_y = 0;
            if (internal_cursor_x >= fb.width - 16) internal_cursor_x = fb.width - 16;
            if (internal_cursor_y >= fb.height - 16) internal_cursor_y = fb.height - 16;
            
            packet_index = 0;
        }
    }
}

static void save_cursor_area(editor_state_t* ed, int x, int y) {
    for (int dy = 0; dy < 16 && (y + dy) < fb.height; dy++) {
        for (int dx = 0; dx < 16 && (x + dx) < fb.width; dx++) {
            int px = x + dx;
            int py = y + dy;
            ed->saved_pixels[dy * 16 + dx] = fb.base[py * fb.width + px];
        }
    }
    ed->cursor_saved = 1;
    ed->saved_cx = x;
    ed->saved_cy = y;
}

static void restore_cursor_area(editor_state_t* ed) {
    if (!ed->cursor_saved) return;
    
    for (int dy = 0; dy < 16 && (ed->saved_cy + dy) < fb.height; dy++) {
        for (int dx = 0; dx < 16 && (ed->saved_cx + dx) < fb.width; dx++) {
            int px = ed->saved_cx + dx;
            int py = ed->saved_cy + dy;
            fb.base[py * fb.width + px] = ed->saved_pixels[dy * 16 + dx];
        }
    }
    ed->cursor_saved = 0;
}

static void draw_custom_cursor(int x, int y) {
    uint32_t CURSOR_COLOR = 0xFF0000;
    int height = 16;
    
    for (int i = 0; i < height && (y + i) < fb.height && x < fb.width; i++) {
        put_pixel(x, y + i, CURSOR_COLOR);
    }
    
    for (int i = 0; i < height && (y + i) < fb.height && (x + i) < fb.width; i++) {
        put_pixel(x + i, y + i, CURSOR_COLOR);
    }
    
    for (int i = 0; i < height && (x + i) < fb.width && (y + height - 1) < fb.height; i++) {
        put_pixel(x + i, y + height - 1, CURSOR_COLOR);
    }
    
    for (int yy = 1; yy < height && (y + yy) < fb.height; yy++) {
        for (int xx = 1; xx < yy && (x + xx) < fb.width; xx++) {
            put_pixel(x + xx, y + yy, CURSOR_COLOR);
        }
    }
}

static void draw_rect(int x, int y, int w, int h, uint32_t color) {
    for (int dy = 0; dy < h; dy++) {
        for (int dx = 0; dx < w; dx++) {
            put_pixel(x + dx, y + dy, color);
        }
    }
}

static void draw_rect_outline(int x, int y, int w, int h, uint32_t color, int thickness) {
    for (int t = 0; t < thickness; t++) {
        for (int dx = 0; dx < w; dx++) {
            put_pixel(x + dx, y + t, color);
            put_pixel(x + dx, y + h - 1 - t, color);
        }
        for (int dy = 0; dy < h; dy++) {
            put_pixel(x + t, y + dy, color);
            put_pixel(x + w - 1 - t, y + dy, color);
        }
    }
}

static void draw_text(int x, int y, const char* text, uint32_t color, uint32_t bg_color) {
    int px = x;
    for (int i = 0; text[i]; i++) {
        draw_char(px, y, text[i], color, bg_color);
        px += 8;
    }
}

static void draw_number(int x, int y, int num, uint32_t color, uint32_t bg_color) {
    char buf[16];
    int i = 0;
    
    if (num == 0) {
        buf[i++] = '0';
    } else {
        int temp = num;
        int digits = 0;
        while (temp > 0) {
            digits++;
            temp /= 10;
        }
        for (int d = digits - 1; d >= 0; d--) {
            buf[d] = '0' + (num % 10);
            num /= 10;
        }
        i = digits;
    }
    buf[i] = '\0';
    draw_text(x, y, buf, color, bg_color);
}

static void editor_init(editor_state_t* ed, const char* filename) {
    ed->needs_full_redraw = 1;
    ed->last_mx = -1;
    ed->last_my = -1;
    ed->cursor_saved = 0;
    
    for (int i = 0; i < MAX_LINES; i++) {
        ed->lines[i][0] = '\0';
    }
    
    ed->line_count = 1;
    ed->cursor_line = 0;
    ed->cursor_col = 0;
    ed->scroll_offset = 0;
    ed->modified = 0;
    STRCPY(ed->filename, filename);
    
    ed->width = fb.width;
    ed->height = fb.height;
    ed->x = 0;
    ed->y = 0;
    
    ed->char_width = 8;
    ed->char_height = 8;
    ed->line_num_width = 50;
    ed->text_start_x = ed->x + ed->line_num_width + 10;
    ed->text_start_y = ed->y + 40;
    
    ed->close_size = 24;
    ed->close_x = ed->x + ed->width - ed->close_size - 5;
    ed->close_y = ed->y + 5;
    
    ed->visible_lines = (ed->height - 50) / ed->char_height;
}

static int editor_load_file(editor_state_t* ed) {
    char fullpath[256];
    if (ed->filename[0] == '/') {
        STRCPY(fullpath, ed->filename);
    } else {
        const char* cwd = vfs_get_cwd_path();
        STRCPY(fullpath, cwd);
        int len = STRLEN(fullpath);
        if (len > 0 && fullpath[len-1] != '/') {
            fullpath[len] = '/';
            fullpath[len+1] = '\0';
        }
        STRCAT(fullpath, ed->filename);
    }
    
    int fd = vfs_open(fullpath, FILE_READ);
    if (fd < 0) {
        ed->lines[0][0] = '\0';
        ed->line_count = 1;
        return 0;
    }
    
    uint8_t buffer[MAX_LINES * MAX_LINE_LENGTH];
    int bytes = vfs_read(fd, buffer, sizeof(buffer) - 1);
    vfs_close(fd);
    
    if (bytes <= 0) {
        ed->lines[0][0] = '\0';
        ed->line_count = 1;
        return 0;
    }
    
    buffer[bytes] = '\0';
    
    int line = 0;
    int col = 0;
    for (int i = 0; i < bytes && line < MAX_LINES; i++) {
        if (buffer[i] == '\n') {
            ed->lines[line][col] = '\0';
            line++;
            col = 0;
        } else if (buffer[i] == '\r') {
            continue;
        } else if (col < MAX_LINE_LENGTH - 1) {
            ed->lines[line][col++] = buffer[i];
        }
    }
    
    if (col > 0 || line == 0) {
        ed->lines[line][col] = '\0';
        line++;
    }
    
    ed->line_count = line;
    if (ed->line_count == 0) ed->line_count = 1;
    
    return 1;
}

static int editor_save_file(editor_state_t* ed) {
    char fullpath[256];
    if (ed->filename[0] == '/') {
        STRCPY(fullpath, ed->filename);
    } else {
        const char* cwd = vfs_get_cwd_path();
        STRCPY(fullpath, cwd);
        int len = STRLEN(fullpath);
        if (len > 0 && fullpath[len-1] != '/') {
            fullpath[len] = '/';
            fullpath[len+1] = '\0';
        }
        STRCAT(fullpath, ed->filename);
    }
    
    int fd = vfs_open(fullpath, FILE_WRITE);
    if (fd < 0) {
        vfs_create(fullpath, FILE_READ | FILE_WRITE);
        fd = vfs_open(fullpath, FILE_WRITE);
    }
    
    if (fd < 0) return -1;
    
    for (int i = 0; i < ed->line_count; i++) {
        int len = STRLEN(ed->lines[i]);
        if (len > 0) {
            vfs_write(fd, (uint8_t*)ed->lines[i], len);
        }
        if (i < ed->line_count - 1) {
            vfs_write(fd, (uint8_t*)"\n", 1);
        }
    }
    
    vfs_close(fd);
    ed->modified = 0;
    return 0;
}

static void editor_draw(editor_state_t* ed) {
    draw_rect(ed->x, ed->y, ed->width, 30, 0x1E40AF);
    
    char title[280];
    STRCPY(title, "Anthropic Editor - ");
    STRCAT(title, ed->filename);
    if (ed->modified) {
        int title_len = STRLEN(title);
        title[title_len] = ' ';
        title[title_len + 1] = '*';
        title[title_len + 2] = '\0';
    }
    draw_text(ed->x + 10, ed->y + 11, title, EDITOR_TEXT, 0x1E40AF);
    
    int mx = internal_cursor_x;
    int my = internal_cursor_y;
    int hover = (mx >= ed->close_x && mx < ed->close_x + ed->close_size &&
                 my >= ed->close_y && my < ed->close_y + ed->close_size);
    
    draw_rect(ed->close_x, ed->close_y, ed->close_size, ed->close_size, 
              hover ? BUTTON_HOVER : BUTTON_COLOR);
    
    for (int i = 0; i < ed->close_size; i++) {
        put_pixel(ed->close_x + i, ed->close_y + i, EDITOR_TEXT);
        put_pixel(ed->close_x + i, ed->close_y + ed->close_size - 1 - i, EDITOR_TEXT);
    }
    
    draw_rect(ed->x, ed->y + 30, ed->line_num_width, ed->height - 30, 0x1E3A8A - 0x0A0A0A);
    draw_rect(ed->x + ed->line_num_width, ed->y + 30, 2, ed->height - 30, LINE_NUM_COLOR);
    
    draw_rect(ed->text_start_x, ed->text_start_y, 
              ed->width - ed->line_num_width - 20, 
              ed->visible_lines * ed->char_height + 5, EDITOR_BG);
    
    draw_text(ed->x + 10, ed->y + ed->height - 20, "Ctrl+S: Save | Click X to close", 
              LINE_NUM_COLOR, EDITOR_BG);
    
    int first_line = ed->scroll_offset;
    int last_line = first_line + ed->visible_lines;
    if (last_line > ed->line_count) last_line = ed->line_count;
    
    for (int i = first_line; i < last_line; i++) {
        int screen_y = ed->text_start_y + (i - first_line) * ed->char_height;
        
        draw_number(ed->x + 10, screen_y, i + 1, LINE_NUM_COLOR, 0x1E3A8A - 0x0A0A0A);
        draw_text(ed->text_start_x, screen_y, ed->lines[i], EDITOR_TEXT, EDITOR_BG);
        
        if (i == ed->cursor_line) {
            int cursor_x = ed->text_start_x + ed->cursor_col * ed->char_width;
            draw_rect(cursor_x, screen_y, 2, ed->char_height, EDITOR_TEXT);
        }
    }
}static int editor_show_save_prompt(editor_state_t* ed) {
    int prompt_w = 400;
    int prompt_h = 200;
    int prompt_x = ed->x + (ed->width - prompt_w) / 2;
    int prompt_y = ed->y + (ed->height - prompt_h) / 2;
    
    draw_rect(prompt_x, prompt_y, prompt_w, prompt_h, PROMPT_BG);
    draw_rect_outline(prompt_x, prompt_y, prompt_w, prompt_h, PROMPT_BORDER, 3);
    
    // Draw text EXACTLY like the buttons
    int text_x = prompt_x + 130;
    int text_y = prompt_y + 60;
    
    draw_char(text_x, text_y, 'S', YELLOW, PROMPT_BG);
    draw_char(text_x + 8, text_y, 'A', YELLOW, PROMPT_BG);
    draw_char(text_x + 16, text_y, 'V', YELLOW, PROMPT_BG);
    draw_char(text_x + 24, text_y, 'E', YELLOW, PROMPT_BG);
    draw_char(text_x + 32, text_y, ' ', YELLOW, PROMPT_BG);
    draw_char(text_x + 40, text_y, 'C', YELLOW, PROMPT_BG);
    draw_char(text_x + 48, text_y, 'H', YELLOW, PROMPT_BG);
    draw_char(text_x + 56, text_y, 'A', YELLOW, PROMPT_BG);
    draw_char(text_x + 64, text_y, 'N', YELLOW, PROMPT_BG);
    draw_char(text_x + 72, text_y, 'G', YELLOW, PROMPT_BG);
    draw_char(text_x + 80, text_y, 'E', YELLOW, PROMPT_BG);
    draw_char(text_x + 88, text_y, 'S', YELLOW, PROMPT_BG);
    draw_char(text_x + 96, text_y, '?', YELLOW, PROMPT_BG);
    
    int button_w = 120;
    int button_h = 40;
    int button_y = prompt_y + 120;
    int yes_x = prompt_x + 50;
    int no_x = prompt_x + 230;
    
    int last_button = 0;
    int result = -1;
    
    int saved_mx = ed->last_mx;
    int saved_my = ed->last_my;
    
    draw_rect(yes_x, button_y, button_w, button_h, 0x10B981);
    draw_rect_outline(yes_x, button_y, button_w, button_h, 0xFFFFFF, 2);
    draw_char(yes_x + 42, button_y + 16, 'Y', GREEN, 0x10B981);
    draw_char(yes_x + 50, button_y + 16, 'E', GREEN, 0x10B981);
    draw_char(yes_x + 58, button_y + 16, 'S', GREEN, 0x10B981);
    
    draw_rect(no_x, button_y, button_w, button_h, 0xDC2626);
    draw_rect_outline(no_x, button_y, button_w, button_h, 0xFFFFFF, 2);
    draw_char(no_x + 50, button_y + 16, 'N', RED, 0xDC2626);
    draw_char(no_x + 58, button_y + 16, 'O', RED, 0xDC2626);
    
    while (result == -1) {
        update_mouse_position_only();
        int mx = internal_cursor_x;
        int my = internal_cursor_y;
        int button = get_mouse_button();
        
        if (mx != ed->last_mx || my != ed->last_my) {
            int yes_hover = (mx >= yes_x && mx < yes_x + button_w &&
                           my >= button_y && my < button_y + button_h);
            int no_hover = (mx >= no_x && mx < no_x + button_w &&
                          my >= button_y && my < button_y + button_h);
            
            uint32_t yes_color = yes_hover ? 0x34D399 : 0x10B981;
            draw_rect(yes_x, button_y, button_w, button_h, yes_color);
            draw_rect_outline(yes_x, button_y, button_w, button_h, 0xFFFFFF, 2);
            draw_char(yes_x + 42, button_y + 16, 'Y', 0xFFFFFF, yes_color);
            draw_char(yes_x + 50, button_y + 16, 'E', 0xFFFFFF, yes_color);
            draw_char(yes_x + 58, button_y + 16, 'S', 0xFFFFFF, yes_color);
            
            uint32_t no_color = no_hover ? 0xF87171 : 0xDC2626;
            draw_rect(no_x, button_y, button_w, button_h, no_color);
            draw_rect_outline(no_x, button_y, button_w, button_h, 0xFFFFFF, 2);
            draw_char(no_x + 50, button_y + 16, 'N', 0xFFFFFF, no_color);
            draw_char(no_x + 58, button_y + 16, 'O', 0xFFFFFF, no_color);
            
            restore_cursor_area(ed);
            save_cursor_area(ed, mx, my);
            draw_custom_cursor(mx, my);
            
            ed->last_mx = mx;
            ed->last_my = my;
        }
        
        if (button && !last_button) {
            if (mx >= yes_x && mx < yes_x + button_w &&
                my >= button_y && my < button_y + button_h) {
                result = 1;
            }
            else if (mx >= no_x && mx < no_x + button_w &&
                     my >= button_y && my < button_y + button_h) {
                result = 0;
            }
        }
        
        last_button = button;
        for (volatile int i = 0; i < 10000; i++);
    }
    
    ed->last_mx = saved_mx;
    ed->last_my = saved_my;
    
    return result;
}
static void editor_handle_key(editor_state_t* ed, uint8_t scancode, int shift_held) {
    if (scancode & 0x80) return;
    
    if (scancode == 0x1C) {
        if (ed->line_count >= MAX_LINES) return;
        
        char remaining[MAX_LINE_LENGTH];
        STRCPY(remaining, &ed->lines[ed->cursor_line][ed->cursor_col]);
        ed->lines[ed->cursor_line][ed->cursor_col] = '\0';
        
        for (int i = ed->line_count; i > ed->cursor_line + 1; i--) {
            STRCPY(ed->lines[i], ed->lines[i-1]);
        }
        
        STRCPY(ed->lines[ed->cursor_line + 1], remaining);
        ed->line_count++;
        ed->cursor_line++;
        ed->cursor_col = 0;
        ed->modified = 1;
        ed->needs_full_redraw = 1;
        
        if (ed->cursor_line >= ed->scroll_offset + ed->visible_lines) {
            ed->scroll_offset++;
        }
        return;
    }
    
    if (scancode == 0x0E) {
        if (ed->cursor_col > 0) {
            int len = STRLEN(ed->lines[ed->cursor_line]);
            for (int i = ed->cursor_col - 1; i < len; i++) {
                ed->lines[ed->cursor_line][i] = ed->lines[ed->cursor_line][i + 1];
            }
            ed->cursor_col--;
            ed->modified = 1;
            ed->needs_full_redraw = 1;
        } else if (ed->cursor_line > 0) {
            int prev_len = STRLEN(ed->lines[ed->cursor_line - 1]);
            if (prev_len + STRLEN(ed->lines[ed->cursor_line]) < MAX_LINE_LENGTH - 1) {
                STRCAT(ed->lines[ed->cursor_line - 1], ed->lines[ed->cursor_line]);
                
                for (int i = ed->cursor_line; i < ed->line_count - 1; i++) {
                    STRCPY(ed->lines[i], ed->lines[i + 1]);
                }
                ed->line_count--;
                ed->cursor_line--;
                ed->cursor_col = prev_len;
                ed->modified = 1;
                ed->needs_full_redraw = 1;
                
                if (ed->cursor_line < ed->scroll_offset) {
                    ed->scroll_offset--;
                }
            }
        }
        return;
    }
    
    if (scancode == 0x48) {
        if (ed->cursor_line > 0) {
            ed->cursor_line--;
            int len = STRLEN(ed->lines[ed->cursor_line]);
            if (ed->cursor_col > len) ed->cursor_col = len;
            if (ed->cursor_line < ed->scroll_offset) {
                ed->scroll_offset--;
            }
        }
        return;
    }
    
    if (scancode == 0x50) {
        if (ed->cursor_line < ed->line_count - 1) {
            ed->cursor_line++;
            int len = STRLEN(ed->lines[ed->cursor_line]);
            if (ed->cursor_col > len) ed->cursor_col = len;
            if (ed->cursor_line >= ed->scroll_offset + ed->visible_lines) {
                ed->scroll_offset++;
            }
        }
        return;
    }
    
    if (scancode == 0x4B) {
        if (ed->cursor_col > 0) {
            ed->cursor_col--;
        } else if (ed->cursor_line > 0) {
            ed->cursor_line--;
            ed->cursor_col = STRLEN(ed->lines[ed->cursor_line]);
            if (ed->cursor_line < ed->scroll_offset) {
                ed->scroll_offset--;
            }
        }
        return;
    }
    
    if (scancode == 0x4D) {
        int len = STRLEN(ed->lines[ed->cursor_line]);
        if (ed->cursor_col < len) {
            ed->cursor_col++;
        } else if (ed->cursor_line < ed->line_count - 1) {
            ed->cursor_line++;
            ed->cursor_col = 0;
            if (ed->cursor_line >= ed->scroll_offset + ed->visible_lines) {
                ed->scroll_offset++;
            }
        }
        return;
    }
    
    char ch = 0;
    switch (scancode) {
        // Numbers with shift
        case 0x02: ch = shift_held ? '!' : '1'; break;
        case 0x03: ch = shift_held ? '@' : '2'; break;
        case 0x04: ch = shift_held ? '#' : '3'; break;
        case 0x05: ch = shift_held ? '$' : '4'; break;
        case 0x06: ch = shift_held ? '%' : '5'; break;
        case 0x07: ch = shift_held ? '^' : '6'; break;
        case 0x08: ch = shift_held ? '&' : '7'; break;
        case 0x09: ch = shift_held ? '*' : '8'; break;
        case 0x0A: ch = shift_held ? '(' : '9'; break;
        case 0x0B: ch = shift_held ? ')' : '0'; break;
        case 0x0C: ch = shift_held ? '_' : '-'; break;
        case 0x0D: ch = shift_held ? '+' : '='; break;
        
        // Letters (uppercase with shift)
        case 0x10: ch = shift_held ? 'Q' : 'q'; break;
        case 0x11: ch = shift_held ? 'W' : 'w'; break;
        case 0x12: ch = shift_held ? 'E' : 'e'; break;
        case 0x13: ch = shift_held ? 'R' : 'r'; break;
        case 0x14: ch = shift_held ? 'T' : 't'; break;
        case 0x15: ch = shift_held ? 'Y' : 'y'; break;
        case 0x16: ch = shift_held ? 'U' : 'u'; break;
        case 0x17: ch = shift_held ? 'I' : 'i'; break;
        case 0x18: ch = shift_held ? 'O' : 'o'; break;
        case 0x19: ch = shift_held ? 'P' : 'p'; break;
        case 0x1E: ch = shift_held ? 'A' : 'a'; break;
        case 0x1F: ch = shift_held ? 'S' : 's'; break;
        case 0x20: ch = shift_held ? 'D' : 'd'; break;
        case 0x21: ch = shift_held ? 'F' : 'f'; break;
        case 0x22: ch = shift_held ? 'G' : 'g'; break;
        case 0x23: ch = shift_held ? 'H' : 'h'; break;
        case 0x24: ch = shift_held ? 'J' : 'j'; break;
        case 0x25: ch = shift_held ? 'K' : 'k'; break;
        case 0x26: ch = shift_held ? 'L' : 'l'; break;
        case 0x2C: ch = shift_held ? 'Z' : 'z'; break;
        case 0x2D: ch = shift_held ? 'X' : 'x'; break;
        case 0x2E: ch = shift_held ? 'C' : 'c'; break;
        case 0x2F: ch = shift_held ? 'V' : 'v'; break;
        case 0x30: ch = shift_held ? 'B' : 'b'; break;
        case 0x31: ch = shift_held ? 'N' : 'n'; break;
        case 0x32: ch = shift_held ? 'M' : 'm'; break;
        
        // Punctuation with shift
        case 0x39: ch = ' '; break;
        case 0x33: ch = shift_held ? '<' : ','; break;
        case 0x34: ch = shift_held ? '>' : '.'; break;
        case 0x35: ch = shift_held ? '?' : '/'; break;
        case 0x27: ch = shift_held ? ':' : ';'; break;
        case 0x28: ch = shift_held ? '"' : '\''; break;
        case 0x1A: ch = shift_held ? '{' : '['; break;
        case 0x1B: ch = shift_held ? '}' : ']'; break;
        case 0x2B: ch = shift_held ? '|' : '\\'; break;
        case 0x29: ch = shift_held ? '~' : '`'; break;
        
        default: return;
    }
    
    if (ch) {
        int len = STRLEN(ed->lines[ed->cursor_line]);
        if (len < MAX_LINE_LENGTH - 1) {
            for (int i = len; i > ed->cursor_col; i--) {
                ed->lines[ed->cursor_line][i] = ed->lines[ed->cursor_line][i - 1];
            }
            ed->lines[ed->cursor_line][ed->cursor_col] = ch;
            ed->lines[ed->cursor_line][len + 1] = '\0';
            ed->cursor_col++;
            ed->modified = 1;
            ed->needs_full_redraw = 1;
        }
    }
}void anthropic_editor(const char* filename) {
    editor_state_t* ed = (editor_state_t*)kmalloc(sizeof(editor_state_t));
    if (!ed) {
        PRINT(YELLOW, BLACK, "Out of memory\n");
        return;
    }
    
    editor_init(ed, filename);
    editor_load_file(ed);
    
    int running = 1;
    int last_mouse_button = 0;
    
    ClearScreen(BLACK);
    editor_draw(ed);
    
    int mx = internal_cursor_x;
    int my = internal_cursor_y;
    save_cursor_area(ed, mx, my);
    draw_custom_cursor(mx, my);
    ed->last_mx = mx;
    ed->last_my = my;
    
    int ctrl_held = 0;
    int shift_held = 0;
    
    while (running) {
        // Update mouse
        update_mouse_position_only();
        
        mx = internal_cursor_x;
        my = internal_cursor_y;
        int button = get_mouse_button();
        
        if (mx != ed->last_mx || my != ed->last_my) {
            restore_cursor_area(ed);
            
            int old_hover = (ed->last_mx >= ed->close_x && ed->last_mx < ed->close_x + ed->close_size &&
                           ed->last_my >= ed->close_y && ed->last_my < ed->close_y + ed->close_size);
            int new_hover = (mx >= ed->close_x && mx < ed->close_x + ed->close_size &&
                           my >= ed->close_y && my < ed->close_y + ed->close_size);
            
            if (old_hover != new_hover) {
                ed->needs_full_redraw = 1;
            }
            
            save_cursor_area(ed, mx, my);
            draw_custom_cursor(mx, my);
            
            ed->last_mx = mx;
            ed->last_my = my;
        }
        
        // Process keyboard - DIRECTLY from buffer
        int had_input = 0;
        
        while (scancode_read_pos != scancode_write_pos) {
            uint8_t scancode = scancode_buffer[scancode_read_pos++];
            
            if (scancode == 0xE0) continue;
            
            int is_release = (scancode & 0x80);
            uint8_t key = scancode & 0x7F;
            
            if (key == 0x2A || key == 0x36) {
                shift_held = !is_release;
                continue;
            }
            
            if (key == 0x1D) {
                ctrl_held = !is_release;
                continue;
            }
            
            if (is_release) continue;
            
            if (key == 0x1F && ctrl_held) {
                if (editor_save_file(ed) == 0) {
                    restore_cursor_area(ed);
                    editor_draw(ed);
                    draw_text(ed->x + ed->width - 150, ed->y + ed->height - 20, 
                              "Saved!", 0x00FF00, EDITOR_BG);
                    save_cursor_area(ed, mx, my);
                    draw_custom_cursor(mx, my);
                    for (volatile int i = 0; i < 10000000; i++);
                    ed->needs_full_redraw = 1;
                }
                continue;
            }
            
            editor_handle_key(ed, scancode, shift_held);
            had_input = 1;
        }
        
        if (had_input) {
            ed->needs_full_redraw = 1;
        }
        
        if (ed->needs_full_redraw) {
            restore_cursor_area(ed);
            editor_draw(ed);
            save_cursor_area(ed, mx, my);
            draw_custom_cursor(mx, my);
            ed->needs_full_redraw = 0;
        }
        
        if (button && !last_mouse_button) {
            if (mx >= ed->close_x && mx < ed->close_x + ed->close_size &&
                my >= ed->close_y && my < ed->close_y + ed->close_size) {
                
                if (ed->modified) {
                    restore_cursor_area(ed);
                    int should_save = editor_show_save_prompt(ed);
                    if (should_save) {
                        editor_save_file(ed);
                    }
                }
                running = 0;
            }
        }
        last_mouse_button = button;
        
        for (volatile int i = 0; i < 1000; i++);
    }
    
    restore_cursor_area(ed);
    kfree(ed);
    ClearScreen(BLACK);
    SetCursorPos(0, 0);
    
    mouse_initialized = 0;
    mouse_button_state = 0;
}