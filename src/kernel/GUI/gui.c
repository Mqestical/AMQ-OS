#include "print.h"
#include "IO.h"
#include "desktop_icons.h"
#include "string_helpers.h"
#include "vfs.h"
#include "process.h"
#include "gui.h"
#include "create_dialog.h"
#include "taskbar.h"
#include "text_editor.h"

#define WIDTH 1366
#define HEIGHT 768
#define TASKBAR_HEIGHT 48
#define DOUBLE_CLICK_TIME 500000  // milliseconds
#define DOUBLE_CLICK_DISTANCE 5  // pixels

extern volatile uint8_t scancode_buffer[256];
extern volatile uint8_t scancode_read_pos;
extern volatile uint8_t scancode_write_pos;
extern int mouse_button_state;

int running = 0;

static int internal_cursor_x = 512;
static int internal_cursor_y = 384;
static int mouse_initialized = 0;
static uint32_t saved_pixels[16*16];
static int cursor_saved = 0;
static int saved_cx, saved_cy;
static int last_button_state = 0;

// Double-click tracking
static uint64_t last_click_time = 0;
static int last_click_x = -1;
static int last_click_y = -1;
static int last_clicked_icon = -1;

// Simple tick counter for double-click detection
static volatile uint64_t tick_counter = 0;

int isgui = 0;
volatile int gui_owns_input = 0;

desktop_state_t desktop;
taskbar_t taskbar;

static void update_mouse_position_only(void);
static void save_cursor_area(int x, int y);
static void restore_cursor_area(void);
static void draw_cursor(int x, int y);
static void redraw_desktop(desktop_state_t *desktop);

// Simple timer function (increments on each main loop iteration)
static uint64_t get_tick_count(void) {
    return tick_counter;
}

static int get_internal_cursor_x(void) {
    return internal_cursor_x;
}

static int get_internal_cursor_y(void) {
    return internal_cursor_y;
}

