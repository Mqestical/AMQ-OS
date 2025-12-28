#ifndef TASKBAR_H
#define TASKBAR_H

#include <stdint.h>

#define MAX_TASKBAR_ITEMS 20
#define TASKBAR_ITEM_WIDTH 150
#define TASKBAR_ITEM_HEIGHT 40
#define TASKBAR_ICON_SIZE 32

#define START_BUTTON_WIDTH 80
#define START_BUTTON_HEIGHT 40

#define START_MENU_WIDTH 250
#define START_MENU_HEIGHT 300
#define START_MENU_ITEM_HEIGHT 40
#define MAX_START_MENU_ITEMS 10

typedef enum {
    TASKBAR_ITEM_MKDIR,
    TASKBAR_ITEM_MKFILE,
    TASKBAR_ITEM_EDITOR
} taskbar_item_type_t;

typedef struct {
    taskbar_item_type_t type;
    char label[64];
    int x, y;
    int active;
} taskbar_item_t;

typedef struct {
    char label[64];
    int icon_type;  // 0 = folder, 1 = file, 2 = app
} start_menu_item_t;

typedef struct {
    int visible;
    int x, y;
    int hover_item;
    start_menu_item_t items[MAX_START_MENU_ITEMS];
    int count;
} start_menu_t;

// Use the struct name that matches the forward declaration
struct taskbar {
    taskbar_item_t items[MAX_TASKBAR_ITEMS];
    int count;
    int height;
    int hover_item;
    
    // Start button
    int start_button_x;
    int start_button_y;
    int start_button_hover;
    
    // Start menu
    start_menu_t start_menu;
};

// Only typedef if not already done
#ifndef TASKBAR_TYPEDEF_DEFINED
typedef struct taskbar taskbar_t;
#endif

void taskbar_init(taskbar_t* taskbar, int height);
void taskbar_add_item(taskbar_t* taskbar, taskbar_item_type_t type, const char* label);
void taskbar_remove_item(taskbar_t* taskbar, int index);
void taskbar_draw(taskbar_t* taskbar, int screen_width, int screen_height);
void taskbar_draw_icon(int x, int y, taskbar_item_type_t type);
void taskbar_update_hover(taskbar_t* taskbar, int mouse_x, int mouse_y, int screen_height);
int taskbar_get_item_at(taskbar_t* taskbar, int mouse_x, int mouse_y, int screen_height);

// Start button functions
void taskbar_update_start_button_hover(taskbar_t* taskbar, int mouse_x, int mouse_y, int screen_height);
int taskbar_is_start_button_clicked(taskbar_t* taskbar, int mouse_x, int mouse_y, int screen_height);

// Start menu functions
void start_menu_init(start_menu_t* menu);
void start_menu_show(start_menu_t* menu, int x, int y);
void start_menu_hide(start_menu_t* menu);
void start_menu_draw(start_menu_t* menu);
void start_menu_update_hover(start_menu_t* menu, int mouse_x, int mouse_y);
int start_menu_get_item_at(start_menu_t* menu, int mouse_x, int mouse_y);

#endif