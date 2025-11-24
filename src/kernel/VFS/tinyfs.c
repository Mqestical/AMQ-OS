// Complete TinyFS with debug logging to find the issue
#include "tinyfs.h"
#include "ata.h"
#include "memory.h"
#include "print.h"
#include "vfs.h"

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

// File handle structure for tracking open files
typedef struct tinyfs_file_handle {
    int used;
    tinyfs_dirent_t *dirent;
    int dirent_index;
    uint32_t open_flags;
    uint32_t position;
    int reference_count;
    int is_dirty;
    uint32_t cached_size;
    int lock_mode;
    int num_readers;
    int has_writer;
} tinyfs_file_handle_t;

static tinyfs_file_handle_t file_handles[MAX_OPEN_FILES];
static int file_handles_initialized = 0;

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
    char msg[] = "[TINYFS] write_superblock: Writing superblock...\n";
    printk(0xFF00FFFF, 0x000000, msg);
    
    uint8_t buffer[TINYFS_BLOCK_SIZE];
    
    tinyfs_superblock_t *sb = (tinyfs_superblock_t*)buffer;
    *sb = data->sb;
    
    int result = ata_write_sectors(0, 1, buffer);
    
    if (result == 0) {
        char ok[] = "[TINYFS] write_superblock: SUCCESS\n";
        printk(0xFF00FF00, 0x000000, ok);
    } else {
        char err[] = "[TINYFS] write_superblock: FAILED\n";
        printk(0xFFFF0000, 0x000000, err);
    }
    
    return result;
}

static int write_fat(tinyfs_data_t *data) {
    char msg[] = "[TINYFS] write_fat: Writing FAT...\n";
    printk(0xFF00FFFF, 0x000000, msg);
    
    uint8_t buffer[TINYFS_BLOCK_SIZE];
    
    for (int i = 0; i < 10; i++) {
        // Clear buffer
        for (int k = 0; k < TINYFS_BLOCK_SIZE; k++) {
            buffer[k] = 0;
        }
        
        int entries_per_block = TINYFS_BLOCK_SIZE / sizeof(uint32_t);
        for (int j = 0; j < entries_per_block; j++) {
            int idx = i * entries_per_block + j;
            if (idx < 1024) {
                ((uint32_t*)buffer)[j] = data->fat[idx];
            }
        }
        
        if (ata_write_sectors(data->sb.fat_start + i, 1, buffer) != 0) {
            char err[] = "[TINYFS] write_fat: FAILED at block %d\n";
            printk(0xFFFF0000, 0x000000, err, i);
            return -1;
        }
    }
    
    char ok[] = "[TINYFS] write_fat: SUCCESS\n";
    printk(0xFF00FF00, 0x000000, ok);
    return 0;
}

static int write_directory(tinyfs_data_t *data) {
    char msg[] = "[TINYFS] write_directory: Writing directory...\n";
    printk(0xFF00FFFF, 0x000000, msg);
    
    uint8_t buffer[TINYFS_BLOCK_SIZE];
    
    for (int i = 0; i < 10; i++) {
        // Clear buffer
        for (int k = 0; k < TINYFS_BLOCK_SIZE; k++) {
            buffer[k] = 0;
        }
        
        int entries_per_block = TINYFS_BLOCK_SIZE / sizeof(tinyfs_dirent_t);
        for (int j = 0; j < entries_per_block; j++) {
            int idx = i * entries_per_block + j;
            if (idx < TINYFS_MAX_FILES) {
                ((tinyfs_dirent_t*)buffer)[j] = data->dirents[idx];
            }
        }
        
        if (ata_write_sectors(data->sb.dir_start + i, 1, buffer) != 0) {
            char err[] = "[TINYFS] write_directory: FAILED at block %d\n";
            printk(0xFFFF0000, 0x000000, err, i);
            return -1;
        }
    }
    
    char ok[] = "[TINYFS] write_directory: SUCCESS\n";
    printk(0xFF00FF00, 0x000000, ok);
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
            char msg[] = "[TINYFS] allocate_block: Allocated block %d\n";
            printk(0xFF00FFFF, 0x000000, msg, i);
            return i;
        }
    }
    char err[] = "[TINYFS] allocate_block: No free blocks!\n";
    printk(0xFFFF0000, 0x000000, err);
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
    for (int i = 0; i < TINYFS_MAX_FILES; i++) {
        if (!data->dirents[i].used) {
            char msg[] = "[TINYFS] find_free_dirent: Found free slot at index %d\n";
            printk(0xFF00FFFF, 0x000000, msg, i);
            return i;
        }
    }
    char err[] = "[TINYFS] find_free_dirent: No free directory entries!\n";
    printk(0xFFFF0000, 0x000000, err);
    return -1;
}

