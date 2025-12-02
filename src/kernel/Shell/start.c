// start.c - FIXED: All string literals replaced with local arrays

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

    // get memory map
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

    /*exit uefi BS*/
    EFI_STATUS status = uefi_call_wrapper(BS->ExitBootServices, 2, ImageHandle, map_key);
    if (EFI_ERROR(status)) {
        uefi_call_wrapper(BS->GetMemoryMap, 5, &memory_map_size, memory_map, &map_key, &descriptor_size, &descriptor_version);
        status = uefi_call_wrapper(BS->ExitBootServices, 2, ImageHandle, map_key);
        if (EFI_ERROR(status)) {
            while(1) __asm__ volatile("hlt");
        }
    }
    
    ClearScreen(0x000000);
    SetCursorPos(0, 0);

    PRINT(0xFFFFFFFF, 0x000000, "AMQ OS Kernel v0.2\n");
    PRINT(0xFFFFFFFF, 0x000000, "==================\n\n");

    enable_io_privilege();
    PRINT(0xFF00FF00, 0x000000, "[OK] I/O privileges enabled\n");

    PRINT(0xFF00FF00, 0x000000, "[OK] Stack: base=0x%llx, top=0x%llx\n", 
          kernel_stack_base, kernel_stack_top);

    tss_init();
    PRINT(0xFF00FF00, 0x000000, "[OK] TSS initialized\n");

    gdt_install();
    PRINT(0xFF00FF00, 0x000000, "[OK] GDT installed\n");

    pic_remap();
    PRINT(0xFF00FF00, 0x000000, "[OK] PIC remapped\n");

    idt_install();
    PRINT(0xFF00FF00, 0x000000, "[OK] IDT installed\n");

    PRINT(0xFFFFFF00, 0x000000, "\n[INIT] Initializing syscall interface...\n");

    syscall_init();
    syscall_register_all();

    PRINT(0xFF00FF00, 0x000000, "[OK] Syscalls ready\n");

    serial_init(COM1);
    PRINT(0xFF00FF00, 0x000000, "[OK] Serial initialized\n");

    // ========================================================================
    // Initialize job system before enabling interrupts
    // ========================================================================

    PRINT(0xFFFFFF00, 0x000000, "\n[INIT] Initializing job system...\n");
    jobs_init();
    jobs_set_active(0);
    PRINT(0xFF00FF00, 0x000000, "[OK] Job system initialized (INACTIVE)\n");

    PRINT(0xFFFFFF00, 0x000000, "\n[INIT] Enabling IRQ system...\n");
    irq_init();
    PRINT(0xFF00FF00, 0x000000, "[OK] IRQ system enabled\n");

    // ========================================================================
    // Test timer for a few seconds
    // ========================================================================

    PRINT(0xFFFFFF00, 0x000000, "\n[TEST] Testing timer for 3 seconds...\n");

    extern volatile uint64_t timer_ticks;
    uint64_t start_ticks = timer_ticks;

    for (int sec = 1; sec <= 3; sec++) {
        uint64_t target = start_ticks + (sec * 1000);
        while (timer_ticks < target) {
            __asm__ volatile("hlt");
        }

        PRINT(0xFF00FF00, 0x000000, "[TEST] Second %d: timer_ticks=%llu\n", 
              sec, timer_ticks);
    }

    PRINT(0xFF00FF00, 0x000000, "[OK] Timer is working correctly!\n");

    // ========================================================================
    // Enable keyboard
    // ========================================================================

    uint8_t mask = inb(0x21);
    mask &= ~0x02;
    outb(0x21, mask);
    PRINT(0xFF00FF00, 0x000000, "[OK] Keyboard enabled\n");

    // ========================================================================
    // Storage & Filesystem
    // ========================================================================

    PRINT(0xFFFFFFFF, 0x000000, "\n[INIT] Initializing storage...\n");
    ata_init();
    PRINT(0xFF00FF00, 0x000000, "[OK] ATA initialized\n");

    PRINT(0xFFFFFFFF, 0x000000, "\n[INIT] Initializing filesystem...\n");
    vfs_init();

    filesystem_t *tinyfs = tinyfs_create();
    if (!tinyfs) {
        PRINT(0xFFFF0000, 0x000000, "[ERROR] Failed to create TinyFS\n");
        goto boot_failed;
    }

    if (vfs_register_filesystem(tinyfs) != 0) {
        PRINT(0xFFFF0000, 0x000000, "[ERROR] Failed to register TinyFS\n");
        goto boot_failed;
    }

    PRINT(0xFFFFFF00, 0x000000, "[INIT] Formatting disk...\n");
    
    char device_name[] = "ata0";
    if (tinyfs_format(device_name) != 0) {
        PRINT(0xFFFF0000, 0x000000, "[ERROR] Format failed\n");
        goto boot_failed;
    }

    PRINT(0xFF00FF00, 0x000000, "[OK] Disk formatted\n");

    for (volatile int i = 0; i < 10000000; i++);

    PRINT(0xFFFFFF00, 0x000000, "[INIT] Mounting filesystem...\n");

    char fs_type[] = "tinyfs";
    char device[] = "ata0";
    char mountpoint[] = "/";

    if (vfs_mount(fs_type, device, mountpoint) != 0) {
        PRINT(0xFFFF0000, 0x000000, "[ERROR] Mount failed\n");
        goto boot_failed;
    }

    PRINT(0xFF00FF00, 0x000000, "[OK] Filesystem mounted\n");

    // ========================================================================
    // Process/Thread System
    // ========================================================================

    PRINT(0xFFFFFFFF, 0x000000, "\n[INIT] Initializing processes...\n");
    process_init();
    scheduler_init();

    // Initialize background thread data
    extern cmd_thread_data_t bg_thread_data[MAX_JOBS];
    for (int i = 0; i < MAX_JOBS; i++) {
        bg_thread_data[i].job_id = 0;
        bg_thread_data[i].command[0] = '\0';
    }

    init_kernel_threads();
    PRINT(0xFF00FF00, 0x000000, "[OK] Threads initialized (scheduler DISABLED)\n");

    // ========================================================================
    // Enable job tracking and start shell
    // ========================================================================

    PRINT(0xFF00FFFF, 0x000000, "\n=== Boot Complete ===\n");

    jobs_set_active(1);
    PRINT(0xFF00FF00, 0x000000, "[OK] Job tracking ENABLED\n");
    for(;;) {
        ClearScreen(0x000000);
        while (1) {
        mouse();
    }}
    PRINT(0xFF00FFFF, 0x000000, "\nStarting shell...\n\n");
    
    for (volatile int i = 0; i < 5000000; i++);

    init_shell();

    while(1) __asm__ volatile("hlt");

boot_failed:
    PRINT(0xFFFF0000, 0x000000, "\n=== BOOT FAILED ===\n");
    while(1) __asm__ volatile("hlt");

    return EFI_SUCCESS;
}