#include "create_dialog.h"
#include "print.h"
#include "desktop_icons.h"
#include "string_helpers.h"
#include "vfs.h"
#include "taskbar.h"

extern volatile uint8_t scancode_buffer[256];
extern volatile uint8_t scancode_read_pos;
extern volatile uint8_t scancode_write_pos;
extern int mouse_button_state;

void dialog_save_cursor_area(input_dialog_t* dialog, int x, int y) {
    for (int dy = 0; dy < 16 && (y + dy) < fb.height; dy++) {
        for (int dx = 0; dx < 16 && (x + dx) < fb.width; dx++) {
            int px = x + dx;
            int py = y + dy;
            dialog->saved_pixels[dy * 16 + dx] = fb.base[py * fb.width + px];
        }
    }
    dialog->cursor_saved = 1;
    dialog->saved_cx = x;
    dialog->saved_cy = y;
}

void dialog_restore_cursor_area(input_dialog_t* dialog) {
    if (!dialog->cursor_saved) return;

    for (int dy = 0; dy < 16 && (dialog->saved_cy + dy) < fb.height; dy++) {
        for (int dx = 0; dx < 16 && (dialog->saved_cx + dx) < fb.width; dx++) {
            int px = dialog->saved_cx + dx;
            int py = dialog->saved_cy + dy;
            fb.base[py * fb.width + px] = dialog->saved_pixels[dy * 16 + dx];
        }
    }
    dialog->cursor_saved = 0;
}

static void input_dialog_handle_key(input_dialog_t* dialog, uint8_t scancode) {
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
        case 0x0D: ch = '_'; break;
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
        case 0x2C: ch = 'z'; break;
        case 0x2D: ch = 'x'; break;
        case 0x2E: ch = 'c'; break;
        case 0x2F: ch = 'v'; break;
        case 0x30: ch = 'b'; break;
        case 0x31: ch = 'n'; break;
        case 0x32: ch = 'm'; break;
        case 0x39: ch = ' '; break;
        case 0x34: ch = '.'; break;
        case 0x35: ch = '/'; break;
        default: return;
    }

    if (ch) {
        int len = STRLEN(dialog->buffer);
        if (len < 255) {
            dialog->buffer[dialog->cursor_pos] = ch;
            dialog->buffer[dialog->cursor_pos + 1] = '\0';
            dialog->cursor_pos++;
        }
    }
}

void input_dialog_init(input_dialog_t* dialog, const char* title) {
    dialog->width = 500;
    dialog->height = 150;
    dialog->x = (fb.width - dialog->width) / 2;
    dialog->y = (fb.height - dialog->height) / 2;
    dialog->buffer[0] = '\0';
    dialog->cursor_pos = 0;
    dialog->visible = 1;
    dialog->title = title;
    dialog->last_mx = -1;
    dialog->last_my = -1;
    dialog->cursor_saved = 0;
    dialog->dragging = 0;
    dialog->drag_offset_x = 0;
    dialog->drag_offset_y = 0;
}

