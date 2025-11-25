#ifndef TINYFS_H
#define TINYFS_H

#include <stdint.h>
#include "vfs.h"

#define TINYFS_MAGIC 0x54494E59  // "TINY"
#define TINYFS_BLOCK_SIZE 512
#define TINYFS_MAX_FILENAME 32
#define TINYFS_MAX_FILES 256

// Superblock structure
typedef struct {
    uint32_t magic;
    uint32_t total_blocks;
    uint32_t fat_start;      // Block number where FAT starts
    uint32_t dir_start;      // Block number where directory starts
    uint32_t data_start;     // Block number where data blocks start
    uint32_t free_blocks;
} tinyfs_superblock_t;

typedef struct {
    char name[TINYFS_MAX_FILENAME];
    uint32_t first_block;
    uint32_t size;
    uint8_t is_directory;
    uint8_t used;
    uint32_t parent_inode;  //inode of parent directory (0 for root)
    uint8_t padding[2];     // keep alignment
} tinyfs_dirent_t;

// In-memory filesystem data
typedef struct {
    tinyfs_superblock_t sb;
    uint32_t fat[1024];              // File allocation table
    tinyfs_dirent_t dirents[TINYFS_MAX_FILES];
    char device[32];
} tinyfs_data_t;

// Function prototypes
int tinyfs_format(const char *device);
filesystem_t* tinyfs_create(void);
static int strcmp_safe(const char *s1, const char *s2);
#endif