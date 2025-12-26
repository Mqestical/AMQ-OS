#include "text_editor.h"
#include "print.h"
#include "string_helpers.h"
#include "vfs.h"

extern volatile uint8_t scancode_buffer[256];
extern volatile uint8_t scancode_read_pos;
extern volatile uint8_t scancode_write_pos;
extern int mouse_button_state;

void editor_save_cursor_area(text_editor_t* editor, int x, int y) {
    for (int dy = 0; dy < 16 && (y + dy) < fb.height; dy++) {
        for (int dx = 0; dx < 16 && (x + dx) < fb.width; dx++) {
            int px = x + dx;
            int py = y + dy;
            editor->saved_pixels[dy * 16 + dx] = fb.base[py * fb.width + px];
        }
    }
    editor->cursor_saved = 1;
    editor->saved_cx = x;
    editor->saved_cy = y;
}

void editor_restore_cursor_area(text_editor_t* editor) {
    if (!editor->cursor_saved) return;

    for (int dy = 0; dy < 16 && (editor->saved_cy + dy) < fb.height; dy++) {
        for (int dx = 0; dx < 16 && (editor->saved_cx + dx) < fb.width; dx++) {
            int px = editor->saved_cx + dx;
            int py = editor->saved_cy + dy;
            fb.base[py * fb.width + px] = editor->saved_pixels[dy * 16 + dx];
        }
    }
    editor->cursor_saved = 0;
}

void text_editor_init(text_editor_t* editor, const char* filepath) {
    editor->width = EDITOR_WIDTH;
    editor->height = EDITOR_HEIGHT;
    editor->x = (fb.width - editor->width) / 2;
    editor->y = (fb.height - editor->height) / 2;
    editor->visible = 1;
    editor->dragging = 0;
    editor->last_mx = -1;
    editor->last_my = -1;
    editor->cursor_saved = 0;
    
    // Initialize buffer
    editor->buffer.line_count = 1;
    editor->buffer.cursor_line = 0;
    editor->buffer.cursor_col = 0;
    editor->buffer.scroll_offset = 0;
    editor->buffer.modified = 0;
    
    for (int i = 0; i < EDITOR_MAX_LINES; i++) {
        editor->buffer.lines[i][0] = '\0';
    }
    
    if (filepath) {
        STRCPY(editor->buffer.filepath, filepath);
        text_editor_load(editor, filepath);
    } else {
        editor->buffer.filepath[0] = '\0';
    }
}

