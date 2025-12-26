#include "desktop_icons.h"
#include "print.h"
#include "vfs.h"
#include "string_helpers.h"
#include "memory.h"

// Helper function to copy strings safely
static void safe_strcpy(char *dest, const char *src, int max_len) {
    int i;
    for (i = 0; i < max_len - 1 && src[i]; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

// Helper function to compare strings
static int safe_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

// Helper function to check if string ends with suffix
static int str_ends_with(const char *str, const char *suffix) {
    int str_len = 0, suffix_len = 0;
    
    while (str[str_len]) str_len++;
    while (suffix[suffix_len]) suffix_len++;
    
    if (suffix_len > str_len) return 0;
    
    for (int i = 0; i < suffix_len; i++) {
        if (str[str_len - suffix_len + i] != suffix[i]) {
            return 0;
        }
    }
    return 1;
}

// Get icon type based on filename and directory status
icon_type_t get_icon_type_from_name(const char *name, uint8_t is_directory) {
    if (is_directory) {
        return ICON_TYPE_FOLDER;
    }
    
    // Check file extensions
    if (str_ends_with(name, ".elf") || str_ends_with(name, ".ELF")) {
        return ICON_TYPE_FILE_ELF;
    }
    if (str_ends_with(name, ".asm") || str_ends_with(name, ".s") || 
        str_ends_with(name, ".ASM") || str_ends_with(name, ".S")) {
        return ICON_TYPE_FILE_ASM;
    }
    if (str_ends_with(name, ".txt") || str_ends_with(name, ".TXT")) {
        return ICON_TYPE_FILE_TXT;
    }
    
    return ICON_TYPE_FILE_GENERIC;
}

// Initialize desktop state
void desktop_init(desktop_state_t *state) {
    state->count = 0;
    state->selected_index = -1;
    
    for (int i = 0; i < MAX_DESKTOP_ICONS; i++) {
        state->icons[i].name[0] = '\0';
        state->icons[i].full_path[0] = '\0';
        state->icons[i].type = ICON_TYPE_FILE_GENERIC;
        state->icons[i].x = 0;
        state->icons[i].y = 0;
        state->icons[i].is_directory = 0;
        state->icons[i].size = 0;
        state->icons[i].selected = 0;
    }
}

// Load icons from VFS directory
int desktop_load_directory(desktop_state_t *state, const char *path) {
    vfs_node_t *dir = vfs_resolve_path(path);
    if (!dir) {
        PRINT(YELLOW, BLACK, "[DESKTOP] Failed to resolve path: %s\n", path);
        return -1;
    }
    
    if (dir->type != FILE_TYPE_DIRECTORY) {
        PRINT(YELLOW, BLACK, "[DESKTOP] Not a directory: %s\n", path);
        return -1;
    }
    
    desktop_init(state);
    
    uint32_t index = 0;
    vfs_node_t *entry;
    
    while ((entry = vfs_readdir(dir, index++)) != NULL && state->count < MAX_DESKTOP_ICONS) {
        desktop_icon_t *icon = &state->icons[state->count];
        
        // Copy name (truncate if needed)
        safe_strcpy(icon->name, entry->name, MAX_ICON_NAME_LEN);
        
        // Build full path
        int path_len = 0;
        while (path[path_len]) path_len++;
        
        int i = 0;
        for (i = 0; i < path_len && i < 255; i++) {
            icon->full_path[i] = path[i];
        }
        
        if (i > 0 && icon->full_path[i-1] != '/') {
            icon->full_path[i++] = '/';
        }
        
        int j = 0;
        while (entry->name[j] && i < 255) {
            icon->full_path[i++] = entry->name[j++];
        }
        icon->full_path[i] = '\0';
        
        icon->is_directory = (entry->type == FILE_TYPE_DIRECTORY) ? 1 : 0;
        icon->size = entry->size;
        icon->type = get_icon_type_from_name(entry->name, icon->is_directory);
        icon->selected = 0;
        
        state->count++;
        kfree(entry);
    }
    
    PRINT(MAGENTA, BLACK, "[DESKTOP] Loaded %d icons from %s\n", state->count, path);
    return state->count;
}

// Comparison function for sorting
static int compare_icons(desktop_icon_t *a, desktop_icon_t *b) {
    // Directories come first
    if (a->is_directory && !b->is_directory) return -1;
    if (!a->is_directory && b->is_directory) return 1;
    
    // Within same type, sort alphabetically (case-insensitive)
    return safe_strcmp(a->name, b->name);
}

// Bubble sort implementation for icons
void desktop_sort_icons(desktop_state_t *state) {
    if (state->count <= 1) return;
    
    for (int i = 0; i < state->count - 1; i++) {
        for (int j = 0; j < state->count - i - 1; j++) {
            if (compare_icons(&state->icons[j], &state->icons[j + 1]) > 0) {
                // Swap icons
                desktop_icon_t temp = state->icons[j];
                state->icons[j] = state->icons[j + 1];
                state->icons[j + 1] = temp;
            }
        }
    }
    
    PRINT(MAGENTA, BLACK, "[DESKTOP] Sorted %d icons\n", state->count);
}

// Arrange icons in grid layout (top-to-bottom, then left-to-right)
void desktop_arrange_icons(desktop_state_t *state, int screen_width, int screen_height, int taskbar_height) {
    int start_x = 20;
    int start_y = 20;
    int current_x = start_x;
    int current_y = start_y;
    int column_width = ICON_WIDTH + ICON_SPACING;
    int row_height = ICON_HEIGHT + ICON_TEXT_OFFSET + 10;
    int max_y = screen_height - taskbar_height - row_height;
    
    for (int i = 0; i < state->count; i++) {
        state->icons[i].x = current_x;
        state->icons[i].y = current_y;
        
        current_y += row_height;
        
        // Move to next column if we exceed screen height
        if (current_y > max_y) {
            current_y = start_y;
            current_x += column_width;
        }
    }
    
    PRINT(MAGENTA, BLACK, "[DESKTOP] Arranged %d icons\n", state->count);
}

// Draw folder icon (48x48)
static void draw_folder_icon(int x, int y, uint32_t color) {
    uint32_t folder_color = 0xFFD700;  // Gold color for folders
    uint32_t dark_folder = 0xDAA520;   // Darker gold for depth
    
    if (color != 0) {
        folder_color = color;
        dark_folder = color & 0xCCCCCC;
    }
    
    // Folder tab (top left)
    for (int dy = 0; dy < 8; dy++) {
        for (int dx = 0; dx < 20; dx++) {
            put_pixel(x + dx + 2, y + dy + 8, dark_folder);
        }
    }
    
    // Main folder body
    for (int dy = 0; dy < 28; dy++) {
        for (int dx = 0; dx < 44; dx++) {
            put_pixel(x + dx + 2, y + dy + 16, folder_color);
        }
    }
    
    // Border
    for (int dx = 0; dx < 44; dx++) {
        put_pixel(x + dx + 2, y + 16, dark_folder);
        put_pixel(x + dx + 2, y + 43, dark_folder);
    }
    for (int dy = 0; dy < 28; dy++) {
        put_pixel(x + 2, y + dy + 16, dark_folder);
        put_pixel(x + 45, y + dy + 16, dark_folder);
    }
}

// Draw file icon (48x48)
static void draw_file_icon(int x, int y, uint32_t color) {
    uint32_t paper_color = 0xFFFFFF;  // White
    uint32_t border_color = 0x808080; // Gray
    
    // File body
    for (int dy = 0; dy < 40; dy++) {
        for (int dx = 0; dx < 32; dx++) {
            put_pixel(x + dx + 8, y + dy + 4, paper_color);
        }
    }
    
    // Folded corner
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j <= i; j++) {
            put_pixel(x + 32 + j, y + 4 + i, border_color);
        }
    }
    
    // Border
    for (int dx = 0; dx < 32; dx++) {
        put_pixel(x + dx + 8, y + 4, border_color);
        put_pixel(x + dx + 8, y + 43, border_color);
    }
    for (int dy = 0; dy < 40; dy++) {
        put_pixel(x + 8, y + dy + 4, border_color);
        if (dy > 8) put_pixel(x + 39, y + dy + 4, border_color);
    }
    
    // Colored accent based on file type
    if (color != 0) {
        for (int dy = 0; dy < 4; dy++) {
            for (int dx = 0; dx < 20; dx++) {
                put_pixel(x + dx + 14, y + dy + 16, color);
            }
        }
    }
}