// ============================================================================
// FILE HANDLE MANAGEMENT
// ============================================================================

static void init_file_handles(void) {
    if (file_handles_initialized) return;
    
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        file_handles[i].used = 0;
        file_handles[i].dirent = NULL;
        file_handles[i].dirent_index = -1;
        file_handles[i].open_flags = 0;
        file_handles[i].position = 0;
        file_handles[i].reference_count = 0;
        file_handles[i].is_dirty = 0;
        file_handles[i].cached_size = 0;
        file_handles[i].lock_mode = 0;
        file_handles[i].num_readers = 0;
        file_handles[i].has_writer = 0;
    }
    
    file_handles_initialized = 1;
}

static tinyfs_file_handle_t* find_file_handle(tinyfs_dirent_t *dirent) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (file_handles[i].used && file_handles[i].dirent == dirent) {
            return &file_handles[i];
        }
    }
    return NULL;
}

static tinyfs_file_handle_t* allocate_file_handle(void) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!file_handles[i].used) {
            file_handles[i].used = 1;
            file_handles[i].reference_count = 0;
            file_handles[i].is_dirty = 0;
            file_handles[i].position = 0;
            file_handles[i].lock_mode = 0;
            file_handles[i].num_readers = 0;
            file_handles[i].has_writer = 0;
            return &file_handles[i];
        }
    }
    return NULL;
}

static int flush_metadata(vfs_node_t *node) {
    if (!node || !node->fs || !node->fs->private_data) return -1;
    
    tinyfs_data_t *data = (tinyfs_data_t*)node->fs->private_data;
    
    if (write_superblock(data) != 0) return -1;
    if (write_fat(data) != 0) return -1;
    if (write_directory(data) != 0) return -1;
    
    return 0;
}

// ============================================================================
// FORMAT
// ============================================================================

int tinyfs_format(const char *device) {
    char msg[] = "[TINYFS] format: Formatting disk...\n";
    printk(0xFF00FFFF, 0x000000, msg);
    
    uint8_t buffer[TINYFS_BLOCK_SIZE];
    
    // Create superblock
    tinyfs_superblock_t *sb = (tinyfs_superblock_t*)buffer;
    sb->magic = TINYFS_MAGIC;
    sb->total_blocks = 1024;
    sb->fat_start = 1;
    sb->dir_start = 11;
    sb->data_start = 101;
    sb->free_blocks = 1024 - 101;
    
    if (ata_write_sectors(0, 1, buffer) != 0) {
        char err[] = "[TINYFS] format: Failed to write superblock\n";
        printk(0xFFFF0000, 0x000000, err);
        return -1;
    }
    
    // Initialize FAT
    for (int i = 1; i <= 10; i++) {
        for (int j = 0; j < TINYFS_BLOCK_SIZE / sizeof(uint32_t); j++) {
            uint32_t block_num = (i - 1) * (TINYFS_BLOCK_SIZE / sizeof(uint32_t)) + j;
            if (block_num < 1024) {
                if (block_num < 101) {
                    ((uint32_t*)buffer)[j] = EOF;
                } else {
                    ((uint32_t*)buffer)[j] = 0;
                }
            }
        }
        if (ata_write_sectors(i, 1, buffer) != 0) {
            char err[] = "[TINYFS] format: Failed to write FAT block %d\n";
            printk(0xFFFF0000, 0x000000, err, i);
            return -1;
        }
    }
    
    // Clear directory
    for (int i = 0; i < TINYFS_BLOCK_SIZE; i++) {
        buffer[i] = 0;
    }
    
    for (int i = 11; i <= 100; i++) {
        if (ata_write_sectors(i, 1, buffer) != 0) {
            char err[] = "[TINYFS] format: Failed to write dir block %d\n";
            printk(0xFFFF0000, 0x000000, err, i);
            return -1;
        }
    }
    
    char ok[] = "[TINYFS] format: SUCCESS\n";
    printk(0xFF00FF00, 0x000000, ok);
    return 0;
}

