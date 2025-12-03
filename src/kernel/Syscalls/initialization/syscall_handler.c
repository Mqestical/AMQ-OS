#include "string_helpers.h"
#include "syscall.h"
#include "print.h"

typedef int64_t (*syscall_fn_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

static int64_t syscall_not_implemented(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
    PRINT(YELLOW, BLACK, "[SYSCALL] Not implemented\n");
    return -ENOSYS;
}

static syscall_fn_t syscall_table[MAX_SYSCALLS] = {
    [0 ... MAX_SYSCALLS-1] = (syscall_fn_t)syscall_not_implemented,
};

static void register_syscall(int num, syscall_fn_t handler) {
    if (num >= 0 && num < MAX_SYSCALLS) syscall_table[num] = handler;
}

uint64_t syscall_handler(uint64_t syscall_num, 
                         uint64_t arg1, uint64_t arg2, 
                         uint64_t arg3, uint64_t arg4,
                         uint64_t arg5, uint64_t arg6) {
    if (syscall_num >= MAX_SYSCALLS) {
        PRINT(YELLOW, BLACK, "[SYSCALL] Invalid syscall number: %llu\n", syscall_num);
        return -ENOSYS;
    }

    syscall_fn_t handler = syscall_table[syscall_num];
    return handler(arg1, arg2, arg3, arg4, arg5, arg6);
}

void syscall_register_all(void) {
    register_syscall(SYS_EXIT, (syscall_fn_t)sys_exit);
    register_syscall(SYS_FORK, (syscall_fn_t)sys_fork);
    register_syscall(SYS_GETPID, (syscall_fn_t)sys_getpid);
    register_syscall(SYS_GETPPID, (syscall_fn_t)sys_getppid);
    register_syscall(SYS_THREAD_CREATE, (syscall_fn_t)sys_thread_create);
    register_syscall(SYS_THREAD_EXIT, (syscall_fn_t)sys_thread_exit);
    register_syscall(SYS_THREAD_YIELD, (syscall_fn_t)sys_thread_yield);
    register_syscall(SYS_SLEEP, (syscall_fn_t)sys_sleep);
    
    register_syscall(SYS_OPEN, (syscall_fn_t)sys_open);
    register_syscall(SYS_CLOSE, (syscall_fn_t)sys_close);
    register_syscall(SYS_READ, (syscall_fn_t)sys_read);
    register_syscall(SYS_WRITE, (syscall_fn_t)sys_write);
    register_syscall(SYS_LSEEK, (syscall_fn_t)sys_lseek);
    register_syscall(SYS_STAT, (syscall_fn_t)sys_stat);
    register_syscall(SYS_UNLINK, (syscall_fn_t)sys_unlink);
    
    register_syscall(SYS_MKDIR, (syscall_fn_t)sys_mkdir);
    register_syscall(SYS_RMDIR, (syscall_fn_t)sys_rmdir);
    register_syscall(SYS_CHDIR, (syscall_fn_t)sys_chdir);
    register_syscall(SYS_GETCWD, (syscall_fn_t)sys_getcwd);
    
    register_syscall(SYS_UPTIME, (syscall_fn_t)sys_uptime);
    register_syscall(SYS_GETTIME, (syscall_fn_t)sys_gettime);
    register_syscall(SYS_DEBUG_PRINT, (syscall_fn_t)sys_debug_print);

    PRINT(MAGENTA, BLACK, "[SYSCALL] Registered %d syscalls\n", 20);
}