// Draw ELF executable icon
static void draw_elf_icon(int x, int y) {
    draw_file_icon(x, y, 0x00FF00);  // Green accent for executables
    
    // Draw "EXE" text
    char exec_text[] = "EXE";
    draw_string(x + 14, y + 24, exec_text, 0x000000, 0x00FF00);
}

// Draw ASM file icon
static void draw_asm_icon(int x, int y) {
    draw_file_icon(x, y, 0x0080FF);  // Blue accent for assembly
    
    // Draw "AS" text
    char asm_text[] = "AS";
    draw_string(x + 14, y + 24, asm_text, 0x000000, 0x0080FF);
}

// Draw TXT file icon
static void draw_txt_icon(int x, int y) {
    draw_file_icon(x, y, 0xFFFFFF);  // White
    
    // Draw text lines
    for (int i = 0; i < 3; i++) {
        for (int dx = 0; dx < 16; dx++) {
            put_pixel(x + 12 + dx, y + 16 + i * 6, 0x808080);
        }
    }
}

// Draw generic file icon
static void draw_generic_icon(int x, int y) {
    draw_file_icon(x, y, 0x808080);  // Gray accent
}

// Draw a single icon with text label
void desktop_draw_icon(desktop_icon_t *icon) {
    // Draw icon based on type
    switch (icon->type) {
        case ICON_TYPE_FOLDER:
            draw_folder_icon(icon->x, icon->y, icon->selected ? 0xFFAA00 : 0);
            break;
        case ICON_TYPE_FILE_ELF:
            draw_elf_icon(icon->x, icon->y);
            break;
        case ICON_TYPE_FILE_ASM:
            draw_asm_icon(icon->x, icon->y);
            break;
        case ICON_TYPE_FILE_TXT:
            draw_txt_icon(icon->x, icon->y);
            break;
        default:
            draw_generic_icon(icon->x, icon->y);
            break;
    }
    
    // Draw text label (centered under icon)
    uint32_t text_color = icon->selected ? 0xFFFFFF : 0x000000;
    uint32_t bg_color = icon->selected ? 0x0080FF : 0x008B8B;
    
    // Calculate text centering
    int name_len = 0;
    while (icon->name[name_len]) name_len++;
    
    int text_x = icon->x + (ICON_WIDTH / 2) - (name_len * 4);
    int text_y = icon->y + ICON_TEXT_OFFSET;
    
    draw_string(text_x, text_y, icon->name, text_color, bg_color);
}

