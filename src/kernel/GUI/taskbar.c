#include "taskbar.h"
#include "print.h"
#include "string_helpers.h"

void taskbar_init(taskbar_t* taskbar, int height) {
    taskbar->count = 0;
    taskbar->height = height;
    taskbar->hover_item = -1;
    taskbar->start_button_hover = 0;
    
    for (int i = 0; i < MAX_TASKBAR_ITEMS; i++) {
        taskbar->items[i].type = TASKBAR_ITEM_MKDIR;
        taskbar->items[i].label[0] = '\0';
        taskbar->items[i].x = 0;
        taskbar->items[i].y = 0;
        taskbar->items[i].active = 0;
    }
    
    // Initialize start menu
    start_menu_init(&taskbar->start_menu);
}

void taskbar_add_item(taskbar_t* taskbar, taskbar_item_type_t type, const char* label) {
    if (taskbar->count >= MAX_TASKBAR_ITEMS) return;
    
    taskbar_item_t* item = &taskbar->items[taskbar->count];
    item->type = type;
    STRCPY(item->label, label);
    item->active = 1;
    
    taskbar->count++;
}

void taskbar_remove_item(taskbar_t* taskbar, int index) {
    if (index < 0 || index >= taskbar->count) return;
    
    // Shift items down
    for (int i = index; i < taskbar->count - 1; i++) {
        taskbar->items[i] = taskbar->items[i + 1];
    }
    
    taskbar->count--;
    taskbar->items[taskbar->count].active = 0;
}

void taskbar_draw_icon(int x, int y, taskbar_item_type_t type) {
    uint32_t bg_color = 0x000000;  // Black background
    uint32_t fg_color = 0x9B59B6;  // Purple foreground
    uint32_t text_color = 0x000000; // Black text on purple
    
    // Draw black background (32x32)
    for (int dy = 0; dy < TASKBAR_ICON_SIZE; dy++) {
        for (int dx = 0; dx < TASKBAR_ICON_SIZE; dx++) {
            put_pixel(x + dx, y + dy, bg_color);
        }
    }
    
    // Draw purple inset (leave 2px black border)
    for (int dy = 2; dy < TASKBAR_ICON_SIZE - 2; dy++) {
        for (int dx = 2; dx < TASKBAR_ICON_SIZE - 2; dx++) {
            put_pixel(x + dx, y + dy, fg_color);
        }
    }
    
    // Draw text based on type
    if (type == TASKBAR_ITEM_MKDIR) {
        // Draw "MKDIR" text - manually assign
        char text[6];
        text[0] = 'M';
        text[1] = 'K';
        text[2] = 'D';
        text[3] = 'I';
        text[4] = 'R';
        text[5] = '\0';
        
        int text_x = x + 3;
        int text_y = y + 12;
        
        for (int i = 0; text[i] != '\0'; i++) {
            draw_char(text_x + i * 5, text_y, text[i], text_color, fg_color);
        }
    } else if (type == TASKBAR_ITEM_MKFILE) {
        // Draw "MKFILE" text (smaller to fit) - manually assign
        char text1[3];
        text1[0] = 'M';
        text1[1] = 'K';
        text1[2] = '\0';
        
        char text2[5];
        text2[0] = 'F';
        text2[1] = 'I';
        text2[2] = 'L';
        text2[3] = 'E';
        text2[4] = '\0';
        
        int text_x = x + 5;
        int text_y1 = y + 8;
        int text_y2 = y + 18;
        
        for (int i = 0; text1[i] != '\0'; i++) {
            draw_char(text_x + i * 6, text_y1, text1[i], text_color, fg_color);
        }
        for (int i = 0; text2[i] != '\0'; i++) {
            draw_char(text_x + i * 5, text_y2, text2[i], text_color, fg_color);
        }
    } else if (type == TASKBAR_ITEM_EDITOR) {
        // Draw a document icon
        // Document outline
        for (int dy = 4; dy < 28; dy++) {
            put_pixel(x + 8, y + dy, text_color);
            put_pixel(x + 24, y + dy, text_color);
        }
        for (int dx = 8; dx < 24; dx++) {
            put_pixel(x + dx, y + 4, text_color);
            put_pixel(x + dx, y + 27, text_color);
        }
        
        // Lines on document
        for (int dx = 11; dx < 21; dx++) {
            put_pixel(x + dx, y + 10, text_color);
            put_pixel(x + dx, y + 14, text_color);
            put_pixel(x + dx, y + 18, text_color);
            put_pixel(x + dx, y + 22, text_color);
        }
    }
}

