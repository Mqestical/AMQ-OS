#ifndef CREATE_DIALOG_H
#define CREATE_DIALOG_H

#include <stdint.h>
#include "desktop_icons.h"

// Forward declare taskbar_t to avoid circular dependency
typedef struct taskbar taskbar_t;

typedef struct {
    int x;
    int y;
    int width;
    int height;
    int visible;
    char buffer[256];
    int cursor_pos;
    const char* title;
    
    // Cursor save/restore
    uint32_t saved_pixels[16*16];
    int cursor_saved;
    int saved_cx;
    int saved_cy;
    
    // For tracking mouse
    int last_mx;
    int last_my;
    
    // For dragging
    int dragging;
    int drag_offset_x;
    int drag_offset_y;
} input_dialog_t;

void input_dialog_init(input_dialog_t* dialog, const char* title);
void input_dialog_draw(input_dialog_t* dialog);
int input_dialog_run(input_dialog_t* dialog, char* result, desktop_state_t* desktop,
                     void (*update_mouse)(void),
                     void (*redraw_desktop_fn)(desktop_state_t*),
                     void (*save_cursor_fn)(input_dialog_t*, int, int),
                     void (*restore_cursor_fn)(input_dialog_t*),
                     void (*draw_cursor_fn)(int, int),
                     int (*get_cursor_x)(void),
                     int (*get_cursor_y)(void));

void dialog_save_cursor_area(input_dialog_t* dialog, int x, int y);
void dialog_restore_cursor_area(input_dialog_t* dialog);

void handle_context_menu_action(context_menu_item_t action, desktop_state_t *desktop,
                                void (*update_mouse)(void),
                                void (*redraw_desktop_fn)(desktop_state_t*),
                                void (*save_cursor_fn)(input_dialog_t*, int, int),
                                void (*restore_cursor_fn)(input_dialog_t*),
                                void (*draw_cursor_fn)(int, int),
                                int (*get_cursor_x)(void),
                                int (*get_cursor_y)(void),
                                void (*restore_main_cursor)(void),
                                taskbar_t* taskbar);

#endif