void input_dialog_draw(input_dialog_t* dialog) {
    if (!dialog->visible) return;
    
    uint32_t bg_color = 0x2D3748;
    uint32_t border_color = 0x4A5568;
    uint32_t title_bg = 0x1E40AF;
    uint32_t input_bg = 0x1F2937;
    uint32_t text_color = 0xFFFFFF;
    
    for (int dy = 0; dy < dialog->height; dy++) {
        for (int dx = 0; dx < dialog->width; dx++) {
            put_pixel(dialog->x + dx, dialog->y + dy, bg_color);
        }
    }
    
    for (int dy = 0; dy < 30; dy++) {
        for (int dx = 0; dx < dialog->width; dx++) {
            put_pixel(dialog->x + dx, dialog->y + dy, title_bg);
        }
    }
    
    int title_x = dialog->x + 10;
    int title_y = dialog->y + 11;
    for (int i = 0; dialog->title[i]; i++) {
        draw_char(title_x + i * 8, title_y, dialog->title[i], 0xFFFFFF, title_bg);
    }
    
    int input_x = dialog->x + 20;
    int input_y = dialog->y + 50;
    int input_w = dialog->width - 40;
    int input_h = 30;
    
    for (int dy = 0; dy < input_h; dy++) {
        for (int dx = 0; dx < input_w; dx++) {
            put_pixel(input_x + dx, input_y + dy, input_bg);
        }
    }
    
    for (int dx = 0; dx < input_w; dx++) {
        put_pixel(input_x + dx, input_y, border_color);
        put_pixel(input_x + dx, input_y + input_h - 1, border_color);
    }
    for (int dy = 0; dy < input_h; dy++) {
        put_pixel(input_x, input_y + dy, border_color);
        put_pixel(input_x + input_w - 1, input_y + dy, border_color);
    }
    
    int text_x = input_x + 5;
    int text_y = input_y + 11;
    for (int i = 0; dialog->buffer[i]; i++) {
        draw_char(text_x + i * 8, text_y, dialog->buffer[i], text_color, input_bg);
    }
    
    int cursor_x = text_x + dialog->cursor_pos * 8;
    for (int dy = 0; dy < 16; dy++) {
        put_pixel(cursor_x, text_y + dy, text_color);
        put_pixel(cursor_x + 1, text_y + dy, text_color);
    }
    
    int button_y = dialog->y + 100;
    int ok_x = dialog->x + 150;
    int cancel_x = dialog->x + 280;
    int button_w = 80;
    int button_h = 30;
    
    for (int dy = 0; dy < button_h; dy++) {
        for (int dx = 0; dx < button_w; dx++) {
            put_pixel(ok_x + dx, button_y + dy, 0x10B981);
        }
    }
    draw_char(ok_x + 30, button_y + 11, 'O', 0xFFFFFF, 0x10B981);
    draw_char(ok_x + 38, button_y + 11, 'K', 0xFFFFFF, 0x10B981);
    
    for (int dy = 0; dy < button_h; dy++) {
        for (int dx = 0; dx < button_w; dx++) {
            put_pixel(cancel_x + dx, button_y + dy, 0xDC2626);
        }
    }
    draw_char(cancel_x + 16, button_y + 11, 'C', 0xFFFFFF, 0xDC2626);
    draw_char(cancel_x + 24, button_y + 11, 'a', 0xFFFFFF, 0xDC2626);
    draw_char(cancel_x + 32, button_y + 11, 'n', 0xFFFFFF, 0xDC2626);
    draw_char(cancel_x + 40, button_y + 11, 'c', 0xFFFFFF, 0xDC2626);
    draw_char(cancel_x + 48, button_y + 11, 'e', 0xFFFFFF, 0xDC2626);
    draw_char(cancel_x + 56, button_y + 11, 'l', 0xFFFFFF, 0xDC2626);
}

