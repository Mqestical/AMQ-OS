#include "syscall.h"
#include "process.h"
#include "print.h"
#include "sleep.h"

int64_t sys_exit(int status) {
    char msg[] = "[SYSCALL] exit(%d)\n";
    printk(0xFFFFFF00, 0x000000, msg, status);
    
    thread_exit();
    
    // Should never reach here
    return 0;
}

int64_t sys_fork(void) {
    char msg[] = "[SYSCALL] fork() - not implemented yet\n";
    printk(0xFFFF0000, 0x000000, msg);
    return -ENOSYS;
}

int64_t sys_getpid(void) {
    thread_t *current = get_current_thread();
    if (!current || !current->parent) {
        return -1;
    }
    return current->parent->pid;
}

int64_t sys_getppid(void) {
    // For now, all processes have init (PID 1) as parent
    return 1;
}

int64_t sys_exec(const char *path) {
    char msg[] = "[SYSCALL] exec('%s') - not implemented yet\n";
    printk(0xFFFF0000, 0x000000, msg, path);
    return -ENOSYS;
}

int64_t sys_thread_create(void (*entry)(void), uint32_t stack_size) {
    thread_t *current = get_current_thread();
    if (!current || !current->parent) {
        return -EINVAL;
    }
    
    if (!entry || stack_size < 4096) {
        return -EINVAL;
    }
    
    int tid = thread_create(
        current->parent->pid,
        entry,
        stack_size,
        50000000,   // 50ms runtime
        500000000,  // 500ms deadline
        500000000   // 500ms period
    );
    
    if (tid < 0) {
        return -ENOMEM;
    }
    
    char msg[] = "[SYSCALL] Created thread TID=%d\n";
    printk(0xFF00FF00, 0x000000, msg, tid);
    
    return tid;
}

int64_t sys_thread_exit(void) {
    char msg[] = "[SYSCALL] thread_exit()\n";
    printk(0xFFFFFF00, 0x000000, msg);
    
    thread_exit();
    
    // Never returns
    return 0;
}

int64_t sys_thread_yield(void) {
    thread_yield();
    return 0;
}

int64_t sys_sleep(uint32_t seconds) {
    if (seconds > 3600) {  // Max 1 hour
        return -EINVAL;
    }
    
    sleep_seconds(seconds);
    return 0;
}