// Draw all icons
void desktop_draw_icons(desktop_state_t *state) {
    for (int i = 0; i < state->count; i++) {
        desktop_draw_icon(&state->icons[i]);
    }
}

// Check if mouse position is over an icon
int desktop_icon_at_position(desktop_state_t *state, int mouse_x, int mouse_y) {
    for (int i = 0; i < state->count; i++) {
        desktop_icon_t *icon = &state->icons[i];
        
        // Check if mouse is within icon bounds (including text)
        if (mouse_x >= icon->x && mouse_x < icon->x + ICON_WIDTH &&
            mouse_y >= icon->y && mouse_y < icon->y + ICON_TEXT_OFFSET + 8) {
            return i;
        }
    }
    
    return -1;
}

// Select an icon by index
void desktop_select_icon(desktop_state_t *state, int index) {
    // Deselect all icons
    for (int i = 0; i < state->count; i++) {
        state->icons[i].selected = 0;
    }
    
    // Select the specified icon
    if (index >= 0 && index < state->count) {
        state->icons[index].selected = 1;
        state->selected_index = index;
    } else {
        state->selected_index = -1;
    }
}

// Context menu functions
void context_menu_init(context_menu_t *menu) {
    menu->x = 0;
    menu->y = 0;
    menu->visible = 0;
    menu->selected_item = -1;
}

void context_menu_show(context_menu_t *menu, int x, int y) {
    menu->x = x;
    menu->y = y;
    menu->visible = 1;
    menu->selected_item = -1;
}

void context_menu_hide(context_menu_t *menu) {
    menu->visible = 0;
    menu->selected_item = -1;
}

