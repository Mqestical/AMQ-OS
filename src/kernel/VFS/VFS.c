#include "vfs.h"
#include "memory.h"
#include "print.h"

// Global VFS state
static vfs_node_t *root_node = NULL;
static file_descriptor_t vfs_fd_table[MAX_OPEN_FILES];
static filesystem_t *registered_filesystems[16];
static int num_filesystems = 0;

// String helper functions
static int str_len(const char *str) {
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
    // Clear file descriptor table
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        vfs_fd_table[i].used = 0;
        vfs_fd_table[i].node = NULL;
        vfs_fd_table[i].position = 0;
        vfs_fd_table[i].flags = 0;
    }
    
    // Clear filesystem registry
    for (int i = 0; i < 16; i++) {
        registered_filesystems[i] = NULL;
    }
    
    printk(0xFF00FF00, 0x000000, "VFS initialized\n");
}

int vfs_register_filesystem(filesystem_t *fs) {
    if (num_filesystems >= 16) return -1;
    
    registered_filesystems[num_filesystems++] = fs;
    printk(0xFF00FF00, 0x000000, "Registered filesystem: %s\n", fs->name);
    
    return 0;
}

vfs_node_t* vfs_get_root(void) {
    return root_node;
}

int vfs_mount(const char *fs_type, const char *device, const char *mountpoint) {
    // Find filesystem type
    filesystem_t *fs = NULL;
    for (int i = 0; i < num_filesystems; i++) {
        if (registered_filesystems[i]) {
            if (str_cmp(fs_type, registered_filesystems[i]->name) == 0) {
                fs = registered_filesystems[i];
                break;
            }
        }
    }
    
    if (!fs) {
        printk(0xFFFF0000, 0x000000, "Filesystem type not found: %s\n", fs_type);
        return -1;
    }
    
    // Mount the filesystem
    if (fs->ops->mount(fs, device) != 0) {
        printk(0xFFFF0000, 0x000000, "Failed to mount %s\n", device);
        return -1;
    }
    
    // Set root node if mounting to /
    if (mountpoint[0] == '/' && mountpoint[1] == '\0') {
        root_node = fs->ops->get_root(fs);
        if (!root_node) {
            printk(0xFFFF0000, 0x000000, "Failed to get root node\n");
            return -1;
        }
    }
    
    printk(0xFF00FF00, 0x000000, "Mounted %s at %s\n", fs_type, mountpoint);
    return 0;
}

// Allocate a file descriptor
static int allocate_fd(void) {
    for (int i = 3; i < MAX_OPEN_FILES; i++) {
        if (!vfs_fd_table[i].used) {
            vfs_fd_table[i].used = 1;
            return i;
        }
    }
    return -1;
}

// Simple path tokenizer
static void tokenize_path(const char *path, char tokens[][MAX_FILENAME], int *token_count) {
    *token_count = 0;
    int token_idx = 0;
    int char_idx = 0;
    
    for (int i = 0; path[i]; i++) {
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
    if (!root_node) return NULL;
    if (!path || path[0] != '/') return NULL;
    
    // Root directory
    if (path[1] == '\0') return root_node;
    
    char tokens[32][MAX_FILENAME];
    int token_count = 0;
    tokenize_path(path, tokens, &token_count);
    
    vfs_node_t *current = root_node;
    
    for (int i = 0; i < token_count; i++) {
        if (!current || current->type != FILE_TYPE_DIRECTORY) {
            return NULL;
        }
        
        current = vfs_finddir(current, tokens[i]);
        if (!current) return NULL;
    }
    
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
    if (!path || path[0] != '/') return -1;
    
    // Find parent directory
    char parent_path[256];
    char filename[MAX_FILENAME];
    
    int last_slash = -1;
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/') last_slash = i;
    }
    
    if (last_slash < 0) return -1;
    
    // Extract parent path and filename
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
    
    // Get parent directory
    vfs_node_t *parent = vfs_resolve_path(parent_path);
    if (!parent || parent->type != FILE_TYPE_DIRECTORY) return -1;
    
    // Create file
    if (!parent->ops || !parent->ops->create) return -1;
    
    return parent->ops->create(parent, filename, FILE_TYPE_REGULAR, permissions);
}

int vfs_mkdir(const char *path, uint32_t permissions) {
    if (!path || path[0] != '/') return -1;
    
    // Find parent directory
    char parent_path[256];
    char dirname[MAX_FILENAME];
    
    int last_slash = -1;
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/') last_slash = i;
    }
    
    if (last_slash < 0) return -1;
    
    // Extract parent path and dirname
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
    
    // Get parent directory
    vfs_node_t *parent = vfs_resolve_path(parent_path);
    if (!parent || parent->type != FILE_TYPE_DIRECTORY) return -1;
    
    // Create directory
    if (!parent->ops || !parent->ops->create) return -1;
    
    return parent->ops->create(parent, dirname, FILE_TYPE_DIRECTORY, permissions);
}

int vfs_unlink(const char *path) {
    if (!path || path[0] != '/') return -1;
    
    // Find parent directory
    char parent_path[256];
    char name[MAX_FILENAME];
    
    int last_slash = -1;
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/') last_slash = i;
    }
    
    if (last_slash < 0) return -1;
    
    // Extract parent path and name
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
    
    // Get parent directory
    vfs_node_t *parent = vfs_resolve_path(parent_path);
    if (!parent || parent->type != FILE_TYPE_DIRECTORY) return -1;
    
    // Remove entry
    if (!parent->ops || !parent->ops->unlink) return -1;
    
    return parent->ops->unlink(parent, name);
}

void vfs_list_directory(const char *path) {
    vfs_node_t *dir = vfs_resolve_path(path);
    if (!dir) {
        printk(0xFFFF0000, 0x000000, "Directory not found: %s\n", path);
        return;
    }
    
    if (dir->type != FILE_TYPE_DIRECTORY) {
        printk(0xFFFF0000, 0x000000, "Not a directory: %s\n", path);
        return;
    }
    
    printk(0xFFFFFFFF, 0x000000, "Contents of %s:\n", path);
    
    uint32_t i = 0;
    vfs_node_t *entry;
    int count = 0;
    
    while ((entry = vfs_readdir(dir, i++)) != NULL) {
        char type = (entry->type == FILE_TYPE_DIRECTORY) ? 'd' : 'f';
        printk(0xFFFFFFFF, 0x000000, "  [%c] %-20s %8u bytes\n", 
               type, entry->name, entry->size);
        count++;
        
        kfree(entry);
    }
    
    if (count == 0) {
        printk(0xFFFFFF00, 0x000000, "  (empty)\n");
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