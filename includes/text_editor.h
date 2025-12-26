#ifndef TEXT_EDITOR_H
#define TEXT_EDITOR_H

#include <stdint.h>
#include "desktop_icons.h"

#define EDITOR_WIDTH 700
#define EDITOR_HEIGHT 500
#define EDITOR_TITLE_HEIGHT 30
#define EDITOR_TOOLBAR_HEIGHT 35
#define EDITOR_MAX_LINES 1000
#define EDITOR_MAX_LINE_LENGTH 256
#define EDITOR_VISIBLE_LINES 25
#define EDITOR_VISIBLE_COLS 80

typedef struct {
    char lines[EDITOR_MAX_LINES][EDITOR_MAX_LINE_LENGTH];
    int line_count;
    int cursor_line;
    int cursor_col;
    int scroll_offset;
    char filepath[256];
    int modified;
} editor_buffer_t;

typedef struct {
    int x;
    int y;
    int width;
    int height;
    int visible;
    
    editor_buffer_t buffer;
    
    // UI state
    int dragging;
    int drag_offset_x;
    int drag_offset_y;
    int last_mx;
    int last_my;
    
    // Cursor rendering
    uint32_t saved_pixels[16*16];
    int cursor_saved;
    int saved_cx;
    int saved_cy;
} text_editor_t;

// Editor functions
void text_editor_init(text_editor_t* editor, const char* filepath);
void text_editor_draw(text_editor_t* editor);
void text_editor_handle_key(text_editor_t* editor, uint8_t scancode);
void text_editor_insert_char(text_editor_t* editor, char ch);
void text_editor_backspace(text_editor_t* editor);
void text_editor_delete(text_editor_t* editor);
void text_editor_newline(text_editor_t* editor);
void text_editor_move_cursor(text_editor_t* editor, int dx, int dy);
void text_editor_save(text_editor_t* editor);
void text_editor_load(text_editor_t* editor, const char* filepath);
int text_editor_run(text_editor_t* editor, desktop_state_t* desktop,
                    void (*update_mouse)(void),
                    void (*redraw_desktop_fn)(desktop_state_t*),
                    void (*save_cursor_fn)(text_editor_t*, int, int),
                    void (*restore_cursor_fn)(text_editor_t*),
                    void (*draw_cursor_fn)(int, int),
                    int (*get_cursor_x)(void),
                    int (*get_cursor_y)(void));

// Cursor management for editor
void editor_save_cursor_area(text_editor_t* editor, int x, int y);
void editor_restore_cursor_area(text_editor_t* editor);

#endif