#ifndef DESKTOP_ICONS_H
#define DESKTOP_ICONS_H

#include <stdint.h>
#include "vfs.h"
#include "print.h"

#define MAX_DESKTOP_ICONS 64
#define MAX_ICON_NAME_LEN 32
#define ICON_WIDTH 48
#define ICON_HEIGHT 48
#define ICON_SPACING 20
#define ICON_TEXT_OFFSET 52

// Context menu dimensions
#define CONTEXT_MENU_WIDTH 150
#define CONTEXT_MENU_HEIGHT 90
#define CONTEXT_MENU_ITEM_HEIGHT 30

typedef enum {
    ICON_TYPE_FOLDER,
    ICON_TYPE_FILE_GENERIC,
    ICON_TYPE_FILE_ELF,
    ICON_TYPE_FILE_ASM,
    ICON_TYPE_FILE_TXT
} icon_type_t;

typedef struct {
    char name[MAX_ICON_NAME_LEN];
    char full_path[256];
    icon_type_t type;
    int x;
    int y;
    uint8_t is_directory;
    uint32_t size;
    uint8_t selected;
} desktop_icon_t;

typedef struct {
    desktop_icon_t icons[MAX_DESKTOP_ICONS];
    int count;
    int selected_index;
} desktop_state_t;

typedef enum {
    CONTEXT_MENU_CREATE_FILE,
    CONTEXT_MENU_CREATE_FOLDER,
    CONTEXT_MENU_OPEN_TERMINAL,
    CONTEXT_MENU_NONE
} context_menu_item_t;

typedef struct {
    int x;
    int y;
    int visible;
    int selected_item;
} context_menu_t;

// Icon management functions
void desktop_init(desktop_state_t *state);
int desktop_load_directory(desktop_state_t *state, const char *path);
void desktop_sort_icons(desktop_state_t *state);
void desktop_arrange_icons(desktop_state_t *state, int screen_width, int screen_height, int taskbar_height);

// Drawing functions
void desktop_draw_icon(desktop_icon_t *icon);
void desktop_draw_icons(desktop_state_t *state);

// Selection functions
int desktop_icon_at_position(desktop_state_t *state, int mouse_x, int mouse_y);
void desktop_select_icon(desktop_state_t *state, int index);

// Context menu functions
void context_menu_init(context_menu_t *menu);
void context_menu_show(context_menu_t *menu, int x, int y);
void context_menu_hide(context_menu_t *menu);
void context_menu_draw(context_menu_t *menu);
int context_menu_get_item_at(context_menu_t *menu, int mouse_x, int mouse_y);
void context_menu_update_hover(context_menu_t *menu, int mouse_x, int mouse_y);

#endif