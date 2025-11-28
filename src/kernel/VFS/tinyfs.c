#include "tinyfs.h"
#include "ata.h"
#include "memory.h"
#include "print.h"
#include "vfs.h"
#include "string_helpers.h"

#define EOF 0xFFFFFFFF

// Forward declarations
static int tinyfs_mount(filesystem_t *fs, const char *device);
static int tinyfs_unmount(filesystem_t *fs);
static vfs_node_t* tinyfs_get_root(filesystem_t *fs);
static int tinyfs_get_stats(filesystem_t *fs, fs_stats_t *stats);

static int tinyfs_open(vfs_node_t *node, uint32_t flags);
static int tinyfs_close(vfs_node_t *node);
static int tinyfs_read(vfs_node_t *node, uint8_t *buffer, uint32_t size, uint32_t offset);
static int tinyfs_write(vfs_node_t *node, uint8_t *buffer, uint32_t size, uint32_t offset);
static vfs_node_t* tinyfs_readdir(vfs_node_t *node, uint32_t index);
static vfs_node_t* tinyfs_finddir(vfs_node_t *node, const char *name);
static int tinyfs_create_node(vfs_node_t *parent, const char *name, uint8_t type, uint32_t permissions);
static int tinyfs_unlink(vfs_node_t *parent, const char *name);

// Filesystem operations
static filesystem_operations_t tinyfs_fs_ops = {
    .mount = tinyfs_mount,
    .unmount = tinyfs_unmount,
    .get_root = tinyfs_get_root,
    .get_stats = tinyfs_get_stats
};

// VFS node operations
static vfs_operations_t tinyfs_vfs_ops = {
    .open = tinyfs_open,
    .close = tinyfs_close,
    .read = tinyfs_read,
    .write = tinyfs_write,
    .readdir = tinyfs_readdir,
    .finddir = tinyfs_finddir,
    .create = tinyfs_create_node,
    .unlink = tinyfs_unlink
};