int input_dialog_run(input_dialog_t* dialog, char* result, desktop_state_t* desktop,
                     void (*update_mouse)(void),
                     void (*redraw_desktop_fn)(desktop_state_t*),
                     void (*save_cursor_fn)(input_dialog_t*, int, int),
                     void (*restore_cursor_fn)(input_dialog_t*),
                     void (*draw_cursor_fn)(int, int),
                     int (*get_cursor_x)(void),
                     int (*get_cursor_y)(void)) {
    int running = 1;
    int return_value = 0;
    int last_mouse_button = 0;
    int needs_redraw = 0;
    
    while (running) {
        update_mouse();
        
        int mx = get_cursor_x();
        int my = get_cursor_y();
        int button = mouse_button_state & 0x01;
        
        if (dialog->dragging) {
            if (button) {
                int new_x = mx - dialog->drag_offset_x;
                int new_y = my - dialog->drag_offset_y;
                
                if (new_x < 0) new_x = 0;
                if (new_y < 0) new_y = 0;
                if (new_x + dialog->width > fb.width) new_x = fb.width - dialog->width;
                if (new_y + dialog->height > fb.height) new_y = fb.height - dialog->height;
                
                if (new_x != dialog->x || new_y != dialog->y) {
                    dialog->x = new_x;
                    dialog->y = new_y;
                    needs_redraw = 1;
                }
            } else {
                dialog->dragging = 0;
            }
        }
        
        if (mx != dialog->last_mx || my != dialog->last_my) {
            restore_cursor_fn(dialog);
            
            if (needs_redraw) {
                redraw_desktop_fn(desktop);
                input_dialog_draw(dialog);
                needs_redraw = 0;
            }
            
            save_cursor_fn(dialog, mx, my);
            draw_cursor_fn(mx, my);
            dialog->last_mx = mx;
            dialog->last_my = my;
        }
        
        while (scancode_read_pos != scancode_write_pos) {
            uint8_t scancode = scancode_buffer[scancode_read_pos++];
            
            if (scancode == 0xE0) continue;
            
            int is_release = (scancode & 0x80);
            uint8_t key = scancode & 0x7F;
            
            if (is_release) continue;
            
            if (key == 0x1C) {
                running = 0;
                return_value = 1;
                break;
            }
            
            if (key == 0x01) {
                running = 0;
                return_value = 0;
                break;
            }
            
            if (key == 0x0E) {
                if (dialog->cursor_pos > 0) {
                    dialog->cursor_pos--;
                    dialog->buffer[dialog->cursor_pos] = '\0';
                    needs_redraw = 1;
                }
                continue;
            }
            
            input_dialog_handle_key(dialog, scancode);
            needs_redraw = 1;
        }
        
        if (needs_redraw) {
            restore_cursor_fn(dialog);
            redraw_desktop_fn(desktop);
            input_dialog_draw(dialog);
            save_cursor_fn(dialog, mx, my);
            draw_cursor_fn(mx, my);
            needs_redraw = 0;
        }
        
        if (button && !last_mouse_button) {
            if (mx >= dialog->x && mx < dialog->x + dialog->width &&
                my >= dialog->y && my < dialog->y + 30) {
                dialog->dragging = 1;
                dialog->drag_offset_x = mx - dialog->x;
                dialog->drag_offset_y = my - dialog->y;
            }
            else {
                int button_y = dialog->y + 100;
                int ok_x = dialog->x + 150;
                int cancel_x = dialog->x + 280;
                int button_w = 80;
                int button_h = 30;
                
                if (mx >= ok_x && mx < ok_x + button_w &&
                    my >= button_y && my < button_y + button_h) {
                    running = 0;
                    return_value = 1;
                }
                else if (mx >= cancel_x && mx < cancel_x + button_w &&
                         my >= button_y && my < button_y + button_h) {
                    running = 0;
                    return_value = 0;
                }
            }
        }
        
        last_mouse_button = button;
        
        for (volatile int i = 0; i < 1000; i++);
    }
    
    if (return_value) {
        STRCPY(result, dialog->buffer);
    }
    
    return return_value;
}
void handle_context_menu_action(context_menu_item_t action, desktop_state_t *desktop,
                                void (*update_mouse)(void),
                                void (*redraw_desktop_fn)(desktop_state_t*),
                                void (*save_cursor_fn)(input_dialog_t*, int, int),
                                void (*restore_cursor_fn)(input_dialog_t*),
                                void (*draw_cursor_fn)(int, int),
                                int (*get_cursor_x)(void),
                                int (*get_cursor_y)(void),
                                void (*restore_main_cursor)(void),
                                taskbar_t* taskbar) {
    char filename[256];
    input_dialog_t dialog;
    
    switch (action) {
        case CONTEXT_MENU_CREATE_FILE: {
            char touch[] = "Make File [ $SUPERUSER | RING 00 ]";
            input_dialog_init(&dialog, touch);
            
            int mx = get_cursor_x();
            int my = get_cursor_y();
            
            restore_main_cursor();
            redraw_desktop_fn(desktop);
            input_dialog_draw(&dialog);
            save_cursor_fn(&dialog, mx, my);
            draw_cursor_fn(mx, my);
            
            if (input_dialog_run(&dialog, filename, desktop, update_mouse, redraw_desktop_fn,
                                save_cursor_fn, restore_cursor_fn, draw_cursor_fn,
                                get_cursor_x, get_cursor_y)) {
                if (filename[0] != '\0') {
                    char fullpath[256];
                    const char* cwd = vfs_get_cwd_path();
                    STRCPY(fullpath, cwd);
                    int len = STRLEN(fullpath);
                    if (len > 0 && fullpath[len-1] != '/') {
                        fullpath[len] = '/';
                        fullpath[len+1] = '\0';
                    }
                    STRCAT(fullpath, filename);
                    
                    // Add to taskbar BEFORE operation
                    if (taskbar) {
                        taskbar_add_item(taskbar, TASKBAR_ITEM_MKFILE, filename);
                    }
                    
                    // Force redraw to show taskbar item
                    restore_main_cursor();
                    redraw_desktop_fn(desktop);
                    mx = get_cursor_x();
                    my = get_cursor_y();
                    save_cursor_fn(&dialog, mx, my);
                    draw_cursor_fn(mx, my);
                    
                    // Keep taskbar visible longer (simulate work)
                    for (volatile int i = 0; i < 5000000; i++);
                    
                    // Now do the actual operation
                    int result = vfs_create(fullpath, FILE_READ | FILE_WRITE);
                    
                    if (result == 0) {
                        desktop_load_directory(desktop, cwd);
                        desktop_sort_icons(desktop);
                        desktop_arrange_icons(desktop, fb.width, fb.height, 48);
                        
                        PRINT(GREEN, BLACK, "[GUI] Created file: %s\n", filename);
                    } else {
                        PRINT(RED, BLACK, "[GUI] Failed to create file: %s\n", filename);
                    }
                    
                    // Keep showing success state
                    restore_main_cursor();
                    redraw_desktop_fn(desktop);
                    for (volatile int i = 0; i < 5000000; i++);
                    
                    // Remove from taskbar after delay
                    if (taskbar && taskbar->count > 0) {
                        for (int i = 0; i < taskbar->count; i++) {
                            if (taskbar->items[i].type == TASKBAR_ITEM_MKFILE) {
                                taskbar_remove_item(taskbar, i);
                                break;
                            }
                        }
                    }
                }
            }
            break;
        }
            
        case CONTEXT_MENU_CREATE_FOLDER: {
            char mkdir[] = "Make Directory [ $SUPERUSER | RING 00 ]";
            input_dialog_init(&dialog, mkdir);
            
            int mx = get_cursor_x();
            int my = get_cursor_y();
            
            restore_main_cursor();
            redraw_desktop_fn(desktop);
            input_dialog_draw(&dialog);
            save_cursor_fn(&dialog, mx, my);
            draw_cursor_fn(mx, my);
            
            if (input_dialog_run(&dialog, filename, desktop, update_mouse, redraw_desktop_fn,
                                save_cursor_fn, restore_cursor_fn, draw_cursor_fn,
                                get_cursor_x, get_cursor_y)) {
                if (filename[0] != '\0') {
                    char fullpath[256];
                    const char* cwd = vfs_get_cwd_path();
                    STRCPY(fullpath, cwd);
                    int len = STRLEN(fullpath);
                    if (len > 0 && fullpath[len-1] != '/') {
                        fullpath[len] = '/';
                        fullpath[len+1] = '\0';
                    }
                    STRCAT(fullpath, filename);
                    
                    // Add to taskbar BEFORE operation
                    if (taskbar) {
                        taskbar_add_item(taskbar, TASKBAR_ITEM_MKDIR, filename);
                    }
                    
                    // Force redraw to show taskbar item
                    restore_main_cursor();
                    redraw_desktop_fn(desktop);
                    mx = get_cursor_x();
                    my = get_cursor_y();
                    save_cursor_fn(&dialog, mx, my);
                    draw_cursor_fn(mx, my);
                    
                    // Keep taskbar visible longer (simulate work)
                    for (volatile int i = 0; i < 5000000; i++);
                    
                    // Now do the actual operation
                    int result = vfs_mkdir(fullpath, FILE_READ | FILE_WRITE);
                    
                    if (result == 0) {
                        desktop_load_directory(desktop, cwd);
                        desktop_sort_icons(desktop);
                        desktop_arrange_icons(desktop, fb.width, fb.height, 48);
                        
                        PRINT(GREEN, BLACK, "[GUI] Created directory: %s\n", filename);
                    } else {
                        PRINT(RED, BLACK, "[GUI] Failed to create directory: %s\n", filename);
                    }
                    
                    // Keep showing success state
                    restore_main_cursor();
                    redraw_desktop_fn(desktop);
                    for (volatile int i = 0; i < 5000000; i++);
                    
                    // Remove from taskbar after delay
                    if (taskbar && taskbar->count > 0) {
                        for (int i = 0; i < taskbar->count; i++) {
                            if (taskbar->items[i].type == TASKBAR_ITEM_MKDIR) {
                                taskbar_remove_item(taskbar, i);
                                break;
                            }
                        }
                    }
                }
            }
            break;
        }
            
        case CONTEXT_MENU_OPEN_TERMINAL:
               extern int running;
               running = 0;
            break;
            
        default:
            break;
    }
}