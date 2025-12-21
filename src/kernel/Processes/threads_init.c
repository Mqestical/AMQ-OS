#include "process.h"
#include "print.h"
#include "string_helpers.h"
#include "sleep.h"





void idle_thread_entry(void) {
    PRINT(MAGENTA, BLACK, "[IDLE] Started\n");
    while (1) {

        thread_yield();


        for (volatile int i = 0; i < 1000; i++);
    }
}





void test_thread_entry(void) {
    PRINT(YELLOW, BLACK, "[TEST] Thread started!\n");

    volatile uint64_t counter = 0;
    while (1) {
        counter++;

        if (counter % 100000000 == 0) {
            PRINT(MAGENTA, BLACK, "[TEST] tick %llu\n", counter / 100000000);
        }

        if (counter % 10000000 == 0) {
            thread_yield();
        }
    }
}





void init_kernel_threads(void) {
    PRINT(MAGENTA, BLACK, "\n=== Initializing Kernel Threads ===\n");


    char initproc[] = "init";
    int init_pid = process_create(initproc, 0);
    if (init_pid < 0) {
        PRINT(YELLOW, BLACK, "[ERROR] Failed to create init process\n");
        return;
    }

    const uint32_t THREAD_STACK_SIZE = 65536;


    int idle_tid = thread_create(init_pid, idle_thread_entry,
                                  THREAD_STACK_SIZE,
                                  10000000,
                                  1000000000,
                                  1000000000);
    if (idle_tid < 0) {
        PRINT(YELLOW, BLACK, "[ERROR] Failed to create idle thread\n");
        return;
    }
    PRINT(MAGENTA, BLACK, "[OK] Idle thread TID=%d\n", idle_tid);





    PRINT(MAGENTA, BLACK, "[INFO] Created 2 kernel threads\n");
}