void gui_thread_entry(void) {
    PRINT(GREEN, BLACK, "[GUI] Starting as thread\n");
    
    __asm__ volatile("sti");
    
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0" : "=r"(flags));
    PRINT(WHITE, BLACK, "[GUI] RFLAGS = 0x%llx (IF=%d)\n", flags, !!(flags & 0x200));
    
    gui_owns_input = 1;
    
    __asm__ volatile("cli");
    scancode_read_pos = 0;
    scancode_write_pos = 0;
    for (int i = 0; i < 256; i++) {
        scancode_buffer[i] = 0;
    }
    __asm__ volatile("sti");
    
    PRINT(GREEN, BLACK, "[GUI] Input buffer claimed\n");
    
    gui_main();
    
    gui_owns_input = 0;
    
    __asm__ volatile("cli");
    scancode_read_pos = 0;
    scancode_write_pos = 0;
    for (int i = 0; i < 256; i++) {
        scancode_buffer[i] = 0;
    }
    __asm__ volatile("sti");
    
    PRINT(GREEN, BLACK, "[GUI] Input buffer released\n");
    
    PRINT(GREEN, BLACK, "[GUI] Exiting\n");
    thread_exit();
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

    if (!gui_owns_input) {
        return;
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

                mouse_button_state = b1 & 0x03;

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
            if (gui_owns_input) {
                scancode_buffer[scancode_write_pos++] = data;
            }
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

static void redraw_desktop(desktop_state_t *desktop) {
    uint32_t dark_cyan = 0x008B8B;
    for (int y = 0; y < fb.height - TASKBAR_HEIGHT; y++) {
        for (int x = 0; x < fb.width; x++) {
            put_pixel(x, y, dark_cyan);
        }
    }
    taskbar_draw(&taskbar, fb.width, fb.height);
    desktop_draw_icons(desktop);
}

static int is_double_click(int mx, int my, int icon_index) {
    uint64_t current_time = get_tick_count();
    
    // Check if this is the same icon
    if (icon_index != last_clicked_icon) {
        return 0;
    }
    
    // Check time difference (using loop iterations as approximation)
    // Increased threshold for more reliable detection
    if (current_time - last_click_time > 50000) {  // ~500ms worth of iterations
        return 0;
    }
    
    // Check distance
    int dx = mx - last_click_x;
    int dy = my - last_click_y;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    
    if (dx > DOUBLE_CLICK_DISTANCE || dy > DOUBLE_CLICK_DISTANCE) {
        return 0;
    }
    
    return 1;
}

// Helper function to open file in text editor
static void open_file_in_editor(desktop_icon_t* icon) {
    PRINT(CYAN, BLACK, "[GUI] Opening file in editor: %s\n", icon->full_path);
    
    text_editor_t editor;
    text_editor_init(&editor, icon->full_path);
    
    // Add editor to taskbar
    taskbar_add_item(&taskbar, TASKBAR_ITEM_EDITOR, icon->name);
    
    // Redraw desktop with editor
    restore_cursor_area();
    redraw_desktop(&desktop);
    text_editor_draw(&editor);
    
    int mx = internal_cursor_x;
    int my = internal_cursor_y;
    editor_save_cursor_area(&editor, mx, my);
    draw_cursor(mx, my);
    
    // Run editor
    text_editor_run(&editor, &desktop,
                   update_mouse_position_only,
                   redraw_desktop,
                   editor_save_cursor_area,
                   editor_restore_cursor_area,
                   draw_cursor,
                   get_internal_cursor_x,
                   get_internal_cursor_y);
    
    // Remove from taskbar when closed
    for (int i = 0; i < taskbar.count; i++) {
        if (taskbar.items[i].type == TASKBAR_ITEM_EDITOR) {
            taskbar_remove_item(&taskbar, i);
            break;
        }
    }
    
    // Redraw desktop after editor closes
    editor_restore_cursor_area(&editor);
    redraw_desktop(&desktop);
    mx = internal_cursor_x;
    my = internal_cursor_y;
    save_cursor_area(mx, my);
    draw_cursor(mx, my);
}

void gui_main(void) {
    isgui = 1;
    uint32_t dark_cyan = 0x008B8B;

    for (int y = 0; y < fb.height - TASKBAR_HEIGHT; y++) {
        for (int x = 0; x < fb.width; x++) {
            put_pixel(x, y, dark_cyan);
        }
    }

    taskbar_init(&taskbar, TASKBAR_HEIGHT);
    taskbar_draw(&taskbar, fb.width, fb.height);

    SetCursorPos(0, 0);
    desktop_init(&desktop);

    context_menu_t context_menu;
    context_menu_init(&context_menu);

    vfs_node_t *root = vfs_get_root();
    if (root) {
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

    int last_mx = -1, last_my = -1;
    int mx = internal_cursor_x;
    int my = internal_cursor_y;

    save_cursor_area(mx, my);
    draw_cursor(mx, my);
    last_mx = mx;
    last_my = my;

    running = 1;

    while (running) {
        tick_counter++;
        update_mouse_position_only();

        mx = internal_cursor_x;
        my = internal_cursor_y;

        int old_hover = taskbar.hover_item;
        taskbar_update_hover(&taskbar, mx, my, fb.height);

        if (old_hover != taskbar.hover_item) {
            restore_cursor_area();
            redraw_desktop(&desktop);
            save_cursor_area(mx, my);
            draw_cursor(mx, my);
        }

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

        int left_click  = (mouse_button_state & 0x01) && !(last_button_state & 0x01);
        int right_click = (mouse_button_state & 0x02) && !(last_button_state & 0x02);

        /* ---------------- LEFT CLICK ---------------- */
        if (left_click) {
            int taskbar_item = taskbar_get_item_at(&taskbar, mx, my, fb.height);

            if (taskbar_item >= 0) {
                taskbar_remove_item(&taskbar, taskbar_item);
            } 
            else if (context_menu.visible) {
                int item = context_menu_get_item_at(&context_menu, mx, my);
                if (item >= 0) {
                    handle_context_menu_action(
                        (context_menu_item_t)item, &desktop,
                        update_mouse_position_only,
                        redraw_desktop,
                        dialog_save_cursor_area,
                        dialog_restore_cursor_area,
                        draw_cursor,
                        get_internal_cursor_x,
                        get_internal_cursor_y,
                        restore_cursor_area,
                        &taskbar
                    );
                }
                context_menu_hide(&context_menu);
            } 
            else {
                int icon_index = desktop_icon_at_position(&desktop, mx, my);
                if (icon_index >= 0) {
                    if (is_double_click(mx, my, icon_index)) {
                        desktop_icon_t *icon = &desktop.icons[icon_index];

                        if (icon->is_directory) {
                            PRINT(CYAN, BLACK, "[GUI] Folder: %s\n", icon->name);
                        } else {
                            PRINT(CYAN, BLACK, "[GUI] Open file: %s\n", icon->full_path);
                            open_file_in_editor(icon);
                        }

                        last_click_time = 0;
                        last_clicked_icon = -1;
                    } else {
                        desktop_select_icon(&desktop, icon_index);
                        last_click_time = get_tick_count();
                        last_click_x = mx;
                        last_click_y = my;
                        last_clicked_icon = icon_index;
                    }
                } else {
                    desktop_select_icon(&desktop, -1);
                    last_clicked_icon = -1;
                }
            }

            restore_cursor_area();
            redraw_desktop(&desktop);
            save_cursor_area(mx, my);
            draw_cursor(mx, my);
        }

        /* ---------------- RIGHT CLICK ---------------- */
        if (right_click) {
            int icon_index = desktop_icon_at_position(&desktop, mx, my);

            if (icon_index < 0) {
                context_menu_show(&context_menu, mx, my);

                if (context_menu.x + CONTEXT_MENU_WIDTH > fb.width)
                    context_menu.x = fb.width - CONTEXT_MENU_WIDTH;

                if (context_menu.y + CONTEXT_MENU_HEIGHT > fb.height - TASKBAR_HEIGHT)
                    context_menu.y = fb.height - TASKBAR_HEIGHT - CONTEXT_MENU_HEIGHT;

                restore_cursor_area();
                context_menu_draw(&context_menu);
                save_cursor_area(mx, my);
                draw_cursor(mx, my);
            }
        }

        last_button_state = mouse_button_state;

        while (scancode_read_pos != scancode_write_pos) {
            uint8_t scancode = scancode_buffer[scancode_read_pos++];
            if (scancode == 0xE0 || (scancode & 0x80)) continue;
            if (scancode == 0x01) running = 0;
        }

        thread_yield();
        for (volatile int i = 0; i < 4000; i++);
    }

    restore_cursor_area();
    disable_mouse();

    __asm__ volatile("cli");
    scancode_read_pos = scancode_write_pos = 0;
    for (int i = 0; i < 256; i++) scancode_buffer[i] = 0;
    __asm__ volatile("sti");

    mouse_initialized = 0;
    mouse_button_state = 0;

    ClearScreen(BLACK);
    SetCursorPos(0, 0);
    isgui = 0;
}