void text_editor_draw(text_editor_t* editor) {
    if (!editor->visible) return;
    
    uint32_t bg_color = 0x1E1E1E;
    uint32_t title_bg = 0x2D2D30;
    uint32_t toolbar_bg = 0x252526;
    uint32_t text_area_bg = 0x1E1E1E;
    uint32_t text_color = 0xD4D4D4;
    uint32_t line_num_bg = 0x1E1E1E;
    uint32_t line_num_color = 0x858585;
    uint32_t border_color = 0x3E3E42;
    
    // Draw main background
    for (int dy = 0; dy < editor->height; dy++) {
        for (int dx = 0; dx < editor->width; dx++) {
            put_pixel(editor->x + dx, editor->y + dy, bg_color);
        }
    }
    
    // Draw title bar
    for (int dy = 0; dy < EDITOR_TITLE_HEIGHT; dy++) {
        for (int dx = 0; dx < editor->width; dx++) {
            put_pixel(editor->x + dx, editor->y + dy, title_bg);
        }
    }
    
    // Draw title text "AMQ Editor"
    const char title[] = "AMQ Editor";
    int title_x = editor->x + 10;
    int title_y = editor->y + 10;
    for (int i = 0; title[i]; i++) {
        draw_char(title_x + i * 8, title_y, title[i], 0xFFFFFF, title_bg);
    }
    
    // Draw filename if available
    if (editor->buffer.filepath[0] != '\0') {
        int filename_x = editor->x + 120;
        draw_char(filename_x, title_y, '-', 0xAAAAAA, title_bg);
        filename_x += 16;
        for (int i = 0; editor->buffer.filepath[i] && i < 40; i++) {
            draw_char(filename_x + i * 8, title_y, editor->buffer.filepath[i], 0xAAAAAA, title_bg);
        }
    }
    
    // Draw modified indicator
    if (editor->buffer.modified) {
        draw_char(editor->x + editor->width - 20, title_y, '*', 0xFF6B6B, title_bg);
    }
    
    // Draw toolbar
    int toolbar_y = editor->y + EDITOR_TITLE_HEIGHT;
    for (int dy = 0; dy < EDITOR_TOOLBAR_HEIGHT; dy++) {
        for (int dx = 0; dx < editor->width; dx++) {
            put_pixel(editor->x + dx, toolbar_y + dy, toolbar_bg);
        }
    }
    
    // Draw Save button
    int save_btn_x = editor->x + 10;
    int save_btn_y = toolbar_y + 5;
    int btn_w = 60;
    int btn_h = 25;
    
    for (int dy = 0; dy < btn_h; dy++) {
        for (int dx = 0; dx < btn_w; dx++) {
            put_pixel(save_btn_x + dx, save_btn_y + dy, 0x0E639C);
        }
    }
    draw_char(save_btn_x + 18, save_btn_y + 8, 'S', 0xFFFFFF, 0x0E639C);
    draw_char(save_btn_x + 26, save_btn_y + 8, 'a', 0xFFFFFF, 0x0E639C);
    draw_char(save_btn_x + 34, save_btn_y + 8, 'v', 0xFFFFFF, 0x0E639C);
    draw_char(save_btn_x + 42, save_btn_y + 8, 'e', 0xFFFFFF, 0x0E639C);
    
    // Draw line numbers area background
    int line_num_width = 50;
    int text_area_y = toolbar_y + EDITOR_TOOLBAR_HEIGHT;
    int text_area_height = editor->height - EDITOR_TITLE_HEIGHT - EDITOR_TOOLBAR_HEIGHT;
    
    for (int dy = 0; dy < text_area_height; dy++) {
        for (int dx = 0; dx < line_num_width; dx++) {
            put_pixel(editor->x + dx, text_area_y + dy, line_num_bg);
        }
    }
    
    // Draw separator line
    for (int dy = 0; dy < text_area_height; dy++) {
        put_pixel(editor->x + line_num_width, text_area_y + dy, border_color);
    }
    
    // Draw line numbers and text
    int line_y = text_area_y + 5;
    int visible_lines = (text_area_height - 10) / 16;
    
    for (int i = 0; i < visible_lines && (editor->buffer.scroll_offset + i) < editor->buffer.line_count; i++) {
        int line_idx = editor->buffer.scroll_offset + i;
        
        // Draw line number
        char line_num[10];
        int num = line_idx + 1;
        int pos = 0;
        if (num == 0) {
            line_num[pos++] = '0';
        } else {
            char temp[10];
            int temp_pos = 0;
            while (num > 0) {
                temp[temp_pos++] = '0' + (num % 10);
                num /= 10;
            }
            for (int j = temp_pos - 1; j >= 0; j--) {
                line_num[pos++] = temp[j];
            }
        }
        line_num[pos] = '\0';
        
        int line_num_x = editor->x + line_num_width - (pos + 1) * 8;
        for (int j = 0; line_num[j]; j++) {
            draw_char(line_num_x + j * 8, line_y + i * 16, line_num[j], line_num_color, line_num_bg);
        }
        
        // Draw text content
        int text_x = editor->x + line_num_width + 5;
        for (int j = 0; editor->buffer.lines[line_idx][j] && j < EDITOR_VISIBLE_COLS; j++) {
            draw_char(text_x + j * 8, line_y + i * 16, editor->buffer.lines[line_idx][j], text_color, text_area_bg);
        }
        
        // Draw cursor if on this line
        if (line_idx == editor->buffer.cursor_line) {
            int cursor_x = text_x + editor->buffer.cursor_col * 8;
            int cursor_y = line_y + i * 16;
            for (int cy = 0; cy < 16; cy++) {
                put_pixel(cursor_x, cursor_y + cy, 0xFFFFFF);
                put_pixel(cursor_x + 1, cursor_y + cy, 0xFFFFFF);
            }
        }
    }
    
    // Draw border
    for (int dx = 0; dx < editor->width; dx++) {
        put_pixel(editor->x + dx, editor->y, border_color);
        put_pixel(editor->x + dx, editor->y + editor->height - 1, border_color);
    }
    for (int dy = 0; dy < editor->height; dy++) {
        put_pixel(editor->x, editor->y + dy, border_color);
        put_pixel(editor->x + editor->width - 1, editor->y + dy, border_color);
    }
}

