#include "taskbar.h"
#include "print.h"
#include "string_helpers.h"

void taskbar_init(taskbar_t* taskbar, int height) {
    taskbar->count = 0;
    taskbar->height = height;
    taskbar->hover_item = -1;
    
    for (int i = 0; i < MAX_TASKBAR_ITEMS; i++) {
        taskbar->items[i].type = TASKBAR_ITEM_MKDIR;
        taskbar->items[i].label[0] = '\0';
        taskbar->items[i].x = 0;
        taskbar->items[i].y = 0;
        taskbar->items[i].active = 0;
    }
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
        // Draw "MKDIR" text
        const char* text = "MKDIR";
        int text_x = x + 3;
        int text_y = y + 12;
        
        for (int i = 0; text[i]; i++) {
            draw_char(text_x + i * 5, text_y, text[i], text_color, fg_color);
        }
    } else if (type == TASKBAR_ITEM_MKFILE) {
        // Draw "MKFILE" text (smaller to fit)
        const char* text1 = "MK";
        const char* text2 = "FILE";
        int text_x = x + 5;
        int text_y1 = y + 8;
        int text_y2 = y + 18;
        
        for (int i = 0; text1[i]; i++) {
            draw_char(text_x + i * 6, text_y1, text1[i], text_color, fg_color);
        }
        for (int i = 0; text2[i]; i++) {
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
    
    // Calculate positions for items (centered in taskbar)
    int start_x = 10;
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