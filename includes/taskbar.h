#ifndef TASKBAR_H
#define TASKBAR_H

#include <stdint.h>

#define MAX_TASKBAR_ITEMS 10
#define TASKBAR_ITEM_WIDTH 120
#define TASKBAR_ITEM_HEIGHT 40
#define TASKBAR_ICON_SIZE 32

typedef enum {
    TASKBAR_ITEM_MKDIR,
    TASKBAR_ITEM_MKFILE,
    TASKBAR_ITEM_EDITOR
} taskbar_item_type_t;

typedef struct {
    taskbar_item_type_t type;
    char label[32];
    int x;
    int y;
    int active;
} taskbar_item_t;

typedef struct taskbar {
    taskbar_item_t items[MAX_TASKBAR_ITEMS];
    int count;
    int height;
    int hover_item;
} taskbar_t;

void taskbar_init(taskbar_t* taskbar, int height);
void taskbar_add_item(taskbar_t* taskbar, taskbar_item_type_t type, const char* label);
void taskbar_remove_item(taskbar_t* taskbar, int index);
void taskbar_draw(taskbar_t* taskbar, int screen_width, int screen_height);
void taskbar_update_hover(taskbar_t* taskbar, int mouse_x, int mouse_y, int screen_height);
int taskbar_get_item_at(taskbar_t* taskbar, int mouse_x, int mouse_y, int screen_height);
void taskbar_draw_icon(int x, int y, taskbar_item_type_t type);

#endif
