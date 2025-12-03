#ifndef TINYFS_H
#define TINYFS_H

#include <stdint.h>
#include "vfs.h"

#define TINYFS_MAGIC 0x54494E59
#define TINYFS_BLOCK_SIZE 512
#define TINYFS_MAX_FILENAME 32
#define TINYFS_MAX_FILES 256

typedef struct {
    uint32_t magic;
    uint32_t total_blocks;
    uint32_t fat_start;
    uint32_t dir_start;
    uint32_t data_start;
    uint32_t free_blocks;
} tinyfs_superblock_t;

typedef struct {
    char name[TINYFS_MAX_FILENAME];
    uint32_t first_block;
    uint32_t size;
    uint8_t is_directory;
    uint8_t used;
    uint32_t parent_inode;
    uint8_t padding[2];
} tinyfs_dirent_t;

typedef struct {
    tinyfs_superblock_t sb;
    uint32_t fat[1024];
    tinyfs_dirent_t dirents[TINYFS_MAX_FILES];
    char device[32];
} tinyfs_data_t;

int tinyfs_format(const char *device);
filesystem_t* tinyfs_create(void);
static int strcmp_safe(const char *s1, const char *s2);
#endif