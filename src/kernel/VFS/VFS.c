
#include "vfs.h"
#include "memory.h"
#include "print.h"
#include "tinyfs.h"
#include "string_helpers.h"

static vfs_node_t *root_node = NULL;
static vfs_node_t *current_dir = NULL;
static char current_path[256] = "/";
static file_descriptor_t vfs_fd_table[MAX_OPEN_FILES];
static filesystem_t *registered_filesystems[16];
static int num_filesystems = 0;

int str_len(const char *str) {
    int len = 0;
    while (str[len]) len++;
    return len;
}

static int str_cmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

static void str_cpy(char *dest, const char *src, int max_len) {
    int i;
    for (i = 0; i < max_len - 1 && src[i]; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

void vfs_init(void) {
    PRINT(WHITE, BLACK,"[VFS] vfs_init called\n");
    PRINT(WHITE, BLACK, "[VFS] Initial root_node value: %p\n", root_node);
    PRINT(WHITE, BLACK, "[VFS] Initial num_filesystems value: %d\n", num_filesystems);
    
    root_node = NULL;
    num_filesystems = 0;
    
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        vfs_fd_table[i].used = 0;
        vfs_fd_table[i].node = NULL;
        vfs_fd_table[i].position = 0;
        vfs_fd_table[i].flags = 0;
    }
    
    for (int i = 0; i < 16; i++) {
        registered_filesystems[i] = NULL;
    }

    PRINT(MAGENTA, BLACK, "[VFS] Initialized\n");
}

int vfs_register_filesystem(filesystem_t *fs) {
    if (!fs || !fs->name) return -1;
    if (num_filesystems >= 16) return -1;
    
    registered_filesystems[num_filesystems++] = fs;
    
    PRINT(MAGENTA, BLACK, "[VFS] Registered filesystem: %s\n", fs->name);
    
    return 0;
}

vfs_node_t* vfs_get_root(void) {
    return root_node;
}

void vfs_debug_root(void) {
    if (!root_node) {
        PRINT(YELLOW, BLACK, "[VFS DEBUG] root_node is NULL\n");
    } else {
        PRINT(MAGENTA, BLACK, "[VFS DEBUG] root_node at %p\n", root_node);
        PRINT(MAGENTA, BLACK, "[VFS DEBUG]   name: '%s'\n", root_node->name);
        PRINT(MAGENTA, BLACK, "[VFS DEBUG]   type: %d\n", root_node->type);
        PRINT(MAGENTA, BLACK, "[VFS DEBUG]   fs: %p\n", root_node->fs);
        PRINT(MAGENTA, BLACK, "[VFS DEBUG]   ops: %p\n", root_node->ops);
        
        if (root_node->fs) {
            PRINT(MAGENTA, BLACK, "[VFS DEBUG]   fs->name: '%s'\n", root_node->fs->name);
            PRINT(MAGENTA, BLACK, "[VFS DEBUG]   fs->ops: %p\n", root_node->fs->ops);
            PRINT(MAGENTA, BLACK, "[VFS DEBUG]   fs->private_data: %p\n", root_node->fs->private_data);
        }
    }
}

int vfs_mount(const char *fs_type, const char *device, const char *mountpoint) {
    PRINT(WHITE, BLACK, "[VFS] === vfs_mount START ===\n");
    PRINT(WHITE, BLACK, "[VFS] fs_type='%s', device='%s', mountpoint='%s'\n", fs_type, device, mountpoint);
    PRINT(WHITE, BLACK, "[VFS] root_node BEFORE mount: %p\n", root_node);
    PRINT(WHITE, BLACK, "[VFS] DEBUG: num_filesystems = %d\n", num_filesystems);
    
    PRINT(WHITE, BLACK, "[VFS MOUNT START] Verifying registered filesystems:\n");
    for (int i = 0; i < num_filesystems; i++) {
        PRINT(WHITE, BLACK, "[VFSX_DEBUG]   fs[%d] at %p, name at %p: '%s'\n", i, 
               registered_filesystems[i],
               registered_filesystems[i]->name,
               registered_filesystems[i]->name);
    }

    PRINT(WHITE, BLACK, "[VFS] Registered filesystems:\n");
    for (int i = 0; i < num_filesystems; i++) {
        if (registered_filesystems[i]) {
            PRINT(WHITE, BLACK, "[VFS]  [%d] '%s' at %p\n", i, 
                   registered_filesystems[i]->name, 
                   registered_filesystems[i]);
        }
    }
    
    filesystem_t *fs = NULL;
    for (int i = 0; i < num_filesystems; i++) {
        PRINT(WHITE, BLACK, "[VFS] Checking index %d: %p\n", i, registered_filesystems[i]);
        PRINT(WHITE, BLACK, "[VFS-DEBUG] fs_type[0]='%c' fs_type addr=%p\n", fs_type[0], fs_type);

        PRINT(WHITE, BLACK, "[VFS-DEBUG] name[0]='%c' name addr=%p\n", registered_filesystems[i]->name[0], 
           registered_filesystems[i]->name);

        if (registered_filesystems[i] != NULL && registered_filesystems[i]->name != NULL) {
            PRINT(WHITE, BLACK, "[VFS] Comparing '%s' with '%s'\n", fs_type, registered_filesystems[i]->name);
            
            if (str_cmp(fs_type, registered_filesystems[i]->name) == 0) {
                fs = registered_filesystems[i];
                PRINT(MAGENTA, BLACK, "[VFS] Found filesystem type: %s\n", fs_type);
                break;
            }
        }
    }
    
    if (!fs) {
        PRINT(YELLOW, BLACK, "[VFS] Filesystem type not found: %s\n", fs_type);
        return -1;
    }
    
    PRINT(WHITE, BLACK,"[VFS] Calling fs->ops->mount()...\n");
    
    if (fs->ops->mount(fs, device) != 0) {
        PRINT(YELLOW, BLACK, "[VFS] Failed to mount %s on device %s\n", fs_type, device);
        return -1;
    }
    
    PRINT(MAGENTA, BLACK, "[VFS] Mount operation successful\n");
    
    if (mountpoint[0] == '/' && mountpoint[1] == '\0') {
        PRINT(WHITE, BLACK, "[VFS] Getting root node from filesystem...\n");
        root_node = fs->ops->get_root(fs);
        PRINT(WHITE, BLACK, "[VFS] root_node AFTER get_root: %p\n", root_node);
        
        if (!root_node) {
            PRINT(YELLOW, BLACK, "[VFS] CRITICAL: get_root returned NULL!\n");
            return -1;
        }
        
        root_node->fs = fs;
        current_dir = root_node;
        PRINT(MAGENTA, BLACK, "[VFS] Root node set successfully:\n");
        PRINT(MAGENTA, BLACK, "[VFS]   root_node = %p\n", root_node);
        PRINT(MAGENTA, BLACK, "[VFS]   root_node->fs = %p\n", root_node->fs);
        PRINT(MAGENTA, BLACK, "[VFS]   root_node->ops = %p\n", root_node->ops);
        PRINT(MAGENTA, BLACK, "[VFS]   root_node->name = '%s'\n", root_node->name);
        PRINT(MAGENTA, BLACK, "[VFS]   root_node->type = %d\n", root_node->type);
    }
    
    PRINT(MAGENTA, BLACK, "[VFS] Mounted %s at %s\n", fs_type, mountpoint);
    
    return 0;
}

static int allocate_fd(void) {
    for (int i = 3; i < MAX_OPEN_FILES; i++) {
        if (!vfs_fd_table[i].used) {
            vfs_fd_table[i].used = 1;
            return i;
        }
    }
    return -1;
}

static void tokenize_path(const char *path, char tokens[][MAX_FILENAME], int *token_count) {
    *token_count = 0;
    int token_idx = 0;
    int char_idx = 0;
    
    int start = (path[0] == '/') ? 1 : 0;
    
    for (int i = start; path[i]; i++) {
        if (path[i] == '/') {
            if (char_idx > 0) {
                tokens[token_idx][char_idx] = '\0';
                token_idx++;
                char_idx = 0;
            }
        } else {
            if (char_idx < MAX_FILENAME - 1) {
                tokens[token_idx][char_idx++] = path[i];
            }
        }
    }
    
    if (char_idx > 0) {
        tokens[token_idx][char_idx] = '\0';
        token_idx++;
    }
    
    *token_count = token_idx;
}

vfs_node_t* vfs_resolve_path(const char *path) {
    PRINT(WHITE, BLACK, "[VFS] resolve_path: '%s'\n", path);
    
    if (!root_node) {
        PRINT(YELLOW, BLACK, "[VFS] resolve_path: root_node is NULL!\n");
        return NULL;
    }
    
    PRINT(MAGENTA, BLACK, "[VFS] resolve_path: root_node = %p\n", root_node);
    
    if (!path) {
        PRINT(YELLOW, BLACK, "[VFS] resolve_path: path is NULL\n");
        return NULL;
    }
    
    if (path[0] == '/' && path[1] == '\0') {
        PRINT(MAGENTA, BLACK, "[VFS] resolve_path: returning root\n");
        return root_node;
    }
    
    if (path[0] != '/') {
        PRINT(YELLOW, BLACK, "[VFS] resolve_path: path must be absolute: %s\n", path);
        return NULL;
    }
    
    char tokens[32][MAX_FILENAME];
    int token_count = 0;
    tokenize_path(path, tokens, &token_count);
    
    PRINT(WHITE, BLACK, "[VFS] resolve_path: %d path components\n", token_count);
    
    vfs_node_t *current = root_node;
    
    for (int i = 0; i < token_count; i++) {
        PRINT(WHITE, BLACK, "[VFS] resolve_path: looking for '%s'\n", tokens[i]);
        
        if (!current) {
            PRINT(YELLOW, BLACK, "[VFS] resolve_path: current node is NULL at component '%s'\n", tokens[i]);
            return NULL;
        }
        
        if (current->type != FILE_TYPE_DIRECTORY) {
            PRINT(YELLOW, BLACK, "[VFS] resolve_path: '%s' is not a directory\n", tokens[i]);
            return NULL;
        }
        
        current = vfs_finddir(current, tokens[i]);
        if (!current) {
            PRINT(YELLOW, BLACK, "[VFS] resolve_path: component not found: '%s'\n", tokens[i]);
            return NULL;
        }
        PRINT(MAGENTA, BLACK, "[VFS] resolve_path: found '%s'\n", tokens[i]);
    }
    
    PRINT(MAGENTA, BLACK, "[VFS] resolve_path: success, returning %p\n", current);
    return current;
}

int vfs_open(const char *path, uint32_t flags) {
    vfs_node_t *node = vfs_resolve_path(path);
    if (!node) return -1;

    int fd = allocate_fd();
    if (fd < 0) return -1;

    vfs_fd_table[fd].node = node;
    vfs_fd_table[fd].position = 0;
    vfs_fd_table[fd].flags = flags;
    
    if (node->ops && node->ops->open) {
        node->ops->open(node, flags);
    }
    
    return fd;
}

int vfs_close(int fd) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || !vfs_fd_table[fd].used) {
        return -1;
    }
    
    vfs_node_t *node = vfs_fd_table[fd].node;
    
    if (node && node->ops && node->ops->close) {
        node->ops->close(node);
    }
    
    vfs_fd_table[fd].used = 0;
    vfs_fd_table[fd].node = NULL;
    vfs_fd_table[fd].position = 0;
    
    return 0;
}