void text_editor_insert_char(text_editor_t* editor, char ch) {
    editor_buffer_t* buf = &editor->buffer;
    
    if (buf->cursor_line >= EDITOR_MAX_LINES) return;
    
    int len = STRLEN(buf->lines[buf->cursor_line]);
    if (len >= EDITOR_MAX_LINE_LENGTH - 1) return;
    
    // Shift characters to make room
    for (int i = len; i >= buf->cursor_col; i--) {
        buf->lines[buf->cursor_line][i + 1] = buf->lines[buf->cursor_line][i];
    }
    
    buf->lines[buf->cursor_line][buf->cursor_col] = ch;
    buf->cursor_col++;
    buf->modified = 1;
}

void text_editor_backspace(text_editor_t* editor) {
    editor_buffer_t* buf = &editor->buffer;
    
    if (buf->cursor_col > 0) {
        int len = STRLEN(buf->lines[buf->cursor_line]);
        for (int i = buf->cursor_col - 1; i < len; i++) {
            buf->lines[buf->cursor_line][i] = buf->lines[buf->cursor_line][i + 1];
        }
        buf->cursor_col--;
        buf->modified = 1;
    } else if (buf->cursor_line > 0) {
        // Merge with previous line
        int prev_len = STRLEN(buf->lines[buf->cursor_line - 1]);
        int curr_len = STRLEN(buf->lines[buf->cursor_line]);
        
        if (prev_len + curr_len < EDITOR_MAX_LINE_LENGTH - 1) {
            STRCAT(buf->lines[buf->cursor_line - 1], buf->lines[buf->cursor_line]);
            
            // Shift lines up
            for (int i = buf->cursor_line; i < buf->line_count - 1; i++) {
                STRCPY(buf->lines[i], buf->lines[i + 1]);
            }
            buf->lines[buf->line_count - 1][0] = '\0';
            
            buf->line_count--;
            buf->cursor_line--;
            buf->cursor_col = prev_len;
            buf->modified = 1;
        }
    }
}

void text_editor_newline(text_editor_t* editor) {
    editor_buffer_t* buf = &editor->buffer;
    
    if (buf->line_count >= EDITOR_MAX_LINES) return;
    
    // Shift lines down
    for (int i = buf->line_count; i > buf->cursor_line; i--) {
        STRCPY(buf->lines[i], buf->lines[i - 1]);
    }
    
    // Split current line
    int len = STRLEN(buf->lines[buf->cursor_line]);
    for (int i = buf->cursor_col; i <= len; i++) {
        buf->lines[buf->cursor_line + 1][i - buf->cursor_col] = buf->lines[buf->cursor_line][i];
    }
    buf->lines[buf->cursor_line][buf->cursor_col] = '\0';
    
    buf->line_count++;
    buf->cursor_line++;
    buf->cursor_col = 0;
    buf->modified = 1;
    
    // Auto-scroll
    int visible_lines = (editor->height - EDITOR_TITLE_HEIGHT - EDITOR_TOOLBAR_HEIGHT - 10) / 16;
    if (buf->cursor_line >= buf->scroll_offset + visible_lines) {
        buf->scroll_offset++;
    }
}

void text_editor_move_cursor(text_editor_t* editor, int dx, int dy) {
    editor_buffer_t* buf = &editor->buffer;
    
    if (dy != 0) {
        int new_line = buf->cursor_line + dy;
        if (new_line >= 0 && new_line < buf->line_count) {
            buf->cursor_line = new_line;
            int line_len = STRLEN(buf->lines[buf->cursor_line]);
            if (buf->cursor_col > line_len) {
                buf->cursor_col = line_len;
            }
            
            // Auto-scroll
            int visible_lines = (editor->height - EDITOR_TITLE_HEIGHT - EDITOR_TOOLBAR_HEIGHT - 10) / 16;
            if (buf->cursor_line < buf->scroll_offset) {
                buf->scroll_offset = buf->cursor_line;
            } else if (buf->cursor_line >= buf->scroll_offset + visible_lines) {
                buf->scroll_offset = buf->cursor_line - visible_lines + 1;
            }
        }
    }
    
    if (dx != 0) {
        int new_col = buf->cursor_col + dx;
        int line_len = STRLEN(buf->lines[buf->cursor_line]);
        if (new_col >= 0 && new_col <= line_len) {
            buf->cursor_col = new_col;
        }
    }
}