// String helpers
static void strcpy_safe(char *dest, const char *src, int max_len) {
    int i;
    for (i = 0; i < max_len - 1 && src[i]; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

static int strcmp_safe(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

// ============================================================================
// DISK I/O HELPERS
// ============================================================================

static int write_superblock(tinyfs_data_t *data) {
    uint8_t buffer[TINYFS_BLOCK_SIZE];
    
    // Clear buffer
    for (int i = 0; i < TINYFS_BLOCK_SIZE; i++) {
        buffer[i] = 0;
    }
    
    tinyfs_superblock_t *sb = (tinyfs_superblock_t*)buffer;
    *sb = data->sb;
    
    PRINT(0xFFFFFF00, 0x000000, "[TINYFS] Writing superblock (free_blocks=%u)...\n", sb->free_blocks);
    
    int result = ata_write_sectors(0, 1, buffer);
    if (result != 0) {
        PRINT(0xFFFF0000, 0x000000, "[TINYFS] Failed to write superblock\n");
        return -1;
    }
    
    return 0;
}

static int write_fat(tinyfs_data_t *data) {
    uint8_t buffer[TINYFS_BLOCK_SIZE];
    int entries_per_block = TINYFS_BLOCK_SIZE / sizeof(uint32_t);
    
    PRINT(0xFFFFFF00, 0x000000, "[TINYFS] Writing FAT...\n");
    
    for (int i = 0; i < 10; i++) {
        // Clear buffer
        for (int k = 0; k < TINYFS_BLOCK_SIZE; k++) {
            buffer[k] = 0;
        }
        
        // Copy FAT entries to buffer
        for (int j = 0; j < entries_per_block; j++) {
            int idx = i * entries_per_block + j;
            if (idx < 1024) {
                ((uint32_t*)buffer)[j] = data->fat[idx];
            }
        }
        
        // Write buffer to disk
        if (ata_write_sectors(data->sb.fat_start + i, 1, buffer) != 0) {
            PRINT(0xFFFF0000, 0x000000, "[TINYFS] Failed to write FAT block %d\n", i);
            return -1;
        }
    }
    
    PRINT(0xFF00FF00, 0x000000, "[TINYFS] FAT written successfully\n");
    return 0;
}

static int write_directory(tinyfs_data_t *data) {
    uint8_t buffer[TINYFS_BLOCK_SIZE];
    int entries_per_block = TINYFS_BLOCK_SIZE / sizeof(tinyfs_dirent_t);
    
    PRINT(0xFFFFFF00, 0x000000, "[TINYFS] Writing directory (90 blocks)...\n");
    
    for (int i = 0; i < 90; i++) {
        // Clear buffer
        for (int k = 0; k < TINYFS_BLOCK_SIZE; k++) {
            buffer[k] = 0;
        }
        
        // Copy directory entries to buffer
        for (int j = 0; j < entries_per_block; j++) {
            int idx = i * entries_per_block + j;
            if (idx < TINYFS_MAX_FILES) {
                ((tinyfs_dirent_t*)buffer)[j] = data->dirents[idx];
            }
        }
        
        // Write buffer to disk
        if (ata_write_sectors(data->sb.dir_start + i, 1, buffer) != 0) {
            PRINT(0xFFFF0000, 0x000000, "[TINYFS] Failed to write dir block %d\n", i);
            return -1;
        }
    }
    
    PRINT(0xFF00FF00, 0x000000, "[TINYFS] Directory written successfully\n");
    return 0;
}

// ============================================================================
// BLOCK ALLOCATION
// ============================================================================

static int allocate_block(tinyfs_data_t *data) {
    for (int i = data->sb.data_start; i < data->sb.total_blocks; i++) {
        if (data->fat[i] == 0) {
            data->fat[i] = EOF;
            data->sb.free_blocks--;
            PRINT(0xFF00FF00, 0x000000, "[TINYFS] Allocated block %d (free_blocks=%u)\n", 
                   i, data->sb.free_blocks);
            return i;
        }
    }
    PRINT(0xFFFF0000, 0x000000, "[TINYFS] No free blocks available\n");
    return -1;
}

static void free_block_chain(tinyfs_data_t *data, uint32_t first_block) {
    uint32_t current = first_block;
    
    while (current != EOF && current != 0) {
        uint32_t next = data->fat[current];
        data->fat[current] = 0;
        data->sb.free_blocks++;
        current = next;
    }
}

static int find_free_dirent(tinyfs_data_t *data) {
    for (int i = 1; i < TINYFS_MAX_FILES; i++) {
        if (!data->dirents[i].used) {
            return i;
        }
    }
    return -1;
}

// ============================================================================
// FORMAT
// ============================================================================

int tinyfs_format(const char *device) {
    PRINT(0xFFFFFF00, 0x000000, "[TINYFS] Formatting disk...\n");
    
    uint8_t buffer[TINYFS_BLOCK_SIZE];
    
    // Clear buffer
    for (int i = 0; i < TINYFS_BLOCK_SIZE; i++) {
        buffer[i] = 0;
    }
    
    // Create superblock
    tinyfs_superblock_t *sb = (tinyfs_superblock_t*)buffer;
    sb->magic = TINYFS_MAGIC;
    sb->total_blocks = 1024;
    sb->fat_start = 1;
    sb->dir_start = 11;
    sb->data_start = 101;
    sb->free_blocks = 1024 - 101;  // 923 free blocks
    
    PRINT(0xFFFFFF00, 0x000000, "[TINYFS] Writing superblock...\n");
    if (ata_write_sectors(0, 1, buffer) != 0) {
        PRINT(0xFFFF0000, 0x000000, "[TINYFS] Failed to write superblock\n");
        return -1;
    }
    
    // Initialize FAT - mark reserved blocks as EOF
    PRINT(0xFFFFFF00, 0x000000, "[TINYFS] Writing FAT (10 blocks)...\n");
    for (int i = 1; i <= 10; i++) {
        // Clear buffer
        for (int k = 0; k < TINYFS_BLOCK_SIZE; k++) {
            buffer[k] = 0;
        }
        
        int entries_per_block = TINYFS_BLOCK_SIZE / sizeof(uint32_t);
        for (int j = 0; j < entries_per_block; j++) {
            uint32_t block_num = (i - 1) * entries_per_block + j;
            if (block_num < 1024) {
                // Mark blocks 0-100 as reserved (EOF)
                if (block_num < 101) {
                    ((uint32_t*)buffer)[j] = EOF;
                } else {
                    ((uint32_t*)buffer)[j] = 0;  // Free
                }
            }
        }
        
        if (ata_write_sectors(i, 1, buffer) != 0) {
            PRINT(0xFFFF0000, 0x000000, "[TINYFS] Failed to write FAT block %d\n", i);
            return -1;
        }
    }
    
    // Clear directory blocks
    PRINT(0xFFFFFF00, 0x000000, "[TINYFS] Clearing directory (90 blocks)...\n");
    for (int i = 0; i < TINYFS_BLOCK_SIZE; i++) {
        buffer[i] = 0;
    }
    
    for (int i = 11; i <= 100; i++) {
        if (ata_write_sectors(i, 1, buffer) != 0) {
            PRINT(0xFFFF0000, 0x000000, "[TINYFS] Failed to write dir block %d\n", i);
            return -1;
        }
    }
    
    PRINT(0xFF00FF00, 0x000000, "[TINYFS] Format successful\n");
    return 0;
}

// ============================================================================
// MOUNT/UNMOUNT
// ============================================================================

static int tinyfs_mount(filesystem_t *fs, const char *device) {
    PRINT(0xFFFFFF00, 0x000000, "[TINYFS] Mounting filesystem...\n");
    
    tinyfs_data_t *data = (tinyfs_data_t*)kmalloc(sizeof(tinyfs_data_t));
    if (!data) {
        PRINT(0xFFFF0000, 0x000000, "[TINYFS] Failed to allocate memory\n");
        return -1;
    }
    
    strcpy_safe(data->device, device, 32);
    
    // Read superblock
    uint8_t buffer[TINYFS_BLOCK_SIZE];
    if (ata_read_sectors(0, 1, buffer) != 0) {
        PRINT(0xFFFF0000, 0x000000, "[TINYFS] Failed to read superblock\n");
        kfree(data);
        return -1;
    }
    
    tinyfs_superblock_t *sb = (tinyfs_superblock_t*)buffer;
    if (sb->magic != TINYFS_MAGIC) {
        PRINT(0xFFFF0000, 0x000000, "[TINYFS] Invalid magic: 0x%x (expected 0x%x)\n", 
               sb->magic, TINYFS_MAGIC);
        kfree(data);
        return -1;
    }
    
    data->sb = *sb;
    PRINT(0xFF00FF00, 0x000000, "[TINYFS] Superblock loaded (free_blocks=%u)\n", 
           data->sb.free_blocks);
    
    // Read FAT
    PRINT(0xFFFFFF00, 0x000000, "[TINYFS] Reading FAT...\n");
    int fat_entries_per_block = TINYFS_BLOCK_SIZE / sizeof(uint32_t);
    for (int i = 0; i < 10; i++) {
        if (ata_read_sectors(data->sb.fat_start + i, 1, buffer) != 0) {
            PRINT(0xFFFF0000, 0x000000, "[TINYFS] Failed to read FAT block %d\n", i);
            kfree(data);
            return -1;
        }
        
        for (int j = 0; j < fat_entries_per_block; j++) {
            int idx = i * fat_entries_per_block + j;
            if (idx < 1024) {
                data->fat[idx] = ((uint32_t*)buffer)[j];
            }
        }
    }
    
    // Read directory
    PRINT(0xFFFFFF00, 0x000000, "[TINYFS] Reading directory...\n");
    int entries_per_block = TINYFS_BLOCK_SIZE / sizeof(tinyfs_dirent_t);
    for (int i = 0; i < 90; i++) {
        if (ata_read_sectors(data->sb.dir_start + i, 1, buffer) != 0) {
            PRINT(0xFFFF0000, 0x000000, "[TINYFS] Failed to read directory block %d\n", i);
            kfree(data);
            return -1;
        }
        
        for (int j = 0; j < entries_per_block; j++) {
            int idx = i * entries_per_block + j;
            if (idx < TINYFS_MAX_FILES) {
                data->dirents[idx] = ((tinyfs_dirent_t*)buffer)[j];
            }
        }
    }
    
    fs->private_data = data;
    PRINT(0xFF00FF00, 0x000000, "[TINYFS] Mount successful\n");
    return 0;
}

static int tinyfs_unmount(filesystem_t *fs) {
    if (fs->private_data) {
        tinyfs_data_t *data = (tinyfs_data_t*)fs->private_data;
        
        write_superblock(data);
        write_fat(data);
        write_directory(data);
        
        kfree(data);
        fs->private_data = NULL;
    }
    return 0;
}

static vfs_node_t* tinyfs_get_root(filesystem_t *fs) {
    vfs_node_t *root = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (!root) return NULL;
    
    strcpy_safe(root->name, "/", MAX_FILENAME);
    root->type = FILE_TYPE_DIRECTORY;
    root->permissions = FILE_READ | FILE_WRITE;
    root->size = 0;
    root->inode = 0;
    root->fs = fs;
    root->ops = &tinyfs_vfs_ops;
    root->private_data = NULL;
    
    PRINT(0xFF00FF00, 0x000000, "[TINYFS] Root node created\n");
    return root;
}

static int tinyfs_get_stats(filesystem_t *fs, fs_stats_t *stats) {
    if (!fs || !fs->private_data || !stats) return -1;
    
    tinyfs_data_t *data = (tinyfs_data_t*)fs->private_data;
    
    stats->total_blocks = data->sb.total_blocks;
    stats->free_blocks = data->sb.free_blocks;
    stats->block_size = TINYFS_BLOCK_SIZE;
    
    return 0;
}

// ============================================================================
// OPEN/CLOSE
// ============================================================================

static int tinyfs_open(vfs_node_t *node, uint32_t flags) {
    return 0;
}

static int tinyfs_close(vfs_node_t *node) {
    return 0;
}

// ============================================================================
// READ/WRITE
// ============================================================================

static int tinyfs_read(vfs_node_t *node, uint8_t *buffer, uint32_t size, uint32_t offset) {
    if (!node || !node->fs || !node->fs->private_data) return -1;
    
    tinyfs_data_t *data = (tinyfs_data_t*)node->fs->private_data;
    tinyfs_dirent_t *dirent = (tinyfs_dirent_t*)node->private_data;
    
    if (!dirent || offset >= dirent->size) return 0;
    
    if (offset + size > dirent->size) {
        size = dirent->size - offset;
    }
    
    uint32_t bytes_read = 0;
    uint32_t current_block = dirent->first_block;
    uint32_t block_offset = offset / TINYFS_BLOCK_SIZE;
    uint32_t byte_offset = offset % TINYFS_BLOCK_SIZE;
    
    for (uint32_t i = 0; i < block_offset && current_block != EOF; i++) {
        current_block = data->fat[current_block];
    }
    
    uint8_t block_buffer[TINYFS_BLOCK_SIZE];
    
    while (bytes_read < size && current_block != EOF) {
        if (ata_read_sectors(current_block, 1, block_buffer) != 0) {
            return -1;
        }
        
        uint32_t to_copy = TINYFS_BLOCK_SIZE - byte_offset;
        if (to_copy > size - bytes_read) {
            to_copy = size - bytes_read;
        }
        
        for (uint32_t i = 0; i < to_copy; i++) {
            buffer[bytes_read + i] = block_buffer[byte_offset + i];
        }
        
        bytes_read += to_copy;
        byte_offset = 0;
        current_block = data->fat[current_block];
    }
    
    return bytes_read;
}

static int tinyfs_write(vfs_node_t *node, uint8_t *buffer, uint32_t size, uint32_t offset) {
    if (!node || !node->fs || !node->fs->private_data) return -1;
    
    tinyfs_data_t *data = (tinyfs_data_t*)node->fs->private_data;
    tinyfs_dirent_t *dirent = (tinyfs_dirent_t*)node->private_data;
    
    if (!dirent) return -1;
    
    if (dirent->first_block == 0) {
        int block = allocate_block(data);
        if (block < 0) return -1;
        dirent->first_block = block;
    }
    
    uint32_t bytes_written = 0;
    uint32_t current_block = dirent->first_block;
    uint32_t block_offset = offset / TINYFS_BLOCK_SIZE;
    uint32_t byte_offset = offset % TINYFS_BLOCK_SIZE;
    
    for (uint32_t i = 0; i < block_offset; i++) {
        if (data->fat[current_block] == EOF) {
            int new_block = allocate_block(data);
            if (new_block < 0) return bytes_written;
            data->fat[current_block] = new_block;
        }
        current_block = data->fat[current_block];
    }
    
    uint8_t block_buffer[TINYFS_BLOCK_SIZE];
    
    while (bytes_written < size) {
        if (byte_offset > 0 || (size - bytes_written) < TINYFS_BLOCK_SIZE) {
            ata_read_sectors(current_block, 1, block_buffer);
        }
        
        uint32_t to_write = TINYFS_BLOCK_SIZE - byte_offset;
        if (to_write > size - bytes_written) {
            to_write = size - bytes_written;
        }
        
        for (uint32_t i = 0; i < to_write; i++) {
            block_buffer[byte_offset + i] = buffer[bytes_written + i];
        }
        
        if (ata_write_sectors(current_block, 1, block_buffer) != 0) {
            return -1;
        }
        
        bytes_written += to_write;
        byte_offset = 0;
        
        if (bytes_written < size) {
            if (data->fat[current_block] == EOF) {
                int new_block = allocate_block(data);
                if (new_block < 0) break;
                data->fat[current_block] = new_block;
            }
            current_block = data->fat[current_block];
        }
    }
    
    if (offset + bytes_written > dirent->size) {
        dirent->size = offset + bytes_written;
        node->size = dirent->size;
    }
    
    // CRITICAL: Write changes back to disk
    write_directory(data);
    write_fat(data);
    write_superblock(data);
    
    return bytes_written;
}

// ============================================================================
// DIRECTORY OPERATIONS
// ============================================================================

static vfs_node_t* tinyfs_readdir(vfs_node_t *node, uint32_t index) {
    if (!node || node->type != FILE_TYPE_DIRECTORY) return NULL;
    if (!node->fs || !node->fs->private_data) return NULL;
    
    tinyfs_data_t *data = (tinyfs_data_t*)node->fs->private_data;
    
    // Get the current directory's inode
    uint32_t current_inode = node->inode;
    
    uint32_t count = 0;
    for (int i = 0; i < TINYFS_MAX_FILES; i++) {
        // Only show entries whose parent matches current directory
        if (data->dirents[i].used && data->dirents[i].parent_inode == current_inode) {
            if (count == index) {
                vfs_node_t *entry = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
                if (!entry) return NULL;
                
                strcpy_safe(entry->name, data->dirents[i].name, MAX_FILENAME);
                entry->type = data->dirents[i].is_directory ? 
                             FILE_TYPE_DIRECTORY : FILE_TYPE_REGULAR;
                entry->permissions = FILE_READ | FILE_WRITE;
                entry->size = data->dirents[i].size;
                entry->inode = i;
                entry->fs = node->fs;
                entry->ops = &tinyfs_vfs_ops;
                entry->private_data = &data->dirents[i];
                
                return entry;
            }
            count++;
        }
    }
    
    return NULL;
}


static vfs_node_t* tinyfs_finddir(vfs_node_t *node, const char *name) {
    if (!node || node->type != FILE_TYPE_DIRECTORY) return NULL;
    if (!node->fs || !node->fs->private_data) return NULL;
    
    tinyfs_data_t *data = (tinyfs_data_t*)node->fs->private_data;
    uint32_t current_inode = node->inode;
    
    for (int i = 0; i < TINYFS_MAX_FILES; i++) {
        if (data->dirents[i].used && 
            data->dirents[i].parent_inode == current_inode &&
            strcmp_safe(data->dirents[i].name, name) == 0) {
            
            vfs_node_t *entry = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
            if (!entry) return NULL;
            
            strcpy_safe(entry->name, data->dirents[i].name, MAX_FILENAME);
            entry->type = data->dirents[i].is_directory ? 
                         FILE_TYPE_DIRECTORY : FILE_TYPE_REGULAR;
            entry->permissions = FILE_READ | FILE_WRITE;
            entry->size = data->dirents[i].size;
            entry->inode = i;
            entry->fs = node->fs;
            entry->ops = &tinyfs_vfs_ops;
            entry->private_data = &data->dirents[i];
            
            return entry;
        }
    }
    
    return NULL;
}

static int tinyfs_create_node(vfs_node_t *parent, const char *name, uint8_t type, uint32_t permissions) {
    if (!parent || !parent->fs || !parent->fs->private_data) {
        PRINT(0xFFFF0000, 0x000000, "[TINYFS] create_node: invalid parent\n");
        return -1;
    }
    
    if (parent->type != FILE_TYPE_DIRECTORY) {
        PRINT(0xFFFF0000, 0x000000, "[TINYFS] create_node: parent not a directory\n");
        return -1;
    }
    
    tinyfs_data_t *data = (tinyfs_data_t*)parent->fs->private_data;
    
    PRINT(0xFFFFFF00, 0x000000, "[TINYFS] create_node: Creating '%s' (type=%d)\n", name, type);
    
    // Check for duplicates in the SAME directory
    uint32_t parent_inode = parent->inode;
    for (int i = 0; i < TINYFS_MAX_FILES; i++) {
        if (data->dirents[i].used && 
            data->dirents[i].parent_inode == parent_inode &&
            strcmp_safe(data->dirents[i].name, name) == 0) {
            PRINT(0xFFFF0000, 0x000000, "[TINYFS] File '%s' already exists\n", name);
            return -1;
        }
    }
    
    int free_idx = find_free_dirent(data);
    if (free_idx < 0) {
        PRINT(0xFFFF0000, 0x000000, "[TINYFS] No free directory entries\n");
        return -1;
    }
    
    PRINT(0xFFFFFF00, 0x000000, "[TINYFS] create_node: Using dirent index %d\n", free_idx);
    
    tinyfs_dirent_t *dirent = &data->dirents[free_idx];
    strcpy_safe(dirent->name, name, TINYFS_MAX_FILENAME);
    dirent->first_block = 0;
    dirent->size = 0;
    dirent->is_directory = (type == FILE_TYPE_DIRECTORY) ? 1 : 0;
    dirent->used = 1;
    dirent->parent_inode = parent_inode;
    
    PRINT(0xFFFFFF00, 0x000000, "[TINYFS] create_node: Writing directory to disk...\n");
    
    // Write changes to disk
    if (write_directory(data) != 0) {
        PRINT(0xFFFF0000, 0x000000, "[TINYFS] Failed to write directory\n");
        dirent->used = 0;
        return -1;
    }
    
    PRINT(0xFF00FF00, 0x000000, "[TINYFS] Created %s '%s' at index %d (parent=%d)\n", 
           (type == FILE_TYPE_DIRECTORY) ? "directory" : "file", name, free_idx, parent_inode);
    return 0;
}


static int tinyfs_unlink(vfs_node_t *parent, const char *name) {
    if (!parent || !parent->fs || !parent->fs->private_data) return -1;
    if (parent->type != FILE_TYPE_DIRECTORY) return -1;
    
    tinyfs_data_t *data = (tinyfs_data_t*)parent->fs->private_data;
    
    for (int i = 0; i < TINYFS_MAX_FILES; i++) {
        if (data->dirents[i].used && strcmp_safe(data->dirents[i].name, name) == 0) {
            
            if (data->dirents[i].first_block != 0) {
                free_block_chain(data, data->dirents[i].first_block);
            }
            
            data->dirents[i].used = 0;
            data->dirents[i].name[0] = '\0';
            data->dirents[i].first_block = 0;
            data->dirents[i].size = 0;
            
            write_fat(data);
            write_directory(data);
            write_superblock(data);
            
            PRINT(0xFF00FF00, 0x000000, "[TINYFS] Removed '%s'\n", name);
            return 0;
        }
    }
    
    return -1;
}

// ============================================================================
// CREATE FILESYSTEM
// ============================================================================


filesystem_t* tinyfs_create(void) {
    filesystem_t *fs = (filesystem_t*)kmalloc(sizeof(filesystem_t));
    if (!fs) {
        PRINT(0xFFFF0000, 0x000000, "[TINYFS] Failed to allocate filesystem\n");
        return NULL;
    }
    
    for (int i = 0; i < sizeof(filesystem_t); i++) {
        ((uint8_t*)fs)[i] = 0;
    }
    
    char name_str[] = "tinyfs";
    
    PRINT(0xFFFFFF00, 0x000000, "[TINYFS] About to call strcpy_safe with '%s'...\n", name_str);
    
    strcpy_safe(fs->name, name_str, 32);
    
    PRINT(0xFFFFFF00, 0x000000, "[TINYFS-STRCPY] AFTER strcpy_safe: fs->name = '%s'\n", fs->name);
    
    fs->ops = &tinyfs_fs_ops;
    fs->private_data = NULL;
    
    PRINT(0xFF00FF00, 0x000000, "[TINYFS] Filesystem created\n");
    return fs;
}