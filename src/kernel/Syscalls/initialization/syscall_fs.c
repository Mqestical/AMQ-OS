#include "syscall.h"
#include "vfs.h"
#include "print.h"

// Helper to validate user pointers
static int validate_user_pointer(const void *ptr, size_t size) {
    // TODO: Implement proper validation
    // For now, just check for NULL
    return (ptr != NULL);
}

int64_t sys_open(const char *path, int flags, int mode) {
    if (!validate_user_pointer(path, 1)) {
        return -EINVAL;
    }
    
    char msg[] = "[SYSCALL] open('%s', %d, %d)\n";
    printk(0xFFFFFF00, 0x000000, msg, path, flags, mode);
    
    int fd = vfs_open(path, flags);
    return fd;
}

int64_t sys_close(int fd) {
    if (fd < 0) {
        return -EBADF;
    }
    
    return vfs_close(fd);
}

int64_t sys_read(int fd, void *buf, size_t count) {
    if (fd < 0 || !validate_user_pointer(buf, count)) {
        return -EINVAL;
    }
    
    return vfs_read(fd, (uint8_t*)buf, count);
}

int64_t sys_write(int fd, const void *buf, size_t count) {
    if (fd < 0 || !validate_user_pointer(buf, count)) {
        return -EINVAL;
    }
    
    return vfs_write(fd, (uint8_t*)buf, count);
}

int64_t sys_lseek(int fd, int64_t offset, int whence) {
    if (fd < 0) {
        return -EBADF;
    }
    
    return vfs_seek(fd, offset, whence);
}

int64_t sys_stat(const char *path, sys_stat_t *statbuf) {
    if (!validate_user_pointer(path, 1) || !validate_user_pointer(statbuf, sizeof(sys_stat_t))) {
        return -EINVAL;
    }
    
    char msg[] = "[SYSCALL] stat('%s') - not fully implemented\n";
    printk(0xFFFFFF00, 0x000000, msg, path);
    
    // TODO: Implement full stat
    return -ENOSYS;
}

int64_t sys_unlink(const char *path) {
    if (!validate_user_pointer(path, 1)) {
        return -EINVAL;
    }
    
    return vfs_unlink(path);
}

int64_t sys_mkdir(const char *path, uint32_t mode) {
    if (!validate_user_pointer(path, 1)) {
        return -EINVAL;
    }
    
    char msg[] = "[SYSCALL] mkdir('%s', %u)\n";
    printk(0xFFFFFF00, 0x000000, msg, path, mode);
    
    return vfs_mkdir(path, mode);
}

int64_t sys_rmdir(const char *path) {
    if (!validate_user_pointer(path, 1)) {
        return -EINVAL;
    }
    
    return vfs_unlink(path);  // Same as unlink for now
}

int64_t sys_chdir(const char *path) {
    if (!validate_user_pointer(path, 1)) {
        return -EINVAL;
    }
    
    return vfs_chdir(path);
}

int64_t sys_getcwd(char *buf, size_t size) {
    if (!validate_user_pointer(buf, size)) {
        return -EINVAL;
    }
    
    const char *cwd = vfs_get_cwd_path();
    
    // Copy to user buffer
    size_t i = 0;
    while (cwd[i] && i < size - 1) {
        buf[i] = cwd[i];
        i++;
    }
    buf[i] = '\0';
    
    return i;
}

int64_t sys_uptime(void) {
    extern uint64_t get_uptime_seconds(void);
    return get_uptime_seconds();
}

int64_t sys_gettime(void) {
    extern uint64_t get_timer_ticks(void);
    return get_timer_ticks();
}

int64_t sys_debug_print(const char *str) {
    if (!validate_user_pointer(str, 1)) {
        return -EINVAL;
    }
    
    printk(0xFF00FFFF, 0x000000, "[USER] %s", str);
    return 0;
}