int vfs_read(int fd, uint8_t *buffer, uint32_t size) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || !vfs_fd_table[fd].used) {
        return -1;
    }
    
    vfs_node_t *node = vfs_fd_table[fd].node;
    if (!node || !node->ops || !node->ops->read) {
        return -1;
    }
    
    int bytes_read = node->ops->read(node, buffer, size, vfs_fd_table[fd].position);
    
    if (bytes_read > 0) {
        vfs_fd_table[fd].position += bytes_read;
    }
    
    return bytes_read;
}

int vfs_write(int fd, uint8_t *buffer, uint32_t size) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || !vfs_fd_table[fd].used) {
        return -1;
    }
    
    vfs_node_t *node = vfs_fd_table[fd].node;
    if (!node || !node->ops || !node->ops->write) {
        return -1;
    }
    
    int bytes_written = node->ops->write(node, buffer, size, vfs_fd_table[fd].position);
    
    if (bytes_written > 0) {
        vfs_fd_table[fd].position += bytes_written;
    }
    
    return bytes_written;
}

int vfs_seek(int fd, int offset, int whence) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || !vfs_fd_table[fd].used) {
        return -1;
    }
    
    uint32_t new_pos = vfs_fd_table[fd].position;
    
    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos += offset;
            break;
        case SEEK_END:
            new_pos = vfs_fd_table[fd].node->size + offset;
            break;
        default:
            return -1;
    }
    
    vfs_fd_table[fd].position = new_pos;
    return new_pos;
}

