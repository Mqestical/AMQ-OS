// ============================================================================
// syscall.h - Syscall Definitions and Prototypes
// ============================================================================

#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#include <stddef.h>

typedef long ssize_t;
typedef long long off_t;

// ============================================================================
// SYSCALL NUMBERS
// ============================================================================

// Process/Thread Management (0-19)
#define SYS_EXIT            0
#define SYS_FORK            1
#define SYS_GETPID          2
#define SYS_GETPPID         3
#define SYS_WAIT            4
#define SYS_EXEC            5
#define SYS_THREAD_CREATE   6
#define SYS_THREAD_EXIT     7
#define SYS_THREAD_YIELD    8
#define SYS_THREAD_JOIN     9
#define SYS_SLEEP           10

// File Operations (20-39)
#define SYS_OPEN            20
#define SYS_CLOSE           21
#define SYS_READ            22
#define SYS_WRITE           23
#define SYS_LSEEK           24
#define SYS_STAT            25
#define SYS_FSTAT           26
#define SYS_UNLINK          27

// Directory Operations (40-49)
#define SYS_MKDIR           40
#define SYS_RMDIR           41
#define SYS_CHDIR           42
#define SYS_GETCWD          43
#define SYS_READDIR         44

// System Information (60-69)
#define SYS_UPTIME          63
#define SYS_GETTIME         64

// Debug (100+)
#define SYS_DEBUG_PRINT     100

#define MAX_SYSCALLS        128

// ============================================================================
// ERROR CODES
// ============================================================================

#define ENOSYS      1   // Function not implemented
#define EINVAL      2   // Invalid argument
#define EBADF       3   // Bad file descriptor
#define ENOMEM      4   // Out of memory
#define ENOENT      5   // No such file or directory
#define EEXIST      6   // File exists
#define ENOTDIR     7   // Not a directory
#define EISDIR      8   // Is a directory
#define EPERM       9   // Operation not permitted
#define EACCES      10  // Permission denied

// ============================================================================
// MSR DEFINITIONS
// ============================================================================

#define MSR_EFER        0xC0000080
#define MSR_STAR        0xC0000081
#define MSR_LSTAR       0xC0000082
#define MSR_CSTAR       0xC0000083
#define MSR_SFMASK      0xC0000084

#define EFER_SCE        (1 << 0)   // System Call Extensions

// RFLAGS bits to mask
#define RFLAGS_IF       (1 << 9)   // Interrupt Flag
#define RFLAGS_TF       (1 << 8)   // Trap Flag
#define RFLAGS_DF       (1 << 10)  // Direction Flag

// ============================================================================
// STRUCTURES
// ============================================================================

typedef struct {
    uint32_t st_dev;
    uint32_t st_ino;
    uint32_t st_mode;
    uint32_t st_size;
    uint64_t st_atime;
    uint64_t st_mtime;
} sys_stat_t;

// ============================================================================
// MSR ACCESS (inline assembly)
// ============================================================================

static inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = value & 0xFFFFFFFF;
    uint32_t high = value >> 32;
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void syscall_init(void);

// ============================================================================
// SYSCALL IMPLEMENTATIONS
// ============================================================================

int64_t sys_exit(int status);
int64_t sys_fork(void);
int64_t sys_getpid(void);
int64_t sys_getppid(void);
int64_t sys_exec(const char *path);
int64_t sys_thread_create(void (*entry)(void), uint32_t stack_size);
int64_t sys_thread_exit(void);
int64_t sys_thread_yield(void);
int64_t sys_sleep(uint32_t seconds);

int64_t sys_open(const char *path, int flags, int mode);
int64_t sys_close(int fd);
int64_t sys_read(int fd, void *buf, size_t count);
int64_t sys_write(int fd, const void *buf, size_t count);
int64_t sys_lseek(int fd, int64_t offset, int whence);
int64_t sys_stat(const char *path, sys_stat_t *statbuf);
int64_t sys_unlink(const char *path);

int64_t sys_mkdir(const char *path, uint32_t mode);
int64_t sys_rmdir(const char *path);
int64_t sys_chdir(const char *path);
int64_t sys_getcwd(char *buf, size_t size);

int64_t sys_uptime(void);
int64_t sys_gettime(void);
int64_t sys_debug_print(const char *str);

#endif // SYSCALL_H