void context_menu_draw(context_menu_t *menu) {
    if (!menu->visible) return;
    
    uint32_t bg_color = 0xF0F0F0;
    uint32_t border_color = 0x808080;
    uint32_t text_color = 0x000000;
    uint32_t hover_color = 0x0078D7;
    uint32_t hover_text = 0xFFFFFF;
    
    // Draw background
    for (int dy = 0; dy < CONTEXT_MENU_HEIGHT; dy++) {
        for (int dx = 0; dx < CONTEXT_MENU_WIDTH; dx++) {
            put_pixel(menu->x + dx, menu->y + dy, bg_color);
        }
    }
    
    // Draw border
    for (int dx = 0; dx < CONTEXT_MENU_WIDTH; dx++) {
        put_pixel(menu->x + dx, menu->y, border_color);
        put_pixel(menu->x + dx, menu->y + CONTEXT_MENU_HEIGHT - 1, border_color);
    }
    for (int dy = 0; dy < CONTEXT_MENU_HEIGHT; dy++) {
        put_pixel(menu->x, menu->y + dy, border_color);
        put_pixel(menu->x + CONTEXT_MENU_WIDTH - 1, menu->y + dy, border_color);
    }
    
    // Item 0: "Create File"
    int item_y = menu->y + 5;
    int text_x = menu->x + 10;
    int text_y = item_y + 8;
    
    if (menu->selected_item == 0) {
        for (int dy = 0; dy < CONTEXT_MENU_ITEM_HEIGHT - 2; dy++) {
            for (int dx = 2; dx < CONTEXT_MENU_WIDTH - 2; dx++) {
                put_pixel(menu->x + dx, item_y + dy, hover_color);
            }
        }
        draw_char(text_x, text_y, 'C', hover_text, hover_color);
        draw_char(text_x + 8, text_y, 'r', hover_text, hover_color);
        draw_char(text_x + 16, text_y, 'e', hover_text, hover_color);
        draw_char(text_x + 24, text_y, 'a', hover_text, hover_color);
        draw_char(text_x + 32, text_y, 't', hover_text, hover_color);
        draw_char(text_x + 40, text_y, 'e', hover_text, hover_color);
        draw_char(text_x + 48, text_y, ' ', hover_text, hover_color);
        draw_char(text_x + 56, text_y, 'F', hover_text, hover_color);
        draw_char(text_x + 64, text_y, 'i', hover_text, hover_color);
        draw_char(text_x + 72, text_y, 'l', hover_text, hover_color);
        draw_char(text_x + 80, text_y, 'e', hover_text, hover_color);
    } else {
        draw_char(text_x, text_y, 'C', text_color, bg_color);
        draw_char(text_x + 8, text_y, 'r', text_color, bg_color);
        draw_char(text_x + 16, text_y, 'e', text_color, bg_color);
        draw_char(text_x + 24, text_y, 'a', text_color, bg_color);
        draw_char(text_x + 32, text_y, 't', text_color, bg_color);
        draw_char(text_x + 40, text_y, 'e', text_color, bg_color);
        draw_char(text_x + 48, text_y, ' ', text_color, bg_color);
        draw_char(text_x + 56, text_y, 'F', text_color, bg_color);
        draw_char(text_x + 64, text_y, 'i', text_color, bg_color);
        draw_char(text_x + 72, text_y, 'l', text_color, bg_color);
        draw_char(text_x + 80, text_y, 'e', text_color, bg_color);
    }
    
    // Item 1: "Create Folder"
    item_y = menu->y + 5 + CONTEXT_MENU_ITEM_HEIGHT;
    text_y = item_y + 8;
    
    if (menu->selected_item == 1) {
        for (int dy = 0; dy < CONTEXT_MENU_ITEM_HEIGHT - 2; dy++) {
            for (int dx = 2; dx < CONTEXT_MENU_WIDTH - 2; dx++) {
                put_pixel(menu->x + dx, item_y + dy, hover_color);
            }
        }
        draw_char(text_x, text_y, 'C', hover_text, hover_color);
        draw_char(text_x + 8, text_y, 'r', hover_text, hover_color);
        draw_char(text_x + 16, text_y, 'e', hover_text, hover_color);
        draw_char(text_x + 24, text_y, 'a', hover_text, hover_color);
        draw_char(text_x + 32, text_y, 't', hover_text, hover_color);
        draw_char(text_x + 40, text_y, 'e', hover_text, hover_color);
        draw_char(text_x + 48, text_y, ' ', hover_text, hover_color);
        draw_char(text_x + 56, text_y, 'F', hover_text, hover_color);
        draw_char(text_x + 64, text_y, 'o', hover_text, hover_color);
        draw_char(text_x + 72, text_y, 'l', hover_text, hover_color);
        draw_char(text_x + 80, text_y, 'd', hover_text, hover_color);
        draw_char(text_x + 88, text_y, 'e', hover_text, hover_color);
        draw_char(text_x + 96, text_y, 'r', hover_text, hover_color);
    } else {
        draw_char(text_x, text_y, 'C', text_color, bg_color);
        draw_char(text_x + 8, text_y, 'r', text_color, bg_color);
        draw_char(text_x + 16, text_y, 'e', text_color, bg_color);
        draw_char(text_x + 24, text_y, 'a', text_color, bg_color);
        draw_char(text_x + 32, text_y, 't', text_color, bg_color);
        draw_char(text_x + 40, text_y, 'e', text_color, bg_color);
        draw_char(text_x + 48, text_y, ' ', text_color, bg_color);
        draw_char(text_x + 56, text_y, 'F', text_color, bg_color);
        draw_char(text_x + 64, text_y, 'o', text_color, bg_color);
        draw_char(text_x + 72, text_y, 'l', text_color, bg_color);
        draw_char(text_x + 80, text_y, 'd', text_color, bg_color);
        draw_char(text_x + 88, text_y, 'e', text_color, bg_color);
        draw_char(text_x + 96, text_y, 'r', text_color, bg_color);
    }
    
    // Item 2: "Open Terminal"
    item_y = menu->y + 5 + 2 * CONTEXT_MENU_ITEM_HEIGHT;
    text_y = item_y + 8;
    
    if (menu->selected_item == 2) {
        for (int dy = 0; dy < CONTEXT_MENU_ITEM_HEIGHT - 2; dy++) {
            for (int dx = 2; dx < CONTEXT_MENU_WIDTH - 2; dx++) {
                put_pixel(menu->x + dx, item_y + dy, hover_color);
            }
        }
        draw_char(text_x, text_y, 'O', hover_text, hover_color);
        draw_char(text_x + 8, text_y, 'p', hover_text, hover_color);
        draw_char(text_x + 16, text_y, 'e', hover_text, hover_color);
        draw_char(text_x + 24, text_y, 'n', hover_text, hover_color);
        draw_char(text_x + 32, text_y, ' ', hover_text, hover_color);
        draw_char(text_x + 40, text_y, 'T', hover_text, hover_color);
        draw_char(text_x + 48, text_y, 'e', hover_text, hover_color);
        draw_char(text_x + 56, text_y, 'r', hover_text, hover_color);
        draw_char(text_x + 64, text_y, 'm', hover_text, hover_color);
        draw_char(text_x + 72, text_y, 'i', hover_text, hover_color);
        draw_char(text_x + 80, text_y, 'n', hover_text, hover_color);
        draw_char(text_x + 88, text_y, 'a', hover_text, hover_color);
        draw_char(text_x + 96, text_y, 'l', hover_text, hover_color);
    } else {
        draw_char(text_x, text_y, 'O', text_color, bg_color);
        draw_char(text_x + 8, text_y, 'p', text_color, bg_color);
        draw_char(text_x + 16, text_y, 'e', text_color, bg_color);
        draw_char(text_x + 24, text_y, 'n', text_color, bg_color);
        draw_char(text_x + 32, text_y, ' ', text_color, bg_color);
        draw_char(text_x + 40, text_y, 'T', text_color, bg_color);
        draw_char(text_x + 48, text_y, 'e', text_color, bg_color);
        draw_char(text_x + 56, text_y, 'r', text_color, bg_color);
        draw_char(text_x + 64, text_y, 'm', text_color, bg_color);
        draw_char(text_x + 72, text_y, 'i', text_color, bg_color);
        draw_char(text_x + 80, text_y, 'n', text_color, bg_color);
        draw_char(text_x + 88, text_y, 'a', text_color, bg_color);
        draw_char(text_x + 96, text_y, 'l', text_color, bg_color);
    }
}

int context_menu_get_item_at(context_menu_t *menu, int mouse_x, int mouse_y) {
    if (!menu->visible) return -1;
    
    // Check if mouse is within menu bounds
    if (mouse_x < menu->x || mouse_x >= menu->x + CONTEXT_MENU_WIDTH ||
        mouse_y < menu->y || mouse_y >= menu->y + CONTEXT_MENU_HEIGHT) {
        return -1;
    }
    
    // Calculate which item was clicked
    int relative_y = mouse_y - menu->y - 5;
    if (relative_y < 0) return -1;
    
    int item = relative_y / CONTEXT_MENU_ITEM_HEIGHT;
    if (item >= 0 && item < 3) {
        return item;
    }
    
    return -1;
}

void context_menu_update_hover(context_menu_t *menu, int mouse_x, int mouse_y) {
    if (!menu->visible) {
        menu->selected_item = -1;
        return;
    }
    
    int item = context_menu_get_item_at(menu, mouse_x, mouse_y);
    if (item != menu->selected_item) {
        menu->selected_item = item;
    }
}