void text_editor_save(text_editor_t* editor) {
    if (editor->buffer.filepath[0] == '\0') {
        PRINT(YELLOW, BLACK, "[EDITOR] No filepath specified\n");
        return;
    }
    
    int fd = vfs_open(editor->buffer.filepath, FILE_WRITE);
    if (fd < 0) {
        PRINT(RED, BLACK, "[EDITOR] Failed to open file for writing: %s\n", editor->buffer.filepath);
        return;
    }
    
    for (int i = 0; i < editor->buffer.line_count; i++) {
        int len = STRLEN(editor->buffer.lines[i]);
        vfs_write(fd, (uint8_t*)editor->buffer.lines[i], len);
        if (i < editor->buffer.line_count - 1) {
            vfs_write(fd, (uint8_t*)"\n", 1);
        }
    }
    
    vfs_close(fd);
    editor->buffer.modified = 0;
    
    PRINT(GREEN, BLACK, "[EDITOR] Saved to %s\n", editor->buffer.filepath);
}

void text_editor_load(text_editor_t* editor, const char* filepath) {
    int fd = vfs_open(filepath, FILE_READ);
    if (fd < 0) {
        PRINT(YELLOW, BLACK, "[EDITOR] File not found, creating new: %s\n", filepath);
        return;
    }
    
    char buffer[4096];
    uint32_t bytes_read = vfs_read(fd, (uint8_t*)buffer, sizeof(buffer) - 1);
    buffer[bytes_read] = '\0';
    
    vfs_close(fd);
    
    // Parse into lines
    editor->buffer.line_count = 0;
    int line_pos = 0;
    
    for (uint32_t i = 0; i < bytes_read && editor->buffer.line_count < EDITOR_MAX_LINES; i++) {
        if (buffer[i] == '\n') {
            editor->buffer.lines[editor->buffer.line_count][line_pos] = '\0';
            editor->buffer.line_count++;
            line_pos = 0;
        } else if (line_pos < EDITOR_MAX_LINE_LENGTH - 1) {
            editor->buffer.lines[editor->buffer.line_count][line_pos++] = buffer[i];
        }
    }
    
    if (line_pos > 0 || editor->buffer.line_count == 0) {
        editor->buffer.lines[editor->buffer.line_count][line_pos] = '\0';
        editor->buffer.line_count++;
    }
    
    editor->buffer.modified = 0;
    PRINT(GREEN, BLACK, "[EDITOR] Loaded %d lines from %s\n", editor->buffer.line_count, filepath);
}

void text_editor_handle_key(text_editor_t* editor, uint8_t scancode) {
    // Handle special keys first
    switch (scancode) {
        case 0x1C: // Enter
            text_editor_newline(editor);
            return;
        case 0x0E: // Backspace
            text_editor_backspace(editor);
            return;
        case 0x48: // Up arrow (extended)
            text_editor_move_cursor(editor, 0, -1);
            return;
        case 0x50: // Down arrow (extended)
            text_editor_move_cursor(editor, 0, 1);
            return;
        case 0x4B: // Left arrow (extended)
            text_editor_move_cursor(editor, -1, 0);
            return;
        case 0x4D: // Right arrow (extended)
            text_editor_move_cursor(editor, 1, 0);
            return;
    }
    
    // Handle character input
    char ch = 0;
    switch(scancode) {
        case 0x02: ch = '1'; break;
        case 0x03: ch = '2'; break;
        case 0x04: ch = '3'; break;
        case 0x05: ch = '4'; break;
        case 0x06: ch = '5'; break;
        case 0x07: ch = '6'; break;
        case 0x08: ch = '7'; break;
        case 0x09: ch = '8'; break;
        case 0x0A: ch = '9'; break;
        case 0x0B: ch = '0'; break;
        case 0x0C: ch = '-'; break;
        case 0x0D: ch = '='; break;
        case 0x10: ch = 'q'; break;
        case 0x11: ch = 'w'; break;
        case 0x12: ch = 'e'; break;
        case 0x13: ch = 'r'; break;
        case 0x14: ch = 't'; break;
        case 0x15: ch = 'y'; break;
        case 0x16: ch = 'u'; break;
        case 0x17: ch = 'i'; break;
        case 0x18: ch = 'o'; break;
        case 0x19: ch = 'p'; break;
        case 0x1E: ch = 'a'; break;
        case 0x1F: ch = 's'; break;
        case 0x20: ch = 'd'; break;
        case 0x21: ch = 'f'; break;
        case 0x22: ch = 'g'; break;
        case 0x23: ch = 'h'; break;
        case 0x24: ch = 'j'; break;
        case 0x25: ch = 'k'; break;
        case 0x26: ch = 'l'; break;
        case 0x27: ch = ';'; break;
        case 0x28: ch = '\''; break;
        case 0x2C: ch = 'z'; break;
        case 0x2D: ch = 'x'; break;
        case 0x2E: ch = 'c'; break;
        case 0x2F: ch = 'v'; break;
        case 0x30: ch = 'b'; break;
        case 0x31: ch = 'n'; break;
        case 0x32: ch = 'm'; break;
        case 0x33: ch = ','; break;
        case 0x34: ch = '.'; break;
        case 0x35: ch = '/'; break;
        case 0x39: ch = ' '; break;
        case 0x1A: ch = '['; break;
        case 0x1B: ch = ']'; break;
        case 0x2B: ch = '\\'; break;
        default: return;
    }
    
    if (ch) {
        text_editor_insert_char(editor, ch);
    }
}

