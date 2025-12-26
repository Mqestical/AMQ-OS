#include "print.h"
#include "IO.h"
#include "desktop_icons.h"
#include "string_helpers.h"
#include "vfs.h"
#define WIDTH 1366
#define HEIGHT 768
#define TASKBAR_HEIGHT 48

extern volatile uint8_t scancode_buffer[256];
extern volatile uint8_t scancode_read_pos;
extern volatile uint8_t scancode_write_pos;
extern int mouse_button_state;

static int internal_cursor_x = 512;
static int internal_cursor_y = 384;
static int mouse_initialized = 0;
static uint32_t saved_pixels[16*16];
static int cursor_saved = 0;
static int saved_cx, saved_cy;
static int last_button_state = 0;

int isgui = 0;
desktop_state_t desktop;
static void update_mouse_position_only(void);
static void save_cursor_area(int x, int y);
static void restore_cursor_area(void);
static void draw_cursor(int x, int y);
static void redraw_desktop(desktop_state_t *desktop);

typedef struct {
    int x, y, width, height;
    char buffer[256];
    int cursor_pos;
    int visible;
    const char* title;
    int last_mx, last_my;
    uint32_t saved_pixels[16*16];
    int cursor_saved;
    int saved_cx, saved_cy;
    int dragging;
    int drag_offset_x, drag_offset_y;
} input_dialog_t;

