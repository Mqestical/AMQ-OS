#include "print.h"
#include "IO.h"
#include "desktop_icons.h"
#include "string_helpers.h"
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

int isgui = 0;

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
    desktop_state_t desktop;
    desktop_init(&desktop);
    
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

        if (mx != last_mx || my != last_my) {
            restore_cursor_area();
            save_cursor_area(mx, my);
            draw_cursor(mx, my);
            last_mx = mx;
            last_my = my;
        }

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