
#include <efi.h>
#include <efilib.h>
#include "print.h"
#include "memory.h"
#include "idt.h"
#include "PICR.h"
#include "keyboard.h"
#include "shell.h"
#include "TSS.h"
#include "serial.h"
#include "IO.h"
#include "vfs.h"
#include "tinyfs.h"
#include "ata.h"
#include "definitions.h"
#include "process.h"
#include "irq.h"
#include "fg.h"
#include "syscall.h"
#include "string_helpers.h"
#include "mouse.h"

extern void syscall_register_all(void);
extern void pmm_init(EFI_MEMORY_DESCRIPTOR* map, UINTN desc_count, UINTN desc_size);
extern void init_kernel_heap(void);
extern vfs_node_t* vfs_get_root(void);

static inline void enable_io_privilege(void) {
    __asm__ volatile(
        "pushfq\n"
        "pop %%rax\n"
        "or $0x3000, %%rax\n"
        "push %%rax\n"
        "popfq\n"
        ::: "rax", "memory"
    );
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);

    uefi_call_wrapper(ST->ConOut->ClearScreen, 1, ST->ConOut);
    uefi_call_wrapper(ST->ConOut->SetCursorPosition, 3, ST->ConOut, 0, 0);
    uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, L"AMQ OS - Booting...\r\n");

    EFI_MEMORY_DESCRIPTOR *memory_map = NULL;
    UINTN memory_map_size = 0;
    UINTN map_key = 0;
    UINTN descriptor_size = 0;
    UINT32 descriptor_version = 0;

    uefi_call_wrapper(BS->GetMemoryMap, 5, &memory_map_size, memory_map, &map_key, &descriptor_size, &descriptor_version);
    memory_map_size += 2 * descriptor_size;
    uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, memory_map_size, (void**)&memory_map);
    uefi_call_wrapper(BS->GetMemoryMap, 5, &memory_map_size, memory_map, &map_key, &descriptor_size, &descriptor_version);

    UINTN desc_count = memory_map_size / descriptor_size;

    pmm_init(memory_map, desc_count, descriptor_size);
    init_kernel_heap();

    if (stackalloc(16, 4096) != EXIT_SUCCESS) {
        uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, L"Failed to allocate kernel stack!\r\n");
        goto boot_failed;
    }

    init_graphics(ST);


    EFI_STATUS status = uefi_call_wrapper(BS->ExitBootServices, 2, ImageHandle, map_key);
    if (EFI_ERROR(status)) {
        uefi_call_wrapper(BS->GetMemoryMap, 5, &memory_map_size, memory_map, &map_key, &descriptor_size, &descriptor_version);
        status = uefi_call_wrapper(BS->ExitBootServices, 2, ImageHandle, map_key);
        if (EFI_ERROR(status)) {
            while(1) __asm__ volatile("hlt");
        }
    }

    ClearScreen(BLACK);
    SetCursorPos(0, 0);

    PRINT(WHITE, BLACK, "AMQ OS Kernel v2.6\n");
    PRINT(WHITE, BLACK, "==================\n\n");

    enable_io_privilege();
    PRINT(GREEN, BLACK, "[OK] I/O privileges enabled\n");

    PRINT(GREEN, BLACK, "[OK] Stack: base=0x%llx, top=0x%llx\n",
          kernel_stack_base, kernel_stack_top);

    tss_init();
    PRINT(GREEN, BLACK, "[OK] TSS initialized\n");

    gdt_install();
    PRINT(GREEN, BLACK, "[OK] GDT installed\n");

    pic_remap();
    PRINT(GREEN, BLACK, "[OK] PIC remapped\n");

    idt_install();
    PRINT(GREEN, BLACK, "[OK] IDT installed\n");

    PRINT(WHITE, BLACK, "\n[INIT] Initializing syscall interface...\n");

    syscall_init();
    syscall_register_all();

    PRINT(GREEN, BLACK, "[OK] Syscalls ready\n");

    serial_init(COM1);
    PRINT(GREEN, BLACK, "[OK] Serial initialized\n");


    PRINT(WHITE, BLACK, "\n[INIT] Initializing job system...\n");
    jobs_init();
    jobs_set_active(0);
    PRINT(GREEN, BLACK, "[OK] Job system initialized (INACTIVE)\n");

    PRINT(WHITE, BLACK, "\n[INIT] Enabling IRQ system...\n");
    irq_init();
    PRINT(GREEN, BLACK, "[OK] IRQ system enabled\n");


    PRINT(WHITE, BLACK, "\n[TEST] Testing timer for 3 seconds...\n");

    extern volatile uint64_t timer_ticks;
    uint64_t start_ticks = timer_ticks;

    for (int sec = 1; sec <= 3; sec++) {
        uint64_t target = start_ticks + (sec * 1000);
        while (timer_ticks < target) {
            __asm__ volatile("hlt");
        }

        PRINT(GREEN, BLACK, "[TEST] Second %d: timer_ticks=%llu\n",
              sec, timer_ticks);
    }

    PRINT(GREEN, BLACK, "[OK] Timer is working correctly!\n");


    uint8_t mask = inb(0x21);
    mask &= ~0x02;
    outb(0x21, mask);
    PRINT(GREEN, BLACK, "[OK] Keyboard enabled\n");


    PRINT(WHITE, BLACK, "\n[INIT] Initializing storage...\n");
    ata_init();
    PRINT(GREEN, BLACK, "[OK] ATA initialized\n");

    PRINT(WHITE, BLACK, "\n[INIT] Initializing filesystem...\n");
    vfs_init();

    filesystem_t *tinyfs = tinyfs_create();
    if (!tinyfs) {
        PRINT(YELLOW, BLACK, "[ERROR] Failed to create TinyFS\n");
        goto boot_failed;
    }

    if (vfs_register_filesystem(tinyfs) != 0) {
        PRINT(YELLOW, BLACK, "[ERROR] Failed to register TinyFS\n");
        goto boot_failed;
    }

    PRINT(WHITE, BLACK, "[INIT] Formatting disk...\n");

    char device_name[] = "ata0";
    if (tinyfs_format(device_name) != 0) {
        PRINT(YELLOW, BLACK, "[ERROR] Format failed\n");
        goto boot_failed;
    }

    PRINT(GREEN, BLACK, "[OK] Disk formatted\n");

    for (volatile int i = 0; i < 10000000; i++);

    PRINT(WHITE, BLACK, "[INIT] Mounting filesystem...\n");

    char fs_type[] = "tinyfs";
    char device[] = "ata0";
    char mountpoint[] = "/";

    if (vfs_mount(fs_type, device, mountpoint) != 0) {
        PRINT(YELLOW, BLACK, "[ERROR] Mount failed\n");
        goto boot_failed;
    }

    PRINT(GREEN, BLACK, "[OK] Filesystem mounted\n");


    PRINT(WHITE, BLACK, "\n[INIT] Initializing processes...\n");
    process_init();
    scheduler_init();

    extern cmd_thread_data_t bg_thread_data[MAX_JOBS];
    for (int i = 0; i < MAX_JOBS; i++) {
        bg_thread_data[i].job_id = 0;
        bg_thread_data[i].command[0] = '\0';
    }

    init_kernel_threads();
    PRINT(GREEN, BLACK, "[OK] Threads initialized (scheduler DISABLED)\n");


    PRINT(GREEN, BLACK, "\n=== Boot Complete ===\n");

    jobs_set_active(1);
    PRINT(GREEN, BLACK, "[OK] Job tracking ENABLED\n");
    PRINT(GREEN, BLACK, "\nStarting shell...\n\n");
    for (volatile int i = 0; i < 5000000; i++);

    init_shell();

    while(1) __asm__ volatile("hlt");

boot_failed:
    PRINT(YELLOW, BLACK, "\n=== BOOT FAILED ===\n");
    while(1) __asm__ volatile("hlt");

    return EFI_SUCCESS;
}