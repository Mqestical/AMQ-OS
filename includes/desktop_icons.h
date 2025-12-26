#ifndef DESKTOP_ICONS_H
#define DESKTOP_ICONS_H

#include <stdint.h>
#include "vfs.h"

#define ICON_WIDTH 48
#define ICON_HEIGHT 48
#define ICON_SPACING 20
#define ICON_TEXT_OFFSET 52
#define MAX_DESKTOP_ICONS 64
#define MAX_ICON_NAME_LEN 16

typedef enum {
    ICON_TYPE_FOLDER,
    ICON_TYPE_FILE_ELF,
    ICON_TYPE_FILE_ASM,
    ICON_TYPE_FILE_TXT,
    ICON_TYPE_FILE_GENERIC
} icon_type_t;

typedef struct {
    char name[MAX_ICON_NAME_LEN];
    char full_path[256];
    icon_type_t type;
    int x;
    int y;
    uint8_t is_directory;
    uint32_t size;
    int selected;
} desktop_icon_t;

typedef struct {
    desktop_icon_t icons[MAX_DESKTOP_ICONS];
    int count;
    int selected_index;
} desktop_state_t;

// Initialize desktop icons
void desktop_init(desktop_state_t *state);

// Load icons from a directory
int desktop_load_directory(desktop_state_t *state, const char *path);

// Sort icons (folders first, then files alphabetically)
void desktop_sort_icons(desktop_state_t *state);

// Arrange icons in grid layout
void desktop_arrange_icons(desktop_state_t *state, int screen_width, int screen_height, int taskbar_height);

// Draw all icons
void desktop_draw_icons(desktop_state_t *state);

// Draw a single icon
void desktop_draw_icon(desktop_icon_t *icon);

// Check if mouse is over an icon
int desktop_icon_at_position(desktop_state_t *state, int mouse_x, int mouse_y);

// Handle icon selection
void desktop_select_icon(desktop_state_t *state, int index);

// Get icon type from filename
icon_type_t get_icon_type_from_name(const char *name, uint8_t is_directory);

#endif // DESKTOP_ICONS_H