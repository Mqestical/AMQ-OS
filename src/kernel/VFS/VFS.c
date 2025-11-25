#include "vfs.h"
#include "memory.h"
#include "print.h"
#include "tinyfs.h"
// Global VFS state
static vfs_node_t *root_node = NULL;
static vfs_node_t *current_dir = NULL;
static char current_path[256] = "/";
static file_descriptor_t vfs_fd_table[MAX_OPEN_FILES];
static filesystem_t *registered_filesystems[16];
static int num_filesystems = 0;

// String helper functions
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
     // Debug: Check if BSS is actually zeroed
    char msg0[] = "[VFS] vfs_init called\n";
    printk(0xFFFFFF00, 0x000000, msg0);
    
    char msg_root[] = "[VFS] Initial root_node value: %p\n";
    printk(0xFFFFFF00, 0x000000, msg_root, root_node);
    
    char msg_numfs[] = "[VFS] Initial num_filesystems value: %d\n";
    printk(0xFFFFFF00, 0x000000, msg_numfs, num_filesystems);
    
    // Explicitly set to NULL/0 to be safe
    root_node = NULL;
    num_filesystems = 0;
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
    
    root_node = NULL;
    num_filesystems = 0;
    
    char msg[] = "[VFS] Initialized\n";
    printk(0xFF00FF00, 0x000000, msg);
}

int vfs_register_filesystem(filesystem_t *fs) {
    if (!fs || !fs->name) return -1;  // Add this check
    if (num_filesystems >= 16) return -1;
    
    registered_filesystems[num_filesystems++] = fs;
    
    char msg[] = "[VFS] Registered filesystem: %s\n";
    printk(0xFF00FF00, 0x000000, msg, fs->name);
    
    return 0;
}

vfs_node_t* vfs_get_root(void) {
    return root_node;
}

void vfs_debug_root(void) {
    if (!root_node) {
        char msg[] = "[VFS DEBUG] root_node is NULL\n";
        printk(0xFFFF0000, 0x000000, msg);
    } else {
        char msg1[] = "[VFS DEBUG] root_node at %p\n";
        char msg2[] = "[VFS DEBUG]   name: '%s'\n";
        char msg3[] = "[VFS DEBUG]   type: %d\n";
        char msg4[] = "[VFS DEBUG]   fs: %p\n";
        char msg5[] = "[VFS DEBUG]   ops: %p\n";
        
        printk(0xFF00FF00, 0x000000, msg1, root_node);
        printk(0xFF00FF00, 0x000000, msg2, root_node->name);
        printk(0xFF00FF00, 0x000000, msg3, root_node->type);
        printk(0xFF00FF00, 0x000000, msg4, root_node->fs);
        printk(0xFF00FF00, 0x000000, msg5, root_node->ops);
        
        if (root_node->fs) {
            char msg6[] = "[VFS DEBUG]   fs->name: '%s'\n";
            char msg7[] = "[VFS DEBUG]   fs->ops: %p\n";
            char msg8[] = "[VFS DEBUG]   fs->private_data: %p\n";
            
            printk(0xFF00FF00, 0x000000, msg6, root_node->fs->name);
            printk(0xFF00FF00, 0x000000, msg7, root_node->fs->ops);
            printk(0xFF00FF00, 0x000000, msg8, root_node->fs->private_data);
        }
    }
}

