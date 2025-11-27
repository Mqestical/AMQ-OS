// start.c - SAFE INITIALIZATION ORDER

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

    CHAR16 msg1[] = L"AMQ OS - Booting...\r\n";
    uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, msg1);

    // Get memory map
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
        CHAR16 err[] = L"Failed to allocate kernel stack!\r\n";
        uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, err);
        goto boot_failed;
    }

    init_graphics(ST);

    // Exit boot services
    EFI_STATUS status = uefi_call_wrapper(BS->ExitBootServices, 2, ImageHandle, map_key);
    if (EFI_ERROR(status)) {
        uefi_call_wrapper(BS->GetMemoryMap, 5, &memory_map_size, memory_map, &map_key, &descriptor_size, &descriptor_version);
        status = uefi_call_wrapper(BS->ExitBootServices, 2, ImageHandle, map_key);
        if (EFI_ERROR(status)) {
            while(1) __asm__ volatile("hlt");
        }
    }

    // ========================================================================
    // PHASE 1: CRITICAL SYSTEM SETUP (NO INTERRUPTS)
    // ========================================================================
    
     ClearScreen(0x000000);
    SetCursorPos(0, 0);

    char boot1[] = "AMQ OS Kernel v0.2\n";
    char boot2[] = "==================\n\n";
    printk(0xFFFFFFFF, 0x000000, boot1);
    printk(0xFFFFFFFF, 0x000000, boot2);
    
    enable_io_privilege();
    printk(0xFF00FF00, 0x000000, "[OK] I/O privileges enabled\n");
    
    printk(0xFF00FF00, 0x000000, "[OK] Stack: base=0x%llx, top=0x%llx\n", 
           kernel_stack_base, kernel_stack_top);
    
    tss_init();
    printk(0xFF00FF00, 0x000000, "[OK] TSS initialized\n");
    
    gdt_install();
    printk(0xFF00FF00, 0x000000, "[OK] GDT installed\n");
    
    pic_remap();
    printk(0xFF00FF00, 0x000000, "[OK] PIC remapped\n");
    
    idt_install();
    printk(0xFF00FF00, 0x000000, "[OK] IDT installed\n");
    
    printk(0xFFFFFF00, 0x000000, "\n[INIT] Initializing syscall interface...\n");
    
    // 1. Initialize MSRs and syscall entry point
    syscall_init();
    
    // 2. Register all syscall handlers
    syscall_register_all();
    
    printk(0xFF00FF00, 0x000000, "[OK] Syscalls ready\n");

    serial_init(COM1);
    printk(0xFF00FF00, 0x000000, "[OK] Serial initialized\n");
    
    // ========================================================================
    // CRITICAL: Initialize job system BEFORE enabling interrupts
    // ========================================================================
    
    printk(0xFFFFFF00, 0x000000, "\n[INIT] Initializing job system...\n");
    jobs_init();
    jobs_set_active(0);  // Keep it DISABLED
    printk(0xFF00FF00, 0x000000, "[OK] Job system initialized (INACTIVE)\n");
    
    // ========================================================================
    // NOW enable timer
    // ========================================================================
    
    printk(0xFFFFFF00, 0x000000, "\n[INIT] Enabling IRQ system...\n");
    irq_init();
    printk(0xFF00FF00, 0x000000, "[OK] IRQ system enabled\n");
    
    // ========================================================================
    // CRITICAL: Test timer for a few seconds
    // ========================================================================
    
    printk(0xFFFFFF00, 0x000000, "\n[TEST] Testing timer for 3 seconds...\n");
    
    extern volatile uint64_t timer_ticks;
    uint64_t start_ticks = timer_ticks;
    
    for (int sec = 1; sec <= 3; sec++) {
        // Wait 1 second
        uint64_t target = start_ticks + (sec * 1000);
        while (timer_ticks < target) {
            __asm__ volatile("hlt");  // Wait for interrupt
        }
        
        printk(0xFF00FF00, 0x000000, "[TEST] Second %d: timer_ticks=%llu\n", 
               sec, timer_ticks);
    }
    
    printk(0xFF00FF00, 0x000000, "[OK] Timer is working correctly!\n");
    
    // ========================================================================
    // Enable keyboard
    // ========================================================================
    
    uint8_t mask = inb(0x21);
    mask &= ~0x02;
    outb(0x21, mask);
    printk(0xFF00FF00, 0x000000, "[OK] Keyboard enabled\n");

    // ========================================================================
    // Storage & Filesystem
    // ========================================================================

    printk(0xFFFFFFFF, 0x000000, "\n[INIT] Initializing storage...\n");
    ata_init();
    printk(0xFF00FF00, 0x000000, "[OK] ATA initialized\n");

    printk(0xFFFFFFFF, 0x000000, "\n[INIT] Initializing filesystem...\n");
    vfs_init();

    filesystem_t *tinyfs = tinyfs_create();
    if (!tinyfs) {
        printk(0xFFFF0000, 0x000000, "[ERROR] Failed to create TinyFS\n");
        goto boot_failed;
    }

    if (vfs_register_filesystem(tinyfs) != 0) {
        printk(0xFFFF0000, 0x000000, "[ERROR] Failed to register TinyFS\n");
        goto boot_failed;
    }

    char device[] = "ata0";
    printk(0xFFFFFF00, 0x000000, "[INIT] Formatting disk...\n");
    
    if (tinyfs_format(device) != 0) {
        printk(0xFFFF0000, 0x000000, "[ERROR] Format failed\n");
        goto boot_failed;
    }

    printk(0xFF00FF00, 0x000000, "[OK] Disk formatted\n");
    
    for (volatile int i = 0; i < 10000000; i++);

    printk(0xFFFFFF00, 0x000000, "[INIT] Mounting filesystem...\n");

    char fs_type[] = "tinyfs";
    char mount_point[] = "/";

    if (vfs_mount(fs_type, device, mount_point) != 0) {
        printk(0xFFFF0000, 0x000000, "[ERROR] Mount failed\n");
        goto boot_failed;
    }

    printk(0xFF00FF00, 0x000000, "[OK] Filesystem mounted\n");

    // ========================================================================
    // Process/Thread System
    // ========================================================================

    printk(0xFFFFFFFF, 0x000000, "\n[INIT] Initializing processes...\n");
    process_init();
    scheduler_init();

    // Initialize background thread data
    extern cmd_thread_data_t bg_thread_data[MAX_JOBS];
    for (int i = 0; i < MAX_JOBS; i++) {
        bg_thread_data[i].job_id = 0;
        bg_thread_data[i].command[0] = '\0';
    }

    init_kernel_threads();
    printk(0xFF00FF00, 0x000000, "[OK] Threads initialized (scheduler DISABLED)\n");

    // ========================================================================
    // Enable job tracking and start shell
    // ========================================================================

    printk(0xFF00FFFF, 0x000000, "\n=== Boot Complete ===\n");
    
    jobs_set_active(1);
    printk(0xFF00FF00, 0x000000, "[OK] Job tracking ENABLED\n");

    printk(0xFF00FFFF, 0x000000, "\nStarting shell...\n\n");
    
    for (volatile int i = 0; i < 5000000; i++);

    init_shell();

    while(1) __asm__ volatile("hlt");

boot_failed:
    printk(0xFFFF0000, 0x000000, "\n=== BOOT FAILED ===\n");
    while(1) __asm__ volatile("hlt");

    return EFI_SUCCESS;
}