static void draw_start_button(taskbar_t* taskbar, int screen_height) {
    int taskbar_y = screen_height - taskbar->height;
    int button_x = 5;
    int button_y = taskbar_y + (taskbar->height - START_BUTTON_HEIGHT) / 2;
    
    taskbar->start_button_x = button_x;
    taskbar->start_button_y = button_y;
    
    // Button background color (darker purple when hovered, purple otherwise)
    uint32_t bg_color = taskbar->start_button_hover ? 0x7B3FF2 : 0x6B2FE2;
    
    // Draw button background
    for (int dy = 0; dy < START_BUTTON_HEIGHT; dy++) {
        for (int dx = 0; dx < START_BUTTON_WIDTH; dx++) {
            put_pixel(button_x + dx, button_y + dy, bg_color);
        }
    }
    
    // Draw "Start" text - manually assign to avoid string literal issues
    char text[6];
    text[0] = 'S';
    text[1] = 't';
    text[2] = 'a';
    text[3] = 'r';
    text[4] = 't';
    text[5] = '\0';
    
    // Center the text in the button
    int text_x = button_x + (START_BUTTON_WIDTH - (5 * 8)) / 2;  // 5 characters * 8 pixels wide
    int text_y = button_y + (START_BUTTON_HEIGHT / 2) - 4;
    
    for (int i = 0; text[i] != '\0'; i++) {
        draw_char(text_x + i * 8, text_y, text[i], 0xFFFFFF, bg_color);
    }
}

void taskbar_draw(taskbar_t* taskbar, int screen_width, int screen_height) {
    int taskbar_y = screen_height - taskbar->height;
    
    // Draw taskbar background (white)
    for (int y = taskbar_y; y < screen_height; y++) {
        for (int x = 0; x < screen_width; x++) {
            put_pixel(x, y, 0xFFFFFF);
        }
    }
    
    // Draw top border
    for (int x = 0; x < screen_width; x++) {
        put_pixel(x, taskbar_y, 0xCCCCCC);
    }
    
    // Draw Start button
    draw_start_button(taskbar, screen_height);
    
    // Calculate positions for items (start after Start button)
    int start_x = START_BUTTON_WIDTH + 15;
    int item_y = taskbar_y + (taskbar->height - TASKBAR_ITEM_HEIGHT) / 2;
    
    // Sort items algorithmically: MKDIR, MKFILE, then others
    int sorted_indices[MAX_TASKBAR_ITEMS];
    int sorted_count = 0;
    
    // First pass: MKDIR items
    for (int i = 0; i < taskbar->count; i++) {
        if (taskbar->items[i].type == TASKBAR_ITEM_MKDIR) {
            sorted_indices[sorted_count++] = i;
        }
    }
    
    // Second pass: MKFILE items
    for (int i = 0; i < taskbar->count; i++) {
        if (taskbar->items[i].type == TASKBAR_ITEM_MKFILE) {
            sorted_indices[sorted_count++] = i;
        }
    }
    
    // Third pass: Other items
    for (int i = 0; i < taskbar->count; i++) {
        if (taskbar->items[i].type != TASKBAR_ITEM_MKDIR && 
            taskbar->items[i].type != TASKBAR_ITEM_MKFILE) {
            sorted_indices[sorted_count++] = i;
        }
    }
    
    // Draw items in sorted order
    for (int i = 0; i < sorted_count; i++) {
        int idx = sorted_indices[i];
        taskbar_item_t* item = &taskbar->items[idx];
        
        int item_x = start_x + i * (TASKBAR_ITEM_WIDTH + 5);
        item->x = item_x;
        item->y = item_y;
        
        // Draw item background
        uint32_t item_bg = (taskbar->hover_item == idx) ? 0xE0E0E0 : 0xF5F5F5;
        for (int dy = 0; dy < TASKBAR_ITEM_HEIGHT; dy++) {
            for (int dx = 0; dx < TASKBAR_ITEM_WIDTH; dx++) {
                put_pixel(item_x + dx, item_y + dy, item_bg);
            }
        }
        
        // Draw border
        uint32_t border_color = 0xCCCCCC;
        for (int dx = 0; dx < TASKBAR_ITEM_WIDTH; dx++) {
            put_pixel(item_x + dx, item_y, border_color);
            put_pixel(item_x + dx, item_y + TASKBAR_ITEM_HEIGHT - 1, border_color);
        }
        for (int dy = 0; dy < TASKBAR_ITEM_HEIGHT; dy++) {
            put_pixel(item_x, item_y + dy, border_color);
            put_pixel(item_x + TASKBAR_ITEM_WIDTH - 1, item_y + dy, border_color);
        }
        
        // Draw icon
        int icon_x = item_x + 4;
        int icon_y = item_y + (TASKBAR_ITEM_HEIGHT - TASKBAR_ICON_SIZE) / 2;
        taskbar_draw_icon(icon_x, icon_y, item->type);
        
        // Draw label
        int label_x = icon_x + TASKBAR_ICON_SIZE + 5;
        int label_y = item_y + (TASKBAR_ITEM_HEIGHT / 2) - 4;
        
        for (int j = 0; item->label[j] && j < 10; j++) {
            draw_char(label_x + j * 8, label_y, item->label[j], 0x000000, item_bg);
        }
    }
    
    // Draw start menu if visible
    if (taskbar->start_menu.visible) {
        start_menu_draw(&taskbar->start_menu);
    }
}