int vfs_mount(const char *fs_type, const char *device, const char *mountpoint) {
    char msg0[] = "[VFS] === vfs_mount START ===\n";
    printk(0xFFFFFF00, 0x000000, msg0);
    
    char msg_params[] = "[VFS] fs_type='%s', device='%s', mountpoint='%s'\n";
    printk(0xFFFFFF00, 0x000000, msg_params, fs_type, device, mountpoint);
    
    char msg_root_before[] = "[VFS] root_node BEFORE mount: %p\n";
    printk(0xFFFFFF00, 0x000000, msg_root_before, root_node);
    
    char msg_debug1[] = "[VFS] DEBUG: num_filesystems = %d\n";
    printk(0xFFFFFF00, 0x000000, msg_debug1, num_filesystems);
    
       char verify[] = "[VFS MOUNT START] Verifying registered filesystems:\n";
    printk(0xFFFFFF00, 0x000000, verify);
    for (int i = 0; i < num_filesystems; i++) {
        char check[] = "[VFSX_DEBUG]   fs[%d] at %p, name at %p: '%s'\n";
        printk(0xFFFFFF00, 0x000000, check, i, 
               registered_filesystems[i],
               registered_filesystems[i]->name,
               registered_filesystems[i]->name);
    }

    // List all registered filesystems
    char msg_list[] = "[VFS] Registered filesystems:\n";
    printk(0xFFFFFF00, 0x000000, msg_list);
    for (int i = 0; i < num_filesystems; i++) {
        if (registered_filesystems[i]) {
            char msg_item[] = "[VFS]  [%d] '%s' at %p\n";
            printk(0xFFFFFF00, 0x000000, msg_item, i, 
                   registered_filesystems[i]->name, 
                   registered_filesystems[i]);
        }
    }
    
    // Find filesystem type
   // Find filesystem type
filesystem_t *fs = NULL;
for (int i = 0; i < num_filesystems; i++) {
    char msg_check[] = "[VFS] Checking index %d: %p\n";
    printk(0xFFFFFF00, 0x000000, msg_check, i, registered_filesystems[i]);
    
    char debug_fs_type[] = "[VFS-DEBUG] fs_type[0]='%c' fs_type addr=%p\n";
printk(0xFFFFFF00, 0x000000, debug_fs_type, fs_type[0], fs_type);

char debug_name[] = "[VFS-DEBUG] name[0]='%c' name addr=%p\n";
printk(0xFFFFFF00, 0x000000, debug_name, registered_filesystems[i]->name[0], 
       registered_filesystems[i]->name);

    if (registered_filesystems[i] != NULL && registered_filesystems[i]->name != NULL) {
        char msg_cmp[] = "[VFS] Comparing '%s' with '%s'\n";
        printk(0xFFFFFF00, 0x000000, msg_cmp, fs_type, registered_filesystems[i]->name);
        
        if (str_cmp(fs_type, registered_filesystems[i]->name) == 0) {
            fs = registered_filesystems[i];
            char msg2[] = "[VFS] Found filesystem type: %s\n";
            printk(0xFF00FF00, 0x000000, msg2, fs_type);
            break;
        }
    }
}
    
    if (!fs) {
        char err[] = "[VFS] Filesystem type not found: %s\n"; 
        printk(0xFFFF0000, 0x000000, err, fs_type);
        return -1;
    }
    
    char msg3[] = "[VFS] Calling fs->ops->mount()...\n";
    printk(0xFFFFFF00, 0x000000, msg3);
    
    // Mount the filesystem
    if (fs->ops->mount(fs, device) != 0) {
        char err[] = "[VFS] Failed to mount %s on device %s\n";
        printk(0xFFFF0000, 0x000000, err, fs_type, device);
        return -1;
    }
    
    char msg4[] = "[VFS] Mount operation successful\n";
    printk(0xFF00FF00, 0x000000, msg4);
    
    // Set root node if mounting to /
    if (mountpoint[0] == '/' && mountpoint[1] == '\0') {
        char msg5[] = "[VFS] Getting root node from filesystem...\n";
        printk(0xFFFFFF00, 0x000000, msg5);

        char ERRmsg[] = "[VALUE] FSVAL: \n";
        printk(0xFFFFFF00, 0x000000, ERRmsg, fs->ops->get_root(fs));
        root_node = fs->ops->get_root(fs);
        
        char msg_root_after[] = "[VFS] root_node AFTER get_root: %p\n";
        printk(0xFFFFFF00, 0x000000, msg_root_after, root_node);
        
        if (!root_node) {
            char err[] = "[VFS] CRITICAL: get_root returned NULL!\n";
            printk(0xFFFF0000, 0x000000, err);
            return -1;
        }
        
        // CRITICAL: Ensure the root node has the filesystem reference
        root_node->fs = fs;
        current_dir = root_node;
        char msg6[] = "[VFS] Root node set successfully:\n";
        char msg7[] = "[VFS]   root_node = %p\n";
        char msg8[] = "[VFS]   root_node->fs = %p\n";
        char msg9[] = "[VFS]   root_node->ops = %p\n";
        char msg10[] = "[VFS]   root_node->name = '%s'\n";
        char msg11[] = "[VFS]   root_node->type = %d\n";
        
        printk(0xFF00FF00, 0x000000, msg6);
        printk(0xFF00FF00, 0x000000, msg7, root_node);
        printk(0xFF00FF00, 0x000000, msg8, root_node->fs);
        printk(0xFF00FF00, 0x000000, msg9, root_node->ops);
        printk(0xFF00FF00, 0x000000, msg10, root_node->name);
        printk(0xFF00FF00, 0x000000, msg11, root_node->type);
    }
    
    char msg12[] = "[VFS] Mounted %s at %s\n";
    printk(0xFF00FF00, 0x000000, msg12, fs_type, mountpoint);
    
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
    
    // Skip leading slash
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
    char msg1[] = "[VFS] resolve_path: '%s'\n";
    printk(0xFFFFFF00, 0x000000, msg1, path);
    
    if (!root_node) {
        char err[] = "[VFS] resolve_path: root_node is NULL!\n";
        printk(0xFFFF0000, 0x000000, err);
        return NULL;
    }
    char msg2[] = "[VFS] resolve_path: root_node = %p\n";
    printk(0xFF00FF00, 0x000000, msg2, root_node);
    if (!path) {
        char err[] = "[VFS] resolve_path: path is NULL\n";
        printk(0xFFFF0000, 0x000000, err);
        return NULL;
    }
    // Handle root directory
        if (path[0] == '/' && path[1] == '\0') {
        char msg3[] = "[VFS] resolve_path: returning root\n";
        printk(0xFF00FF00, 0x000000, msg3);
        return root_node;
    }
    if (path[0] != '/') {
        char err[] = "[VFS] resolve_path: path must be absolute: %s\n";
        printk(0xFFFF0000, 0x000000, err, path);
        return NULL;
    }
    
    char tokens[32][MAX_FILENAME];
    int token_count = 0;
    tokenize_path(path, tokens, &token_count);
    
    char msg4[] = "[VFS] resolve_path: %d path components\n";
    printk(0xFFFFFF00, 0x000000, msg4, token_count);
    
    vfs_node_t *current = root_node;
    
    for (int i = 0; i < token_count; i++) {
        char msg5[] = "[VFS] resolve_path: looking for '%s'\n";
        printk(0xFFFFFF00, 0x000000, msg5, tokens[i]);
        
        if (!current) {
            char err[] = "[VFS] resolve_path: current node is NULL at component '%s'\n";
            printk(0xFFFF0000, 0x000000, err, tokens[i]);
            return NULL;
        }
        
        if (current->type != FILE_TYPE_DIRECTORY) {
            char err[] = "[VFS] resolve_path: '%s' is not a directory\n";
            printk(0xFFFF0000, 0x000000, err, tokens[i]);
            return NULL;
        }
        
        current = vfs_finddir(current, tokens[i]);
        if (!current) {
            char err[] = "[VFS] resolve_path: component not found: '%s'\n";
            printk(0xFFFF0000, 0x000000, err, tokens[i]);
            return NULL;
        }
        
        char msg6[] = "[VFS] resolve_path: found '%s'\n";
        printk(0xFF00FF00, 0x000000, msg6, tokens[i]);
    }
    
    char msg7[] = "[VFS] resolve_path: success, returning %p\n";
    printk(0xFF00FF00, 0x000000, msg7, current);
    return current;
}