vfs_node_t* vfs_readdir(vfs_node_t *node, uint32_t index) {
    if (!node || node->type != FILE_TYPE_DIRECTORY) return NULL;
    if (!node->ops || !node->ops->readdir) return NULL;
    
    return node->ops->readdir(node, index);
}

vfs_node_t* vfs_finddir(vfs_node_t *node, const char *name) {
    if (!node || node->type != FILE_TYPE_DIRECTORY) return NULL;
    if (!node->ops || !node->ops->finddir) return NULL;
    
    return node->ops->finddir(node, name);
}

int vfs_create(const char *path, uint32_t permissions) {
    PRINT(WHITE, BLACK, "[VFS] vfs_create: '%s'\n", path);
    
    if (!path || path[0] != '/') {
        PRINT(YELLOW, BLACK, "[VFS] vfs_create: Invalid path\n");
        return -1;
    }
    
    char parent_path[256];
    char filename[MAX_FILENAME];
    
    int last_slash = -1;
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/') last_slash = i;
    }
    
    if (last_slash < 0) {
        PRINT(YELLOW, BLACK, "[VFS] vfs_create: No slash in path\n");
        return -1;
    }
    
    if (last_slash == 0) {
        parent_path[0] = '/';
        parent_path[1] = '\0';
    } else {
        for (int i = 0; i < last_slash; i++) {
            parent_path[i] = path[i];
        }
        parent_path[last_slash] = '\0';
    }
    
    int j = 0;
    for (int i = last_slash + 1; path[i] && j < MAX_FILENAME - 1; i++) {
        filename[j++] = path[i];
    }
    filename[j] = '\0';
    
    PRINT(WHITE, BLACK, "[VFS] vfs_create: parent='%s', filename='%s'\n", parent_path, filename);
    
    vfs_node_t *parent = vfs_resolve_path(parent_path);
    if (!parent) {
        PRINT(YELLOW, BLACK, "[VFS] vfs_create: Parent directory not found: %s\n", parent_path);
        return -1;
    }
    
    PRINT(MAGENTA, BLACK, "[VFS] vfs_create: parent found at %p\n", parent);
    
    if (parent->type != FILE_TYPE_DIRECTORY) {
        PRINT(YELLOW, BLACK, "[VFS] vfs_create: Parent is not a directory\n");
        return -1;
    }
    
    if (!parent->ops || !parent->ops->create) {
        PRINT(YELLOW, BLACK, "[VFS] vfs_create: Parent has no create operation\n");
        return -1;
    }

    PRINT(WHITE, BLACK, "[VFS] vfs_create: Calling parent->ops->create()...\n");
    
    int result = parent->ops->create(parent, filename, FILE_TYPE_REGULAR, permissions);
    
    if (result == 0) {
        PRINT(MAGENTA, BLACK, "[VFS] vfs_create: SUCCESS\n");
    } else {
        PRINT(YELLOW, BLACK, "[VFS] vfs_create: FAILED\n");
    }
    
    return result;
}

