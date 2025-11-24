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
    vfs_init();
    
    // Create and register TinyFS
    filesystem_t *tinyfs = tinyfs_create();
    if (tinyfs) {
        vfs_register_filesystem(tinyfs);
        
        char device[] = "ata0";
        char fs_type[] = "tinyfs";
        char mount_point[] = "/";
        
        // ALWAYS format on boot (for testing)
        char format_msg[] = "[INFO] Formatting disk...\n";
        printk(0xFFFFFF00, 0x000000, format_msg);
        
        if (tinyfs_format(device) == 0) {
            char ok[] = "[OK] Disk formatted\n";
            printk(0xFF00FF00, 0x000000, ok);
            
            // Wait for disk
            for (volatile int i = 0; i < 10000000; i++);
            
            // Mount
            char mount_msg[] = "[INFO] Mounting filesystem...\n";
            printk(0xFFFFFF00, 0x000000, mount_msg);
            
            if (vfs_mount(fs_type, device, mount_point) == 0) {
                char ok2[] = "[OK] Filesystem mounted at /\n";
                printk(0xFF00FF00, 0x000000, ok2);
                
                // Verify root node
                vfs_node_t *check_root = vfs_get_root();
                if (check_root) {
                    char root_ok[] = "[OK] Root node is valid (addr=%p)\n";
                    printk(0xFF00FF00, 0x000000, root_ok, check_root);
                    
                    // Create initial structure
                    char init_msg[] = "[INFO] Creating initial filesystem structure...\n";
                    printk(0xFFFFFF00, 0x000000, init_msg);
                    
                    int result1 = vfs_mkdir("/home", FILE_READ | FILE_WRITE);
                    char r1[] = "[INFO] mkdir /home returned: %d\n";
                    printk(0xFF00FFFF, 0x000000, r1, result1);
                    
                    int result2 = vfs_mkdir("/bin", FILE_READ | FILE_WRITE);
                    char r2[] = "[INFO] mkdir /bin returned: %d\n";
                    printk(0xFF00FFFF, 0x000000, r2, result2);
                    
                    int result3 = vfs_mkdir("/etc", FILE_READ | FILE_WRITE);
                    char r3[] = "[INFO] mkdir /etc returned: %d\n";
                    printk(0xFF00FFFF, 0x000000, r3, result3);
                    
                    int result4 = vfs_create("/readme.txt", FILE_READ | FILE_WRITE);
                    char r4[] = "[INFO] create /readme.txt returned: %d\n";
                    printk(0xFF00FFFF, 0x000000, r4, result4);
                    
                    if (result4 == 0) {
                        int fd = vfs_open("/readme.txt", FILE_WRITE);
                        if (fd >= 0) {
                            char welcome[] = "Welcome to AMQ OS!\n";
                            vfs_write(fd, (uint8_t*)welcome, 19);
                            vfs_close(fd);
                            char ok3[] = "[OK] Wrote to readme.txt\n";
                            printk(0xFF00FF00, 0x000000, ok3);
                        }
                    }
                    
                    char init_ok[] = "[OK] Initial filesystem created\n";
                    printk(0xFF00FF00, 0x000000, init_ok);
                } else {
                    char root_err[] = "[ERROR] Root node is NULL!\n";
                    printk(0xFFFF0000, 0x000000, root_err);
                }
            } else {
                char err[] = "[ERROR] Mount failed\n";
                printk(0xFFFF0000, 0x000000, err);
            }
        } else {
            char err[] = "[ERROR] Format failed\n";
            printk(0xFFFF0000, 0x000000, err);
        }
    } else {
        char err[] = "[ERROR] Could not create TinyFS\n";
        printk(0xFFFF0000, 0x000000, err);
    }
    
    if (serial_initialized) {
        char serial1[] = "\r\n=== Boot Complete ===\r\n";
        serial_write_string(COM1, serial1);
    }
    
    char boot_complete[] = "\n=== Boot Complete ===\n";
    printk(0xFF00FFFF, 0x000000, boot_complete);
    char shell_msg[] = "Starting shell...\n\n";
    printk(0xFF00FFFF, 0x000000, shell_msg);
    
    for (volatile int i = 0; i < 5000000; i++);
    
    init_shell();
    
    while(1) __asm__ volatile("hlt");

    return EFI_SUCCESS;
}