int vfs_open(const char *path, uint32_t flags) {
    vfs_node_t *node = vfs_resolve_path(path);  // FAILURE AFTER ROOT NODE = ... (PRINTK)
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
    char msg1[] = "[VFS] vfs_create: '%s'\n";
    printk(0xFFFFFF00, 0x000000, msg1, path);
    
    if (!path || path[0] != '/') {
        char err[] = "[VFS] vfs_create: Invalid path\n";
        printk(0xFFFF0000, 0x000000, err);
        return -1;
    }
    
    // Find parent directory
    char parent_path[256];
    char filename[MAX_FILENAME];
    
    int last_slash = -1;
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/') last_slash = i;
    }
    
    if (last_slash < 0) {
        char err[] = "[VFS] vfs_create: No slash in path\n";
        printk(0xFFFF0000, 0x000000, err);
        return -1;
    }
    
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
    
    char msg2[] = "[VFS] vfs_create: parent='%s', filename='%s'\n";
    printk(0xFFFFFF00, 0x000000, msg2, parent_path, filename);
    
    // Get parent directory
    vfs_node_t *parent = vfs_resolve_path(parent_path);
    if (!parent) {
        char err[] = "[VFS] vfs_create: Parent directory not found: %s\n";
        printk(0xFFFF0000, 0x000000, err, parent_path);
        return -1;
    }
    
    char msg3[] = "[VFS] vfs_create: parent found at %p\n";
    printk(0xFF00FF00, 0x000000, msg3, parent);
    
    if (parent->type != FILE_TYPE_DIRECTORY) {
        char err[] = "[VFS] vfs_create: Parent is not a directory\n";
        printk(0xFFFF0000, 0x000000, err);
        return -1;
    }
    
    if (!parent->ops || !parent->ops->create) {
        char err1[] = "[VFS] vfs_create: Parent has no create operation\n";
        char err2[] = "[VFS]   parent->ops = %p\n";
        printk(0xFFFF0000, 0x000000, err1);
        printk(0xFFFF0000, 0x000000, err2, parent->ops);
        if (parent->ops) {
            char err3[] = "[VFS]   parent->ops->create = %p\n";
            printk(0xFFFF0000, 0x000000, err3, parent->ops->create);
        }
        return -1;
    }
    
    char msg4[] = "[VFS] vfs_create: Calling parent->ops->create()...\n";
    printk(0xFFFFFF00, 0x000000, msg4);
    
    // Create file
    int result = parent->ops->create(parent, filename, FILE_TYPE_REGULAR, permissions);
    
    if (result == 0) {
        char msg5[] = "[VFS] vfs_create: SUCCESS\n";
        printk(0xFF00FF00, 0x000000, msg5);
    } else {
        char err[] = "[VFS] vfs_create: FAILED\n";
        printk(0xFFFF0000, 0x000000, err);
    }
    
    return result;
}