void taskbar_update_hover(taskbar_t* taskbar, int mouse_x, int mouse_y, int screen_height) {
    int taskbar_y = screen_height - taskbar->height;
    
    if (mouse_y < taskbar_y) {
        taskbar->hover_item = -1;
        return;
    }
    
    int old_hover = taskbar->hover_item;
    taskbar->hover_item = -1;
    
    for (int i = 0; i < taskbar->count; i++) {
        taskbar_item_t* item = &taskbar->items[i];
        
        if (mouse_x >= item->x && mouse_x < item->x + TASKBAR_ITEM_WIDTH &&
            mouse_y >= item->y && mouse_y < item->y + TASKBAR_ITEM_HEIGHT) {
            taskbar->hover_item = i;
            break;
        }
    }
}

int taskbar_get_item_at(taskbar_t* taskbar, int mouse_x, int mouse_y, int screen_height) {
    int taskbar_y = screen_height - taskbar->height;
    
    if (mouse_y < taskbar_y) {
        return -1;
    }
    
    for (int i = 0; i < taskbar->count; i++) {
        taskbar_item_t* item = &taskbar->items[i];
        
        if (mouse_x >= item->x && mouse_x < item->x + TASKBAR_ITEM_WIDTH &&
            mouse_y >= item->y && mouse_y < item->y + TASKBAR_ITEM_HEIGHT) {
            return i;
        }
    }
    
    return -1;
}

void taskbar_update_start_button_hover(taskbar_t* taskbar, int mouse_x, int mouse_y, int screen_height) {
    int taskbar_y = screen_height - taskbar->height;
    int button_x = taskbar->start_button_x;
    int button_y = taskbar->start_button_y;
    
    if (mouse_x >= button_x && mouse_x < button_x + START_BUTTON_WIDTH &&
        mouse_y >= button_y && mouse_y < button_y + START_BUTTON_HEIGHT) {
        taskbar->start_button_hover = 1;
    } else {
        taskbar->start_button_hover = 0;
    }
}

int taskbar_is_start_button_clicked(taskbar_t* taskbar, int mouse_x, int mouse_y, int screen_height) {
    int button_x = taskbar->start_button_x;
    int button_y = taskbar->start_button_y;
    
    if (mouse_x >= button_x && mouse_x < button_x + START_BUTTON_WIDTH &&
        mouse_y >= button_y && mouse_y < button_y + START_BUTTON_HEIGHT) {
        return 1;
    }
    
    return 0;
}

// Start menu functions
void start_menu_init(start_menu_t* menu) {
    menu->visible = 0;
    menu->x = 0;
    menu->y = 0;
    menu->hover_item = -1;
    menu->count = 0;
    
    // Zero out all items to ensure clean state
    for (int i = 0; i < MAX_START_MENU_ITEMS; i++) {
        menu->items[i].label[0] = '\0';
        menu->items[i].icon_type = 0;
    }
}

void start_menu_show(start_menu_t* menu, int x, int y) {
    menu->visible = 1;
    menu->x = x;
    menu->y = y;
    menu->hover_item = -1;
}

void start_menu_hide(start_menu_t* menu) {
    menu->visible = 0;
    menu->hover_item = -1;
}