// ============================================================================
// MOUNT/UNMOUNT
// ============================================================================

static int tinyfs_mount(filesystem_t *fs, const char *device) {
    char msg[] = "[TINYFS] mount: Mounting...\n";
    printk(0xFF00FFFF, 0x000000, msg);
    
    tinyfs_data_t *data = (tinyfs_data_t*)kmalloc(sizeof(tinyfs_data_t));
    if (!data) {
        char err[] = "[TINYFS] mount: Failed to allocate memory\n";
        printk(0xFFFF0000, 0x000000, err);
        return -1;
    }
    
    strcpy_safe(data->device, device, 32);
    
    uint8_t buffer[TINYFS_BLOCK_SIZE];
    if (ata_read_sectors(0, 1, buffer) != 0) {
        char err[] = "[TINYFS] mount: Failed to read superblock\n";
        printk(0xFFFF0000, 0x000000, err);
        kfree(data);
        return -1;
    }
    
    tinyfs_superblock_t *sb = (tinyfs_superblock_t*)buffer;
    if (sb->magic != TINYFS_MAGIC) {
        char err[] = "[TINYFS] mount: Invalid magic: 0x%x\n";
        printk(0xFFFF0000, 0x000000, err, sb->magic);
        kfree(data);
        return -1;
    }
    
    data->sb = *sb;
    
    // Read directory
    int entries_per_block = TINYFS_BLOCK_SIZE / sizeof(tinyfs_dirent_t);
    for (int i = 0; i < 10; i++) {
        if (ata_read_sectors(data->sb.dir_start + i, 1, buffer) != 0) {
            char err[] = "[TINYFS] mount: Failed to read directory block %d\n";
            printk(0xFFFF0000, 0x000000, err, i);
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
    
    // Read FAT
    int fat_entries_per_block = TINYFS_BLOCK_SIZE / sizeof(uint32_t);
    for (int i = 0; i < 10; i++) {
        if (ata_read_sectors(data->sb.fat_start + i, 1, buffer) != 0) {
            char err[] = "[TINYFS] mount: Failed to read FAT block %d\n";
            printk(0xFFFF0000, 0x000000, err, i);
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
    
    fs->private_data = data;
    
    char ok[] = "[TINYFS] mount: SUCCESS (free_blocks=%u)\n";
    printk(0xFF00FF00, 0x000000, ok, data->sb.free_blocks);
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
    char msg[] = "[TINYFS] get_root: Creating root node\n";
    printk(0xFF00FFFF, 0x000000, msg);
    
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
    
    char ok[] = "[TINYFS] get_root: Root node created at %p\n";
    printk(0xFF00FF00, 0x000000, ok, root);
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
    if (!node || !node->fs || !node->fs->private_data) return -1;
    
    init_file_handles();
    
    tinyfs_data_t *data = (tinyfs_data_t*)node->fs->private_data;
    tinyfs_dirent_t *dirent = (tinyfs_dirent_t*)node->private_data;
    
    if (!dirent) return -1;
    if (dirent->is_directory) return -1;
    
    tinyfs_file_handle_t *handle = find_file_handle(dirent);
    
    if (handle) {
        handle->reference_count++;
    } else {
        handle = allocate_file_handle();
        if (!handle) return -1;
        
        handle->dirent = dirent;
        handle->open_flags = flags;
        handle->position = 0;
        handle->reference_count = 1;
        handle->cached_size = dirent->size;
        
        for (int i = 0; i < TINYFS_MAX_FILES; i++) {
            if (&data->dirents[i] == dirent) {
                handle->dirent_index = i;
                break;
            }
        }
    }
    
    node->private_data = handle;
    
    return 0;
}

static int tinyfs_close(vfs_node_t *node) {
    if (!node || !node->fs || !node->fs->private_data) return -1;
    
    tinyfs_file_handle_t *handle = (tinyfs_file_handle_t*)node->private_data;
    
    if (!handle || !handle->used) return -1;
    
    handle->reference_count--;
    
    if (handle->reference_count <= 0) {
        if (handle->is_dirty) {
            handle->dirent->size = handle->cached_size;
            flush_metadata(node);
            handle->is_dirty = 0;
        }
        
        handle->used = 0;
        handle->dirent = NULL;
        handle->dirent_index = -1;
        handle->position = 0;
    }
    
    node->private_data = NULL;
    
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
    
    tinyfs_file_handle_t *handle = (tinyfs_file_handle_t*)node->private_data;
    if (!handle) return -1;
    
    tinyfs_data_t *data = (tinyfs_data_t*)node->fs->private_data;
    tinyfs_dirent_t *dirent = handle->dirent;
    
    if (!dirent) return -1;
    
    if (dirent->first_block == 0) {
        int block = allocate_block(data);
        if (block < 0) return -1;
        dirent->first_block = block;
        handle->is_dirty = 1;
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
            handle->is_dirty = 1;
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
        handle->is_dirty = 1;
        
        if (bytes_written < size) {
            if (data->fat[current_block] == EOF) {
                int new_block = allocate_block(data);
                if (new_block < 0) break;
                data->fat[current_block] = new_block;
            }
            current_block = data->fat[current_block];
        }
    }
    
    if (offset + bytes_written > handle->cached_size) {
        handle->cached_size = offset + bytes_written;
        dirent->size = handle->cached_size;
        node->size = handle->cached_size;
    }
    
    return bytes_written;
}

// ============================================================================
// DIRECTORY OPERATIONS
// ============================================================================

static vfs_node_t* tinyfs_readdir(vfs_node_t *node, uint32_t index) {
    if (!node || node->type != FILE_TYPE_DIRECTORY) return NULL;
    if (!node->fs || !node->fs->private_data) return NULL;
    
    tinyfs_data_t *data = (tinyfs_data_t*)node->fs->private_data;
    
    uint32_t count = 0;
    for (int i = 0; i < TINYFS_MAX_FILES; i++) {
        if (data->dirents[i].used) {
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
    
    for (int i = 0; i < TINYFS_MAX_FILES; i++) {
        if (data->dirents[i].used && 
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
    char msg1[] = "[TINYFS] create_node: Called with name='%s', type=%d\n";
    printk(0xFF00FFFF, 0x000000, msg1, name, type);
    
    if (!parent) {
        char err[] = "[TINYFS] create_node: parent is NULL\n";
        printk(0xFFFF0000, 0x000000, err);
        return -1;
    }
    
    if (!parent->fs) {
        char err[] = "[TINYFS] create_node: parent->fs is NULL\n";
        printk(0xFFFF0000, 0x000000, err);
        return -1;
    }
    
    if (!parent->fs->private_data) {
        char err[] = "[TINYFS] create_node: parent->fs->private_data is NULL\n";
        printk(0xFFFF0000, 0x000000, err);
        return -1;
    }
    
    if (parent->type != FILE_TYPE_DIRECTORY) {
        char err[] = "[TINYFS] create_node: parent is not a directory (type=%d)\n";
        printk(0xFFFF0000, 0x000000, err, parent->type);
        return -1;
    }
    
    char msg2[] = "[TINYFS] create_node: Parent checks passed\n";
    printk(0xFF00FFFF, 0x000000, msg2);
    
    tinyfs_data_t *data = (tinyfs_data_t*)parent->fs->private_data;
    
    char msg3[] = "[TINYFS] create_node: Got filesystem data at %p\n";
    printk(0xFF00FFFF, 0x000000, msg3, data);
    
    // Check for duplicates
    char msg4[] = "[TINYFS] create_node: Checking for duplicates...\n";
    printk(0xFF00FFFF, 0x000000, msg4);
    
    for (int i = 0; i < TINYFS_MAX_FILES; i++) {
        if (data->dirents[i].used && 
            strcmp_safe(data->dirents[i].name, name) == 0) {
            char err[] = "[TINYFS] create_node: File '%s' already exists\n";
            printk(0xFFFF0000, 0x000000, err, name);
            return -1;
        }
    }
    
    char msg5[] = "[TINYFS] create_node: No duplicates found\n";
    printk(0xFF00FFFF, 0x000000, msg5);
    
    int free_idx = find_free_dirent(data);
    if (free_idx < 0) {
        char err[] = "[TINYFS] create_node: No free directory entries\n";
        printk(0xFFFF0000, 0x000000, err);
        return -1;
    }
    
    char msg6[] = "[TINYFS] create_node: Found free slot at index %d\n";
    printk(0xFF00FFFF, 0x000000, msg6, free_idx);
    
    tinyfs_dirent_t *dirent = &data->dirents[free_idx];
    strcpy_safe(dirent->name, name, TINYFS_MAX_FILENAME);
    dirent->first_block = 0;
    dirent->size = 0;
    dirent->is_directory = (type == FILE_TYPE_DIRECTORY) ? 1 : 0;
    dirent->used = 1;
    
    char msg7[] = "[TINYFS] create_node: Directory entry created in memory\n";
    printk(0xFF00FFFF, 0x000000, msg7);
    char msg8[] = "[TINYFS] create_node:   name='%s'\n";
    printk(0xFF00FFFF, 0x000000, msg8, dirent->name);
    char msg9[] = "[TINYFS] create_node:   used=%d\n";
    printk(0xFF00FFFF, 0x000000, msg9, dirent->used);
    char msg10[] = "[TINYFS] create_node:   is_directory=%d\n";
    printk(0xFF00FFFF, 0x000000, msg10, dirent->is_directory);
    
    char msg11[] = "[TINYFS] create_node: Writing directory to disk...\n";
    printk(0xFF00FFFF, 0x000000, msg11);
    
    if (write_directory(data) != 0) {
        char err[] = "[TINYFS] create_node: write_directory FAILED\n";
        printk(0xFFFF0000, 0x000000, err);
        dirent->used = 0;
        return -1;
    }
    
    char msg12[] = "[TINYFS] create_node: Directory written successfully\n";
    printk(0xFF00FFFF, 0x000000, msg12);
    
    if (write_fat(data) != 0) {
        char err[] = "[TINYFS] create_node: write_fat FAILED\n";
        printk(0xFFFF0000, 0x000000, err);
        dirent->used = 0;
        return -1;
    }
    
    char msg13[] = "[TINYFS] create_node: FAT written successfully\n";
    printk(0xFF00FFFF, 0x000000, msg13);
    
    if (write_superblock(data) != 0) {
        char err[] = "[TINYFS] create_node: write_superblock FAILED\n";
        printk(0xFFFF0000, 0x000000, err);
        dirent->used = 0;
        return -1;
    }
    
    char msg14[] = "[TINYFS] create_node: Superblock written successfully\n";
    printk(0xFF00FFFF, 0x000000, msg14);
    
    char ok[] = "[TINYFS] create_node: SUCCESS! Created '%s' at index %d\n";
    printk(0xFF00FF00, 0x000000, ok, name, free_idx);
    
    return 0;
}

static int tinyfs_unlink(vfs_node_t *parent, const char *name) {
    if (!parent || !parent->fs || !parent->fs->private_data) return -1;
    if (parent->type != FILE_TYPE_DIRECTORY) return -1;
    
    tinyfs_data_t *data = (tinyfs_data_t*)parent->fs->private_data;
    
    for (int i = 0; i < TINYFS_MAX_FILES; i++) {
        if (data->dirents[i].used && 
            strcmp_safe(data->dirents[i].name, name) == 0) {
            
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
            
            return 0;
        }
    }
    
    return -1;
}

// ============================================================================
// CREATE FILESYSTEM
// ============================================================================

filesystem_t* tinyfs_create(void) {
    char msg[] = "[TINYFS] create: Creating filesystem structure\n";
    printk(0xFF00FFFF, 0x000000, msg);
    
    filesystem_t *fs = (filesystem_t*)kmalloc(sizeof(filesystem_t));
    if (!fs) {
        char err[] = "[TINYFS] create: Failed to allocate memory\n";
        printk(0xFFFF0000, 0x000000, err);
        return NULL;
    }
    
    strcpy_safe(fs->name, "tinyfs", 32);
    fs->ops = &tinyfs_fs_ops;
    fs->private_data = NULL;
    
    char ok[] = "[TINYFS] create: Filesystem created at %p\n";
    printk(0xFF00FF00, 0x000000, ok, fs);
    char ops[] = "[TINYFS] create: ops = %p\n";
    printk(0xFF00FF00, 0x000000, ops, fs->ops);
    char mount_ptr[] = "[TINYFS] create: ops->mount = %p\n";
    printk(0xFF00FF00, 0x000000, mount_ptr, fs->ops->mount);
    
    return fs;
}