int vfs_mkdir(const char *path, uint32_t permissions) {
    char msg1[] = "[VFS] vfs_mkdir: '%s'\n";
    printk(0xFFFFFF00, 0x000000, msg1, path);
    
    if (!path || path[0] != '/') {
        char err[] = "[VFS] vfs_mkdir: Invalid path\n";
        printk(0xFFFF0000, 0x000000, err);
        return -1;
    }
    
    // Find parent directory
    char parent_path[256];
    char dirname[MAX_FILENAME];
    
    int last_slash = -1;
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/') last_slash = i;
    }
    
    if (last_slash < 0) {
        char err[] = "[VFS] vfs_mkdir: No slash in path\n";
        printk(0xFFFF0000, 0x000000, err);
        return -1;
    }
    
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
    
    char msg2[] = "[VFS] vfs_mkdir: parent='%s', dirname='%s'\n";
    printk(0xFFFFFF00, 0x000000, msg2, parent_path, dirname);
    
    // Get parent directory
    vfs_node_t *parent = vfs_resolve_path(parent_path);
    if (!parent) {
        char err[] = "[VFS] vfs_mkdir: Parent directory not found: %s\n";
        printk(0xFFFF0000, 0x000000, err, parent_path);
        return -1;
    }
    
    char msg3[] = "[VFS] vfs_mkdir: parent found at %p\n";
    printk(0xFF00FF00, 0x000000, msg3, parent);
    
    if (parent->type != FILE_TYPE_DIRECTORY) {
        char err[] = "[VFS] vfs_mkdir: Parent is not a directory\n";
        printk(0xFFFF0000, 0x000000, err);
        return -1;
    }
    
    if (!parent->ops || !parent->ops->create) {
        char err[] = "[VFS] vfs_mkdir: Parent has no create operation\n";
        printk(0xFFFF0000, 0x000000, err);
        return -1;
    }
    
    char msg4[] = "[VFS] vfs_mkdir: Calling parent->ops->create()...\n";
    printk(0xFFFFFF00, 0x000000, msg4);
    
    // Create directory
    int result = parent->ops->create(parent, dirname, FILE_TYPE_DIRECTORY, permissions);
    
    if (result == 0) {
        char msg5[] = "[VFS] vfs_mkdir: SUCCESS\n";
        printk(0xFF00FF00, 0x000000, msg5);
    } else {
        char err[] = "[VFS] vfs_mkdir: FAILED\n";
        printk(0xFFFF0000, 0x000000, err);
    }
    
    return result;
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
    char msg1[] = "[VFS] list_directory: '%s'\n";
    printk(0xFFFFFF00, 0x000000, msg1, path);
    
    vfs_node_t *dir = vfs_resolve_path(path);
    if (!dir) {
        char err[] = "Directory not found: %s\n";
        printk(0xFFFF0000, 0x000000, err, path);
        return;
    }
    
    if (dir->type != FILE_TYPE_DIRECTORY) {
        char err[] = "Not a directory: %s\n";
        printk(0xFFFF0000, 0x000000, err, path);
        return;
    }
    
    char msg2[] = "Contents of %s:\n";
    printk(0xFFFFFFFF, 0x000000, msg2, path);
    
    uint32_t i = 0;
    vfs_node_t *entry;
    int count = 0;
    
    while ((entry = vfs_readdir(dir, i++)) != NULL) {
        char type = (entry->type == FILE_TYPE_DIRECTORY) ? 'd' : 'f';
        char msg3[] = "  [%c] %s %d bytes\n";
        printk(0xFFFFFFFF, 0x000000, msg3, type, entry->name, entry->size);
        count++;
        
        kfree(entry);
    }
    
    if (count == 0) {
        char msg4[] = "  (empty)\n";
        printk(0xFFFFFF00, 0x000000, msg4);
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

// Get current working directory node
vfs_node_t* vfs_get_cwd(void) {
    return current_dir ? current_dir : root_node;
}

// Get current working directory path string
const char* vfs_get_cwd_path(void) {
    return current_path;
}

// Change directory
// Change directory
int vfs_chdir(const char *path) {
    if (!path) return -1;
    
    vfs_node_t *target;
    char full_path[256];
    
    // Handle absolute vs relative paths
    if (path[0] == '/') {
        // Absolute path
        str_cpy(full_path, path, 256);
        target = vfs_resolve_path(path);
    } else {
        // Relative path - build full path
        
        // Copy current path
        int i = 0;
        while (current_path[i] && i < 255) {
            full_path[i] = current_path[i];
            i++;
        }
        
        // Add slash if not at root
        if (i > 0 && full_path[i-1] != '/') {
            full_path[i++] = '/';
        }
        
        // Add relative path
        int j = 0;
        while (path[j] && i < 255) {
            full_path[i++] = path[j++];
        }
        full_path[i] = '\0';
        
        target = vfs_resolve_path(full_path);
    }
    
    if (!target) {
        char err[] = "cd: directory not found: %s\n";
        printk(0xFFFF0000, 0x000000, err, path);
        return -1;
    }
    
    if (target->type != FILE_TYPE_DIRECTORY) {
        char err[] = "cd: not a directory: %s\n";
        printk(0xFFFF0000, 0x000000, err, path);
        return -1;
    }
    
    // Update current directory
    current_dir = target;
    
    // Update path string - NOW WORKS FOR BOTH ABSOLUTE AND RELATIVE
    int i = 0;
    while (full_path[i] && i < 255) {
        current_path[i] = full_path[i];
        i++;
    }
    current_path[i] = '\0';
    
    // Remove trailing slash (unless it's root "/")
    if (i > 1 && current_path[i-1] == '/') {
        current_path[i-1] = '\0';
    }
    
    return 0;
}