int text_editor_run(text_editor_t* editor, desktop_state_t* desktop,
                    void (*update_mouse)(void),
                    void (*redraw_desktop_fn)(desktop_state_t*),
                    void (*save_cursor_fn)(text_editor_t*, int, int),
                    void (*restore_cursor_fn)(text_editor_t*),
                    void (*draw_cursor_fn)(int, int),
                    int (*get_cursor_x)(void),
                    int (*get_cursor_y)(void)) {
    int running = 1;
    int last_mouse_button = 0;
    int needs_redraw = 1;
    int was_extended = 0;
    
    while (running) {
        update_mouse();
        
        int mx = get_cursor_x();
        int my = get_cursor_y();
        int button = mouse_button_state & 0x01;
        
        // Handle dragging
        if (editor->dragging) {
            if (button) {
                int new_x = mx - editor->drag_offset_x;
                int new_y = my - editor->drag_offset_y;
                
                if (new_x < 0) new_x = 0;
                if (new_y < 0) new_y = 0;
                if (new_x + editor->width > fb.width) new_x = fb.width - editor->width;
                if (new_y + editor->height > fb.height) new_y = fb.height - editor->height;
                
                if (new_x != editor->x || new_y != editor->y) {
                    editor->x = new_x;
                    editor->y = new_y;
                    needs_redraw = 1;
                }
            } else {
                editor->dragging = 0;
            }
        }
        
        // Handle keyboard input
        while (scancode_read_pos != scancode_write_pos) {
            uint8_t scancode = scancode_buffer[scancode_read_pos++];
            
            if (scancode == 0xE0) {
                was_extended = 1;
                continue;
            }
            
            int is_release = (scancode & 0x80);
            uint8_t key = scancode & 0x7F;
            
            if (is_release) {
                was_extended = 0;
                continue;
            }
            
            // ESC to close
            if (key == 0x01) {
                running = 0;
                break;
            }
            
            // Handle extended keys (arrows)
            if (was_extended) {
                text_editor_handle_key(editor, key);
                was_extended = 0;
            } else {
                text_editor_handle_key(editor, scancode);
            }
            
            needs_redraw = 1;
        }
        
        // Redraw if needed
        if (needs_redraw || mx != editor->last_mx || my != editor->last_my) {
            restore_cursor_fn(editor);
            
            if (needs_redraw) {
                redraw_desktop_fn(desktop);
                text_editor_draw(editor);
                needs_redraw = 0;
            }
            
            save_cursor_fn(editor, mx, my);
            draw_cursor_fn(mx, my);
            editor->last_mx = mx;
            editor->last_my = my;
        }
        
        // Handle mouse clicks
        if (button && !last_mouse_button) {
            // Title bar drag
            if (mx >= editor->x && mx < editor->x + editor->width &&
                my >= editor->y && my < editor->y + EDITOR_TITLE_HEIGHT) {
                editor->dragging = 1;
                editor->drag_offset_x = mx - editor->x;
                editor->drag_offset_y = my - editor->y;
            }
            // Save button
            else {
                int toolbar_y = editor->y + EDITOR_TITLE_HEIGHT;
                int save_btn_x = editor->x + 10;
                int save_btn_y = toolbar_y + 5;
                
                if (mx >= save_btn_x && mx < save_btn_x + 60 &&
                    my >= save_btn_y && my < save_btn_y + 25) {
                    text_editor_save(editor);
                    needs_redraw = 1;
                }
            }
        }
        
        last_mouse_button = button;
        
        for (volatile int i = 0; i < 1000; i++);
    }
    
    return 0;
}