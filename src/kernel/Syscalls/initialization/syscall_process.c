#include "syscall.h"
#include "process.h"
#include "print.h"
#include "sleep.h"
#include "string_helpers.h"

int64_t sys_exit(int status) {
    PRINT(0xFFFFFF00, 0x000000, "[SYSCALL] exit(%d)\n", status);
    thread_exit();
    return 0;
}

int64_t sys_fork(void) {
    PRINT(0xFFFF0000, 0x000000, "[SYSCALL] fork() - not implemented yet\n");
    return -ENOSYS;
}

int64_t sys_getpid(void) {
    thread_t *current = get_current_thread();
    if (!current || !current->parent) return -1;
    return current->parent->pid;
}

int64_t sys_getppid(void) {
    return 1;
}

int64_t sys_exec(const char *path) {
    PRINT(0xFFFF0000, 0x000000, "[SYSCALL] exec('%s') - not implemented yet\n", path);
    return -ENOSYS;
}

int64_t sys_thread_create(void (*entry)(void), uint32_t stack_size) {
    thread_t *current = get_current_thread();
    if (!current || !current->parent) return -EINVAL;
    if (!entry || stack_size < 4096) return -EINVAL;

    int tid = thread_create(
        current->parent->pid,
        entry,
        stack_size,
        50000000,
        500000000,
        500000000
    );

    if (tid < 0) return -ENOMEM;

    PRINT(0xFF00FF00, 0x000000, "[SYSCALL] Created thread TID=%d\n", tid);
    return tid;
}

int64_t sys_thread_exit(void) {
    PRINT(0xFFFFFF00, 0x000000, "[SYSCALL] thread_exit()\n");
    thread_exit();
    return 0;
}

int64_t sys_thread_yield(void) {
    thread_yield();
    return 0;
}

int64_t sys_sleep(uint32_t seconds) {
    if (seconds > 3600) return -EINVAL;
    sleep_seconds(seconds);
    return 0;
}