int vfs_mkdir(const char *path, uint32_t permissions) {
    PRINT(WHITE, BLACK, "[VFS] vfs_mkdir: '%s'\n", path);
    
    if (!path || path[0] != '/') {
        PRINT(YELLOW, BLACK, "[VFS] vfs_mkdir: Invalid path\n");
        return -1;
    }
    
    char parent_path[256];
    char dirname[MAX_FILENAME];
    
    int last_slash = -1;
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/') last_slash = i;
    }
    
    if (last_slash < 0) {
        PRINT(YELLOW, BLACK, "[VFS] vfs_mkdir: No slash in path\n");
        return -1;
    }
    
    if (last_slash == 0) {
        parent_path[0] = '/';
        parent_path[1] = '\0';
    } else {
        for (int i = 0; i < last_slash; i++) {
            parent_path[i] = path[i];
        }
        parent_path[last_slash] = '\0';
    }
    
    int j = 0;
    for (int i = last_slash + 1; path[i] && j < MAX_FILENAME - 1; i++) {
        dirname[j++] = path[i];
    }
    dirname[j] = '\0';

    PRINT(WHITE, BLACK, "[VFS] vfs_mkdir: parent='%s', dirname='%s'\n", parent_path, dirname);
    
    vfs_node_t *parent = vfs_resolve_path(parent_path);
    if (!parent) {
        PRINT(YELLOW, BLACK, "[VFS] vfs_mkdir: Parent directory not found: %s\n", parent_path);
        return -1;
    }

    PRINT(MAGENTA, BLACK, "[VFS] vfs_mkdir: parent found at %p\n", parent);
    
    if (parent->type != FILE_TYPE_DIRECTORY) {
        PRINT(YELLOW, BLACK, "[VFS] vfs_mkdir: Parent is not a directory\n");
        return -1;
    }
    
    if (!parent->ops || !parent->ops->create) {
        PRINT(YELLOW, BLACK, "[VFS] vfs_mkdir: Parent has no create operation\n");
        return -1;
    }

    PRINT(WHITE, BLACK, "[VFS] vfs_mkdir: Calling parent->ops->create()...\n");
    
    int result = parent->ops->create(parent, dirname, FILE_TYPE_DIRECTORY, permissions);
    
    if (result == 0) {
        PRINT(MAGENTA, BLACK, "[VFS] vfs_mkdir: SUCCESS\n");
    } else {
        PRINT(YELLOW, BLACK, "[VFS] vfs_mkdir: FAILED\n");
    }
    
    return result;
}

