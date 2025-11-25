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

    // Kernel initialization
    enable_io_privilege();
    tss_init();
    gdt_install();
    idt_install();
    serial_init(COM1);

    ClearScreen(0x000000);
    SetCursorPos(0, 0);

    char boot1[] = "AMQ OS Kernel v0.2\n";
    char boot2[] = "==================\n\n";
    printk(0xFFFFFFFF, 0x000000, boot1);
    printk(0xFFFFFFFF, 0x000000, boot2);
char stack_msg[] = "[OK] Stack allocated: base=0x%llx, top=0x%llx, size=%llu KB\n";
    printk(0xFF00FF00, 0x000000, stack_msg, kernel_stack_base, kernel_stack_top, 
       (kernel_stack_top - kernel_stack_base) / 1024);
    extern volatile int serial_initialized;
    if (serial_initialized) {
        char msg[] = "[OK] Serial initialized\n";
        printk(0xFF00FF00, 0x000000, msg);
    }

    pic_remap();
    __asm__ volatile("sti");

    for (volatile int i = 0; i < 5000000; i++);

    extern volatile uint32_t interrupt_vector;
    if (interrupt_vector > 0) {
        char msg[] = "[OK] Interrupts working\n";
        printk(0xFF00FF00, 0x000000, msg);
    }

    
    uint8_t mask = inb(0x21);
    mask &= ~0x02;
    outb(0x21, mask);

    char kbd_msg[] = "[OK] Keyboard enabled\n";
    printk(0xFF00FF00, 0x000000, kbd_msg);

    // Initialize storage
    char storage_header[] = "\nInitializing storage...\n";
    printk(0xFFFFFFFF, 0x000000, storage_header);
    ata_init();

    // Initialize VFS
    char fs_header[] = "\nInitializing filesystem...\n";
    printk(0xFFFFFFFF, 0x000000, fs_header);

    char vfs1[] = "[KERNEL] Initializing VFS...\n";
    printk(0xFFFFFF00, 0x000000, vfs1);
    vfs_init();

    char vfs2[] = "[KERNEL] Creating TinyFS instance...\n";
    printk(0xFFFFFF00, 0x000000, vfs2);

    filesystem_t *tinyfs = tinyfs_create();
    if (!tinyfs) {
        char err[] = "[ERROR] Failed to create TinyFS instance\n";
        printk(0xFFFF0000, 0x000000, err);
        goto boot_failed;
    }

    char fmt1[] = "[OK] TinyFS instance created at %p\n";
    printk(0xFF00FF00, 0x000000, fmt1, tinyfs);

    char fmt2[] = "[DEBUG] tinyfs->name = '%s'\n";
    printk(0xFFFFFF00, 0x000000, fmt2, tinyfs->name);

    char fmt3[] = "[DEBUG] tinyfs->ops = %p\n";
    printk(0xFFFFFF00, 0x000000, fmt3, tinyfs->ops);

    char reg1[] = "[KERNEL] Registering TinyFS with VFS...\n";
    printk(0xFFFFFF00, 0x000000, reg1);
    int reg_result = vfs_register_filesystem(tinyfs);

    char check_after[] = "[DEBUG AFTER REG] tinyfs->name = '%s' at %p\n";
printk(0xFFFFFF00, 0x000000, check_after, tinyfs->name, tinyfs->name);

   // char dbg_reg[] = "[DEBUG] vfs_register_filesystem returned %d\n";
    //printk(0xFFFFFF00, 0x000000, dbg_reg, reg_result);

    if (reg_result != 0) {
        char err[] = "[ERROR] Failed to register TinyFS\n";
        printk(0xFFFF0000, 0x000000, err);
        goto boot_failed;
    }

    char ok_reg[] = "[OK] TinyFS registered\n";
    printk(0xFF00FF00, 0x000000, ok_reg);

    // Format disk
    char format_msg[] = "[INFO] Formatting disk...\n";
    printk(0xFFFFFF00, 0x000000, format_msg);

    char device[] = "ata0";
    if (tinyfs_format(device) != 0) {
        char err[] = "[ERROR] Format failed\n";
        printk(0xFFFF0000, 0x000000, err);
        goto boot_failed;
    }

    char ok_fmt[] = "[OK] Disk formatted\n";
    printk(0xFF00FF00, 0x000000, ok_fmt);

    for (volatile int i = 0; i < 10000000; i++);

    // Mount filesystem
    char mount_msg[] = "[INFO] Mounting filesystem...\n";
    printk(0xFFFFFF00, 0x000000, mount_msg);

    char fs_type[] = "tinyfs";
    char mount_point[] = "/";

    if (vfs_mount(fs_type, device, mount_point) != 0) {
        char err[] = "[ERROR] Mount failed\n";
        printk(0xFFFF0000, 0x000000, err);
        goto boot_failed;
    }

    char ok_mount[] = "[OK] Filesystem mounted at /\n";
    printk(0xFF00FF00, 0x000000, ok_mount);

    vfs_debug_root();

    char test_msg[] = "\n[TEST] Testing filesystem operations...\n";
    printk(0xFFFFFF00, 0x000000, test_msg);
    char home[] = "/home";
    if (vfs_mkdir(home, FILE_READ | FILE_WRITE) == 0) {
        char msg[] = "[OK] Created /home directory\n";
        printk(0xFF00FF00, 0x000000, msg);
    } else {
        char msg[] = "[FAIL] Could not create /home\n";
        printk(0xFFFF0000, 0x000000, msg);
    }
    char bin[] = "/bin";
    if (vfs_mkdir(bin, FILE_READ | FILE_WRITE) == 0) {
        char msg[] = "[OK] Created /bin directory\n";
        printk(0xFF00FF00, 0x000000, msg);
    } else {
        char msg[] = "[FAIL] Could not create /bin\n";
        printk(0xFFFF0000, 0x000000, msg);
    }
    char readme[] = "/readme.txt";
    if (vfs_create(readme, FILE_READ | FILE_WRITE) == 0) {
        char msg[] = "[OK] Created /readme.txt\n";
        printk(0xFF00FF00, 0x000000, msg);

    char lst_msg[] = "\nFinal root directory contents:\n";
    printk(0xFFFFFFFF, 0x000000, lst_msg);
    vfs_list_directory("/");

    char boot_complete[] = "\n=== Boot Complete ===\n";
    printk(0xFF00FFFF, 0x000000, boot_complete);

    char shell_msg[] = "Starting shell...\n\n";
    printk(0xFF00FFFF, 0x000000, shell_msg);

    for (volatile int i = 0; i < 5000000; i++);

    init_shell();

    while(1) __asm__ volatile("hlt");

boot_failed:
    char fail1[] = "\n=== BOOT FAILED ===\n";
    printk(0xFFFF0000, 0x000000, fail1);

    char fail2[] = "System halted.\n";
    printk(0xFFFF0000, 0x000000, fail2);

    while(1) __asm__ volatile("hlt");

    return EFI_SUCCESS;
    }   
}