static void dialog_save_cursor_area(input_dialog_t* dialog, int x, int y) {
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

static void dialog_restore_cursor_area(input_dialog_t* dialog) {
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

static void input_dialog_init(input_dialog_t* dialog, const char* title) {
    dialog->width = 500;  // Wider for longer title
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

static void input_dialog_draw(input_dialog_t* dialog) {
    if (!dialog->visible) return;
    
    uint32_t bg_color = 0x2D3748;
    uint32_t border_color = 0x4A5568;
    uint32_t title_bg = 0x1E40AF;
    uint32_t input_bg = 0x1F2937;
    uint32_t text_color = 0xFFFFFF;
    
    // Draw background
    for (int dy = 0; dy < dialog->height; dy++) {
        for (int dx = 0; dx < dialog->width; dx++) {
            put_pixel(dialog->x + dx, dialog->y + dy, bg_color);
        }
    }
    
    // Draw title bar (30px tall)
    for (int dy = 0; dy < 30; dy++) {
        for (int dx = 0; dx < dialog->width; dx++) {
            put_pixel(dialog->x + dx, dialog->y + dy, title_bg);
        }
    }
    
    // Draw title text character by character
    int title_x = dialog->x + 10;
    int title_y = dialog->y + 11;
    for (int i = 0; dialog->title[i]; i++) {
        draw_char(title_x + i * 8, title_y, dialog->title[i], 0xFFFFFF, title_bg);
    }
    
    // Draw input field background
    int input_x = dialog->x + 20;
    int input_y = dialog->y + 50;
    int input_w = dialog->width - 40;
    int input_h = 30;
    
    for (int dy = 0; dy < input_h; dy++) {
        for (int dx = 0; dx < input_w; dx++) {
            put_pixel(input_x + dx, input_y + dy, input_bg);
        }
    }
    
    // Draw input border
    for (int dx = 0; dx < input_w; dx++) {
        put_pixel(input_x + dx, input_y, border_color);
        put_pixel(input_x + dx, input_y + input_h - 1, border_color);
    }
    for (int dy = 0; dy < input_h; dy++) {
        put_pixel(input_x, input_y + dy, border_color);
        put_pixel(input_x + input_w - 1, input_y + dy, border_color);
    }
    
    // Draw text in input field
    int text_x = input_x + 5;
    int text_y = input_y + 11;
    for (int i = 0; dialog->buffer[i]; i++) {
        draw_char(text_x + i * 8, text_y, dialog->buffer[i], text_color, input_bg);
    }
    
    // Draw cursor
    int cursor_x = text_x + dialog->cursor_pos * 8;
    for (int dy = 0; dy < 16; dy++) {
        put_pixel(cursor_x, text_y + dy, text_color);
        put_pixel(cursor_x + 1, text_y + dy, text_color);
    }
    
    // Draw buttons
    int button_y = dialog->y + 100;
    int ok_x = dialog->x + 150;
    int cancel_x = dialog->x + 280;
    int button_w = 80;
    int button_h = 30;
    
    // OK button
    for (int dy = 0; dy < button_h; dy++) {
        for (int dx = 0; dx < button_w; dx++) {
            put_pixel(ok_x + dx, button_y + dy, 0x10B981);
        }
    }
    draw_char(ok_x + 30, button_y + 11, 'O', 0xFFFFFF, 0x10B981);
    draw_char(ok_x + 38, button_y + 11, 'K', 0xFFFFFF, 0x10B981);
    
    // Cancel button
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

static int input_dialog_run(input_dialog_t* dialog, char* result, desktop_state_t* desktop) {
    int running = 1;
    int return_value = 0;
    int last_mouse_button = 0;
    int needs_redraw = 0;
    
    while (running) {
        update_mouse_position_only();
        
        int mx = internal_cursor_x;
        int my = internal_cursor_y;
        int button = mouse_button_state & 0x01;
        
        // Handle dragging
        if (dialog->dragging) {
            if (button) {
                // Update dialog position
                int new_x = mx - dialog->drag_offset_x;
                int new_y = my - dialog->drag_offset_y;
                
                // Keep dialog on screen
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
                // Released mouse, stop dragging
                dialog->dragging = 0;
            }
        }
        
        // Update mouse cursor position if it moved
        if (mx != dialog->last_mx || my != dialog->last_my) {
            dialog_restore_cursor_area(dialog);
            
            if (needs_redraw) {
                redraw_desktop(desktop);
                input_dialog_draw(dialog);
                needs_redraw = 0;
            }
            
            dialog_save_cursor_area(dialog, mx, my);
            draw_cursor(mx, my);
            dialog->last_mx = mx;
            dialog->last_my = my;
        }
        
        // Handle keyboard
        while (scancode_read_pos != scancode_write_pos) {
            uint8_t scancode = scancode_buffer[scancode_read_pos++];
            
            if (scancode == 0xE0) continue;
            
            int is_release = (scancode & 0x80);
            uint8_t key = scancode & 0x7F;
            
            if (is_release) continue;
            
            // Enter
            if (key == 0x1C) {
                running = 0;
                return_value = 1;
                break;
            }
            
            // Escape
            if (key == 0x01) {
                running = 0;
                return_value = 0;
                break;
            }
            
            // Backspace
            if (key == 0x0E) {
                if (dialog->cursor_pos > 0) {
                    dialog->cursor_pos--;
                    dialog->buffer[dialog->cursor_pos] = '\0';
                    needs_redraw = 1;
                }
                continue;
            }
            
            // Handle character
            input_dialog_handle_key(dialog, scancode);
            needs_redraw = 1;
        }
        
        // Redraw if needed
        if (needs_redraw) {
            dialog_restore_cursor_area(dialog);
            redraw_desktop(desktop);
            input_dialog_draw(dialog);
            dialog_save_cursor_area(dialog, mx, my);
            draw_cursor(mx, my);
            needs_redraw = 0;
        }
        
        // Handle mouse clicks
        if (button && !last_mouse_button) {
            // Check if clicking in title bar (for dragging)
            if (mx >= dialog->x && mx < dialog->x + dialog->width &&
                my >= dialog->y && my < dialog->y + 30) {
                dialog->dragging = 1;
                dialog->drag_offset_x = mx - dialog->x;
                dialog->drag_offset_y = my - dialog->y;
            }
            // Check buttons
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

static void update_mouse_position_only(void) {
    static uint8_t packet[3];
    static uint8_t packet_index = 0;

    if (!mouse_initialized) {
        int timeout = 100000;
        while ((inb(0x64) & 2) && timeout--);
        outb(0x64, 0xD4);

        timeout = 100000;
        while ((inb(0x64) & 2) && timeout--);
        outb(0x60, 0xF4);

        timeout = 100000;
        while (!(inb(0x64) & 1) && timeout--);
        if (inb(0x64) & 1) {
            inb(0x60);
        }

        mouse_initialized = 1;
        packet_index = 0;
        internal_cursor_x = fb.width / 2;
        internal_cursor_y = fb.height / 2;
    }

    if (inb(0x64) & 0x01) {
        uint8_t status = inb(0x64);
        uint8_t data = inb(0x60);

        if (status & 0x20) {
            packet[packet_index++] = data;

            if (packet_index == 3) {
                uint8_t b1 = packet[0];
                uint8_t b2 = packet[1];
                uint8_t b3 = packet[2];

                mouse_button_state = b1 & 0x03;  // Track both left (bit 0) and right (bit 1)

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
        } else {
            scancode_buffer[scancode_write_pos++] = data;
        }
    }
}

static void disable_mouse(void) {
    int timeout = 100000;
    while ((inb(0x64) & 2) && timeout--);
    outb(0x64, 0xD4);

    timeout = 100000;
    while ((inb(0x64) & 2) && timeout--);
    outb(0x60, 0xF5);

    timeout = 100000;
    while (!(inb(0x64) & 1) && timeout--);
    if (inb(0x64) & 1) {
        inb(0x60);
    }

    timeout = 10000;
    while (timeout-- > 0 && (inb(0x64) & 0x01)) {
        inb(0x60);
    }
}

static void save_cursor_area(int x, int y) {
    for (int dy = 0; dy < 16 && (y + dy) < fb.height; dy++) {
        for (int dx = 0; dx < 16 && (x + dx) < fb.width; dx++) {
            int px = x + dx;
            int py = y + dy;
            saved_pixels[dy * 16 + dx] = fb.base[py * fb.width + px];
        }
    }
    cursor_saved = 1;
    saved_cx = x;
    saved_cy = y;
}



static void restore_cursor_area(void) {
    if (!cursor_saved) return;

    for (int dy = 0; dy < 16 && (saved_cy + dy) < fb.height; dy++) {
        for (int dx = 0; dx < 16 && (saved_cx + dx) < fb.width; dx++) {
            int px = saved_cx + dx;
            int py = saved_cy + dy;
            fb.base[py * fb.width + px] = saved_pixels[dy * 16 + dx];
        }
    }
    cursor_saved = 0;
}

static void draw_cursor(int x, int y) {
    uint32_t color = 0x000000;
    int height = 16;

    for (int i = 0; i < height && (y + i) < fb.height && x < fb.width; i++) {
        put_pixel(x, y + i, color);
    }

    for (int i = 0; i < height && (y + i) < fb.height && (x + i) < fb.width; i++) {
        put_pixel(x + i, y + i, color);
    }

    for (int i = 0; i < height && (x + i) < fb.width && (y + height - 1) < fb.height; i++) {
        put_pixel(x + i, y + height - 1, color);
    }

    for (int yy = 1; yy < height && (y + yy) < fb.height; yy++) {
        for (int xx = 1; xx < yy && (x + xx) < fb.width; xx++) {
            put_pixel(x + xx, y + yy, color);
        }
    }
}

static void draw_taskbar(void) {
    int taskbar_y = fb.height - TASKBAR_HEIGHT;
    
    for (int y = taskbar_y; y < fb.height; y++) {
        for (int x = 0; x < fb.width; x++) {
            put_pixel(x, y, WHITE);
        }
    }
}

static void redraw_desktop(desktop_state_t *desktop) {
    uint32_t dark_cyan = 0x008B8B;
    for (int y = 0; y < fb.height - TASKBAR_HEIGHT; y++) {
        for (int x = 0; x < fb.width; x++) {
            put_pixel(x, y, dark_cyan);
        }
    }
    draw_taskbar();
    desktop_draw_icons(desktop);
}

static void handle_context_menu_action(context_menu_item_t action, desktop_state_t *desktop) {
    char filename[256];
    input_dialog_t dialog;
    
    switch (action) {
        case CONTEXT_MENU_CREATE_FILE: {
            char touch[] = "Make File [ $SUPERUSER | RING 00 ]";
            input_dialog_init(&dialog, touch);
            
            int mx = internal_cursor_x;
            int my = internal_cursor_y;
            
            restore_cursor_area();
            redraw_desktop(desktop);
            input_dialog_draw(&dialog);
            dialog_save_cursor_area(&dialog, mx, my);
            draw_cursor(mx, my);
            
            if (input_dialog_run(&dialog, filename, desktop)) {
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
                    
                    if (vfs_create(fullpath, FILE_READ | FILE_WRITE) == 0) {
                        desktop_load_directory(desktop, cwd);
                        desktop_sort_icons(desktop);
                        desktop_arrange_icons(desktop, fb.width, fb.height, TASKBAR_HEIGHT);
                    }
                }
            }
            break;
        }
            
        case CONTEXT_MENU_CREATE_FOLDER: {
            char mkdir[] = "Make Directory [ $SUPERUSER | RING 00 ]";
            input_dialog_init(&dialog, mkdir);
            
            int mx = internal_cursor_x;
            int my = internal_cursor_y;
            
            restore_cursor_area();
            redraw_desktop(desktop);
            input_dialog_draw(&dialog);
            dialog_save_cursor_area(&dialog, mx, my);
            draw_cursor(mx, my);
            
            if (input_dialog_run(&dialog, filename, desktop)) {
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
                    
                    if (vfs_mkdir(fullpath, FILE_READ | FILE_WRITE) == 0) {
                        desktop_load_directory(desktop, cwd);
                        desktop_sort_icons(desktop);
                        desktop_arrange_icons(desktop, fb.width, fb.height, TASKBAR_HEIGHT);
                    }
                }
            }
            break;
        }
            
        case CONTEXT_MENU_OPEN_TERMINAL:
            break;
            
        default:
            break;
    }
}
void gui_main(void) {
    isgui = 1;
    uint32_t dark_cyan = 0x008B8B;
    for (int y = 0; y < fb.height - TASKBAR_HEIGHT; y++) {
        for (int x = 0; x < fb.width; x++) {
            put_pixel(x, y, dark_cyan);
        }
    }
    draw_taskbar();
    SetCursorPos(0, 0);
    
    // Initialize desktop icons
    
    desktop_init(&desktop);
    
    // Initialize context menu
    context_menu_t context_menu;
    context_menu_init(&context_menu);
    
    // Check if VFS root is available
    vfs_node_t *root = vfs_get_root();
    if (root != NULL) {
        // Load icons from root directory
        char desktop_path[] = "/";
        if (desktop_load_directory(&desktop, desktop_path) >= 0) {
            desktop_sort_icons(&desktop);
            desktop_arrange_icons(&desktop, fb.width, fb.height, TASKBAR_HEIGHT);
            desktop_draw_icons(&desktop);
        } else {
            PRINT(YELLOW, BLACK, "[GUI] Failed to load desktop icons\n");
        }
    } else {
        PRINT(YELLOW, BLACK, "[GUI] VFS not mounted, no icons to display\n");
    }
    
    int running = 1;
    int last_mx = -1;
    int last_my = -1;

    int mx = internal_cursor_x;
    int my = internal_cursor_y;
    save_cursor_area(mx, my);
    draw_cursor(mx, my);
    last_mx = mx;
    last_my = my;

    while (running) {
        update_mouse_position_only();

        mx = internal_cursor_x;
        my = internal_cursor_y;

        // Update context menu hover state
        if (context_menu.visible) {
            int old_selected = context_menu.selected_item;
            context_menu_update_hover(&context_menu, mx, my);
            
            if (old_selected != context_menu.selected_item) {
                restore_cursor_area();
                redraw_desktop(&desktop);
                context_menu_draw(&context_menu);
                save_cursor_area(mx, my);
                draw_cursor(mx, my);
            }
        }

        if (mx != last_mx || my != last_my) {
            restore_cursor_area();
            save_cursor_area(mx, my);
            draw_cursor(mx, my);
            last_mx = mx;
            last_my = my;
        }

        // Handle mouse button events
        int left_click = (mouse_button_state & 0x01) && !(last_button_state & 0x01);
        int right_click = (mouse_button_state & 0x02) && !(last_button_state & 0x02);
        
        if (left_click) {
            if (context_menu.visible) {
                int item = context_menu_get_item_at(&context_menu, mx, my);
                if (item >= 0) {
                    handle_context_menu_action((context_menu_item_t)item, &desktop);
                }
                
                // Hide menu after click (or if clicked outside)
                context_menu_hide(&context_menu);
                restore_cursor_area();
                redraw_desktop(&desktop);
                save_cursor_area(mx, my);
                draw_cursor(mx, my);
            } else {
                // Check if clicking on an icon
                int icon_index = desktop_icon_at_position(&desktop, mx, my);
                if (icon_index >= 0) {
                    desktop_select_icon(&desktop, icon_index);
                    PRINT(CYAN, BLACK, "[GUI] Selected icon: %s\n", desktop.icons[icon_index].name);
                } else {
                    desktop_select_icon(&desktop, -1);
                }
                
                restore_cursor_area();
                redraw_desktop(&desktop);
                save_cursor_area(mx, my);
                draw_cursor(mx, my);
            }
        }
        
        if (right_click) {
            // Check if right-clicking on empty space
            int icon_index = desktop_icon_at_position(&desktop, mx, my);
            
            if (icon_index < 0) {
                // Right-clicked on empty desktop
                context_menu_show(&context_menu, mx, my);
                
                // Make sure menu doesn't go off screen
                if (context_menu.x + CONTEXT_MENU_WIDTH > fb.width) {
                    context_menu.x = fb.width - CONTEXT_MENU_WIDTH;
                }
                if (context_menu.y + CONTEXT_MENU_HEIGHT > fb.height - TASKBAR_HEIGHT) {
                    context_menu.y = fb.height - TASKBAR_HEIGHT - CONTEXT_MENU_HEIGHT;
                }
                
                restore_cursor_area();
                context_menu_draw(&context_menu);
                save_cursor_area(mx, my);
                draw_cursor(mx, my);
                
                PRINT(CYAN, BLACK, "[GUI] Context menu opened at (%d, %d)\n", mx, my);
            } else {
                // Right-clicked on an icon (could implement icon-specific menu later)
                PRINT(CYAN, BLACK, "[GUI] Right-clicked on icon: %s\n", desktop.icons[icon_index].name);
            }
        }
        
        last_button_state = mouse_button_state;

        while (scancode_read_pos != scancode_write_pos) {
            uint8_t scancode = scancode_buffer[scancode_read_pos++];
            
            if (scancode == 0xE0) continue;
            if (scancode & 0x80) continue;
            
            if (scancode == 0x01) {
                running = 0;
            }
        }
    }

    restore_cursor_area();
    
    disable_mouse();
    
    for (volatile int i = 0; i < 100000; i++);
    
    __asm__ volatile("cli");
    scancode_read_pos = 0;
    scancode_write_pos = 0;
    
    for (int i = 0; i < 256; i++) {
        scancode_buffer[i] = 0;
    }
    __asm__ volatile("sti");
    
    mouse_initialized = 0;
    mouse_button_state = 0;
    
    ClearScreen(BLACK);
    SetCursorPos(0, 0);
}