int vfs_unlink(const char *path) {
    if (!path || path[0] != '/') return -1;
    
    char parent_path[256];
    char name[MAX_FILENAME];
    
    int last_slash = -1;
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/') last_slash = i;
    }
    
    if (last_slash < 0) return -1;
    
    if (last_slash == 0) {
        parent_path[0] = '/';
        parent_path[1] = '\0';
    } else {
        for (int i = 0; i < last_slash; i++) {
            parent_path[i] = path[i];
        }
        parent_path[last_slash] = '\0';
    }
    
    int j = 0;
    for (int i = last_slash + 1; path[i] && j < MAX_FILENAME - 1; i++) {
        name[j++] = path[i];
    }
    name[j] = '\0';
    
    vfs_node_t *parent = vfs_resolve_path(parent_path);
    if (!parent || parent->type != FILE_TYPE_DIRECTORY) return -1;
    
    if (!parent->ops || !parent->ops->unlink) return -1;
    
    return parent->ops->unlink(parent, name);
}

void vfs_list_directory(const char *path) {
    PRINT(WHITE, BLACK, "[VFS] list_directory: '%s'\n", path);
    
    vfs_node_t *dir = vfs_resolve_path(path);
    if (!dir) {
        PRINT(YELLOW, BLACK, "Directory not found: %s\n", path);
        return;
    }
    
    if (dir->type != FILE_TYPE_DIRECTORY) {
        PRINT(YELLOW, BLACK, "Not a directory: %s\n", path);
        return;
    }

    PRINT(WHITE, BLACK, "Contents of %s:\n", path);
    
    uint32_t i = 0;
    vfs_node_t *entry;
    int count = 0;
    
    while ((entry = vfs_readdir(dir, i++)) != NULL) {
        char type = (entry->type == FILE_TYPE_DIRECTORY) ? 'd' : 'f';
        PRINT(WHITE, BLACK, "  [%c] %s %d bytes\n", type, entry->name, entry->size);
        count++;
        
        kfree(entry);
    }
    
    if (count == 0) {
        PRINT(WHITE, BLACK, "  (empty)\n");
    }
}

int vfs_statfs(const char *path, fs_stats_t *stats) {
    vfs_node_t *node = vfs_resolve_path(path);
    if (!node || !node->fs) return -1;
    
    if (node->fs->ops && node->fs->ops->get_stats) {
        return node->fs->ops->get_stats(node->fs, stats);
    }
    
    return -1;
}

vfs_node_t* vfs_get_cwd(void) {
    return current_dir ? current_dir : root_node;
}

const char* vfs_get_cwd_path(void) {
    return current_path;
}

int vfs_chdir(const char *path) {
    if (!path) return -1;
    
    vfs_node_t *target;
    char full_path[256];
    
    if (path[0] == '/') {
        str_cpy(full_path, path, 256);
        target = vfs_resolve_path(path);
    } else {
        int i = 0;
        while (current_path[i] && i < 255) {
            full_path[i] = current_path[i];
            i++;
        }
        
        if (i > 0 && full_path[i-1] != '/') {
            full_path[i++] = '/';
        }
        
        int j = 0;
        while (path[j] && i < 255) {
            full_path[i++] = path[j++];
        }
        full_path[i] = '\0';
        
        target = vfs_resolve_path(full_path);
    }
    
    if (!target) {
        PRINT(YELLOW, BLACK, "cd: directory not found: %s\n", path);
        return -1;
    }
    
    if (target->type != FILE_TYPE_DIRECTORY) {
        PRINT(YELLOW, BLACK, "cd: not a directory: %s\n", path);
        return -1;
    }
    
    current_dir = target;
    
    int i = 0;
    while (full_path[i] && i < 255) {
        current_path[i] = full_path[i];
        i++;
    }
    current_path[i] = '\0';
    
    if (i > 1 && current_path[i-1] == '/') {
        current_path[i-1] = '\0';
    }
    
    return 0;
}