void start_menu_draw(start_menu_t* menu) {
    if (!menu->visible) return;
    
    uint32_t bg_color = 0xF0F0F0;
    uint32_t border_color = 0x6B2FE2;  // Purple to match dragon button
    uint32_t text_color = 0x000000;
    
    // Draw background
    for (int dy = 0; dy < START_MENU_HEIGHT; dy++) {
        for (int dx = 0; dx < START_MENU_WIDTH; dx++) {
            put_pixel(menu->x + dx, menu->y + dy, bg_color);
        }
    }
    
    // Draw border
    for (int dx = 0; dx < START_MENU_WIDTH; dx++) {
        put_pixel(menu->x + dx, menu->y, border_color);
        put_pixel(menu->x + dx, menu->y + START_MENU_HEIGHT - 1, border_color);
    }
    for (int dy = 0; dy < START_MENU_HEIGHT; dy++) {
        put_pixel(menu->x, menu->y + dy, border_color);
        put_pixel(menu->x + START_MENU_WIDTH - 1, menu->y + dy, border_color);
    }
    
    // If menu is empty, show a message
    if (menu->count == 0) {
        // "No items" - manually assign to avoid string literal issues
        char msg[16];
        msg[0] = 'N';
        msg[1] = 'o';
        msg[2] = ' ';
        msg[3] = 'i';
        msg[4] = 't';
        msg[5] = 'e';
        msg[6] = 'm';
        msg[7] = 's';
        msg[8] = '\0';
        
        int text_x = menu->x + (START_MENU_WIDTH - 8 * 8) / 2;  // Center text
        int text_y = menu->y + START_MENU_HEIGHT / 2 - 4;
        
        for (int i = 0; msg[i] != '\0'; i++) {
            draw_char(text_x + i * 8, text_y, msg[i], text_color, bg_color);
        }
        return;
    }
    
    // Draw menu items (only if count > 0)
    uint32_t hover_color = 0xE0E0E0;
    for (int i = 0; i < menu->count; i++) {
        int item_y = menu->y + 5 + i * START_MENU_ITEM_HEIGHT;
        uint32_t item_bg = (menu->hover_item == i) ? hover_color : bg_color;
        
        // Draw item background
        for (int dy = 0; dy < START_MENU_ITEM_HEIGHT - 2; dy++) {
            for (int dx = 5; dx < START_MENU_WIDTH - 5; dx++) {
                put_pixel(menu->x + dx, item_y + dy, item_bg);
            }
        }
        
        // Draw simple icon
        int icon_x = menu->x + 10;
        int icon_y = item_y + 5;
        int icon_size = 24;
        
        uint32_t icon_color = 0x6B2FE2;  // Purple to match theme
        
        if (menu->items[i].icon_type == 0) {  // Folder
            // Draw folder
            for (int dy = 8; dy < icon_size; dy++) {
                for (int dx = 0; dx < icon_size; dx++) {
                    put_pixel(icon_x + dx, icon_y + dy, icon_color);
                }
            }
            // Folder tab
            for (int dy = 0; dy < 8; dy++) {
                for (int dx = 0; dx < 12; dx++) {
                    put_pixel(icon_x + dx, icon_y + dy, icon_color);
                }
            }
        } else if (menu->items[i].icon_type == 1) {  // File
            // Draw file
            for (int dy = 0; dy < icon_size; dy++) {
                put_pixel(icon_x + 2, icon_y + dy, icon_color);
                put_pixel(icon_x + icon_size - 2, icon_y + dy, icon_color);
            }
            for (int dx = 2; dx < icon_size - 2; dx++) {
                put_pixel(icon_x + dx, icon_y, icon_color);
                put_pixel(icon_x + dx, icon_y + icon_size - 1, icon_color);
            }
        } else {  // App
            // Draw app icon (rounded square)
            for (int dy = 2; dy < icon_size - 2; dy++) {
                for (int dx = 2; dx < icon_size - 2; dx++) {
                    put_pixel(icon_x + dx, icon_y + dy, icon_color);
                }
            }
        }
        
        // Draw text
        int text_x = icon_x + icon_size + 8;
        int text_y = item_y + (START_MENU_ITEM_HEIGHT / 2) - 4;
        
        for (int j = 0; menu->items[i].label[j] != '\0' && j < 20; j++) {
            draw_char(text_x + j * 8, text_y, menu->items[i].label[j], text_color, item_bg);
        }
    }
}

void start_menu_update_hover(start_menu_t* menu, int mouse_x, int mouse_y) {
    if (!menu->visible) {
        menu->hover_item = -1;
        return;
    }
    
    menu->hover_item = -1;
    
    for (int i = 0; i < menu->count; i++) {
        int item_y = menu->y + 5 + i * START_MENU_ITEM_HEIGHT;
        
        if (mouse_x >= menu->x + 5 && mouse_x < menu->x + START_MENU_WIDTH - 5 &&
            mouse_y >= item_y && mouse_y < item_y + START_MENU_ITEM_HEIGHT - 2) {
            menu->hover_item = i;
            break;
        }
    }
}

int start_menu_get_item_at(start_menu_t* menu, int mouse_x, int mouse_y) {
    if (!menu->visible) return -1;
    
    for (int i = 0; i < menu->count; i++) {
        int item_y = menu->y + 5 + i * START_MENU_ITEM_HEIGHT;
        
        if (mouse_x >= menu->x + 5 && mouse_x < menu->x + START_MENU_WIDTH - 5 &&
            mouse_y >= item_y && mouse_y < item_y + START_MENU_ITEM_HEIGHT - 2) {
            return i;
        }
    }
    
    return -1;
}