// ============================================================================
// syscall_user.c - User-Space Syscall Wrappers
// ============================================================================
// These functions execute the SYSCALL instruction to invoke kernel functions

#include "syscall.h"

// ============================================================================
// RAW SYSCALL INVOCATION (inline assembly)
// ============================================================================

// Syscall with 0 arguments
static inline int64_t syscall0(uint64_t num) {
    int64_t ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(num)
        : "rcx", "r11", "memory"
    );
    return ret;
}

// Syscall with 1 argument
static inline int64_t syscall1(uint64_t num, uint64_t arg1) {
    int64_t ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(arg1)
        : "rcx", "r11", "memory"
    );
    return ret;
}

// Syscall with 2 arguments
static inline int64_t syscall2(uint64_t num, uint64_t arg1, uint64_t arg2) {
    int64_t ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(arg1), "S"(arg2)
        : "rcx", "r11", "memory"
    );
    return ret;
}

// Syscall with 3 arguments
static inline int64_t syscall3(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    int64_t ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3)
        : "rcx", "r11", "memory"
    );
    return ret;
}

// Syscall with 4 arguments
static inline int64_t syscall4(uint64_t num, uint64_t arg1, uint64_t arg2, 
                               uint64_t arg3, uint64_t arg4) {
    int64_t ret;
    register uint64_t r10 __asm__("r10") = arg4;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10)
        : "rcx", "r11", "memory"
    );
    return ret;
}

// Syscall with 5 arguments
static inline int64_t syscall5(uint64_t num, uint64_t arg1, uint64_t arg2, 
                               uint64_t arg3, uint64_t arg4, uint64_t arg5) {
    int64_t ret;
    register uint64_t r10 __asm__("r10") = arg4;
    register uint64_t r8 __asm__("r8") = arg5;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10), "r"(r8)
        : "rcx", "r11", "memory"
    );
    return ret;
}

// Syscall with 6 arguments
static inline int64_t syscall6(uint64_t num, uint64_t arg1, uint64_t arg2, 
                               uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    int64_t ret;
    register uint64_t r10 __asm__("r10") = arg4;
    register uint64_t r8 __asm__("r8") = arg5;
    register uint64_t r9 __asm__("r9") = arg6;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory"
    );
    return ret;
}

// ============================================================================
// USER-SPACE SYSCALL WRAPPERS
// ============================================================================

// Process Management
void exit(int status) {
    syscall1(SYS_EXIT, status);
    // Never returns
    while(1) __asm__ volatile("hlt");
}

int fork(void) {
    return syscall0(SYS_FORK);
}

int getpid(void) {
    return syscall0(SYS_GETPID);
}

int getppid(void) {
    return syscall0(SYS_GETPPID);
}

int thread_create_user(void (*entry)(void), uint32_t stack_size) {
    return syscall2(SYS_THREAD_CREATE, (uint64_t)entry, stack_size);
}

void thread_exit_user(void) {
    syscall0(SYS_THREAD_EXIT);
    while(1) __asm__ volatile("hlt");
}

void thread_yield_user(void) {
    syscall0(SYS_THREAD_YIELD);
}

int sleep_user(uint32_t seconds) {
    return syscall1(SYS_SLEEP, seconds);
}

// File Operations
int open(const char *path, int flags, int mode) {
    return syscall3(SYS_OPEN, (uint64_t)path, flags, mode);
}

int close(int fd) {
    return syscall1(SYS_CLOSE, fd);
}

ssize_t read(int fd, void *buf, size_t count) {
    return syscall3(SYS_READ, fd, (uint64_t)buf, count);
}

ssize_t write(int fd, const void *buf, size_t count) {
    return syscall3(SYS_WRITE, fd, (uint64_t)buf, count);
}

off_t lseek(int fd, off_t offset, int whence) {
    return syscall3(SYS_LSEEK, fd, offset, whence);
}

int stat(const char *path, sys_stat_t *statbuf) {
    return syscall2(SYS_STAT, (uint64_t)path, (uint64_t)statbuf);
}

int unlink(const char *path) {
    return syscall1(SYS_UNLINK, (uint64_t)path);
}

// Directory Operations
int mkdir(const char *path, uint32_t mode) {
    return syscall2(SYS_MKDIR, (uint64_t)path, mode);
}

int rmdir(const char *path) {
    return syscall1(SYS_RMDIR, (uint64_t)path);
}

int chdir(const char *path) {
    return syscall1(SYS_CHDIR, (uint64_t)path);
}

char* getcwd(char *buf, size_t size) {
    int ret = syscall2(SYS_GETCWD, (uint64_t)buf, size);
    return (ret >= 0) ? buf : NULL;
}

// System Info
uint64_t uptime(void) {
    return syscall0(SYS_UPTIME);
}

uint64_t gettime(void) {
    return syscall0(SYS_GETTIME);
}

// Debug
int debug_print(const char *str) {
    return syscall1(SYS_DEBUG_PRINT, (uint64_t)str);
}

// ============================================================================
// EXAMPLE USER PROGRAM
// ============================================================================

void example_user_thread(void) {
    debug_print("Hello from user thread!\n");
    
    // Create a file
    int fd = open("/test.txt", SYS_WRITE, 0);
    if (fd >= 0) {
        write(fd, "Hello World\n", 12);
        close(fd);
        debug_print("Created /test.txt\n");
    }
    
    // Read it back
    fd = open("/test.txt", SYS_READ, 0);
    if (fd >= 0) {
        char buf[64];
        int bytes = read(fd, buf, 63);
        if (bytes > 0) {
            buf[bytes] = '\0';
            debug_print("Read from file: ");
            debug_print(buf);
        }
        close(fd);
    }
    
    // Sleep for a bit
    debug_print("Sleeping for 2 seconds...\n");
    sleep_user(2);
    debug_print("Awake!\n");
    
    thread_exit_user();
}

void test_syscalls(void) {
    debug_print("\n=== Testing Syscalls ===\n");
    
    // Test getpid
    int pid = getpid();
    char msg1[] = "Current PID: %d\n";
    // Can't use printk from userspace, use debug_print
    
    // Test mkdir
    debug_print("Creating directory /testdir...\n");
    int ret = mkdir("/testdir", SYS_READ | SYS_WRITE);
    if (ret == 0) {
        debug_print("Success!\n");
    }
    
    // Test chdir
    debug_print("Changing to /testdir...\n");
    ret = chdir("/testdir");
    if (ret == 0) {
        char cwd[256];
        getcwd(cwd, 256);
        debug_print("Current directory: ");
        debug_print(cwd);
        debug_print("\n");
    }
    
    // Test thread creation
    debug_print("Creating user thread...\n");
    int tid = thread_create_user(example_user_thread, 8192);
    if (tid > 0) {
        debug_print("Created thread successfully\n");
    }
    
    debug_print("=== Syscall Tests Complete ===\n");
}

/*

// File Operations (20-39)
#define SYS_OPEN            20
#define SYS_CLOSE           21
#define SYS_READ            22
#define SYS_WRITE           23
#define SYS_LSEEK           24
#define SYS_STAT            25
#define SYS_FSTAT           26
#define SYS_UNLINK          27

*/
