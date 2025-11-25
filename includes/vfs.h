#ifndef VFS_H
#define VFS_H

#include <stdint.h>

#define MAX_FILENAME 256
#define MAX_OPEN_FILES 256  // Changed from 128 to 256 to match tinyfs.c

// File types
#define FILE_TYPE_REGULAR    0x01
#define FILE_TYPE_DIRECTORY  0x02
#define FILE_TYPE_CHARDEV    0x03
#define FILE_TYPE_BLOCKDEV   0x04
#define FILE_TYPE_PIPE       0x05
#define FILE_TYPE_SYMLINK    0x06
#define FILE_TYPE_MOUNTPOINT 0x08

// File flags
#define FILE_READ   0x01
#define FILE_WRITE  0x02
#define FILE_APPEND 0x04
#define FILE_CREATE 0x08

// Seek modes
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

// Forward declarations
typedef struct vfs_node vfs_node_t;
typedef struct filesystem filesystem_t;

// VFS operations structure
typedef struct vfs_operations {
    int (*open)(vfs_node_t *node, uint32_t flags);
    int (*close)(vfs_node_t *node);
    int (*read)(vfs_node_t *node, uint8_t *buffer, uint32_t size, uint32_t offset);
    int (*write)(vfs_node_t *node, uint8_t *buffer, uint32_t size, uint32_t offset);
    vfs_node_t* (*readdir)(vfs_node_t *node, uint32_t index);
    vfs_node_t* (*finddir)(vfs_node_t *node, const char *name);
    int (*create)(vfs_node_t *parent, const char *name, uint8_t type, uint32_t permissions);
    int (*unlink)(vfs_node_t *parent, const char *name);
} vfs_operations_t;

// VFS node structure
struct vfs_node {
    char name[MAX_FILENAME];
    uint8_t type;
    uint32_t permissions;
    uint32_t size;
    uint32_t inode;
    filesystem_t *fs;
    vfs_operations_t *ops;
    void *private_data;
};

// Filesystem statistics
typedef struct fs_stats {
    uint32_t total_blocks;
    uint32_t free_blocks;
    uint32_t block_size;
} fs_stats_t;

// Filesystem operations
typedef struct filesystem_operations {
    int (*mount)(filesystem_t *fs, const char *device);
    int (*unmount)(filesystem_t *fs);
    vfs_node_t* (*get_root)(filesystem_t *fs);
    int (*get_stats)(filesystem_t *fs, fs_stats_t *stats);
} filesystem_operations_t;

// Filesystem structure
struct filesystem {
    char name[32];
    filesystem_operations_t *ops;
    void *private_data;
};

// File descriptor structure
typedef struct file_descriptor {
    int used;
    vfs_node_t *node;
    uint32_t position;
    uint32_t flags;
} file_descriptor_t;

// VFS API functions
void vfs_init(void);
int vfs_register_filesystem(filesystem_t *fs);
int vfs_mount(const char *fs_type, const char *device, const char *mountpoint);

// File operations
int vfs_open(const char *path, uint32_t flags);
int vfs_close(int fd);
int vfs_read(int fd, uint8_t *buffer, uint32_t size);
int vfs_write(int fd, uint8_t *buffer, uint32_t size);
int vfs_seek(int fd, int offset, int whence);

// Directory operations
vfs_node_t* vfs_readdir(vfs_node_t *node, uint32_t index);
vfs_node_t* vfs_finddir(vfs_node_t *node, const char *name);
void vfs_list_directory(const char *path);

// Path resolution
vfs_node_t* vfs_resolve_path(const char *path);

// File/Directory creation and deletion
int vfs_create(const char *path, uint32_t permissions);
int vfs_mkdir(const char *path, uint32_t permissions);
int vfs_unlink(const char *path);

// Filesystem statistics
int vfs_statfs(const char *path, fs_stats_t *stats);
vfs_node_t* vfs_get_root(void);
static int str_len(const char *str);
void vfs_debug_root(void);
int vfs_register_filesystem(filesystem_t *fs);

// Change Directory Functions

vfs_node_t* vfs_get_cwd(void);
const char* vfs_get_cwd_path(void);
int vfs_chdir(const char *path);


#endif // VFS_H