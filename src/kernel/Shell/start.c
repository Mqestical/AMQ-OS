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

extern void pmm_init(EFI_MEMORY_DESCRIPTOR* map, UINTN desc_count, UINTN desc_size);
extern void init_kernel_heap(void);
extern char font8x8_basic[128][8];

// Enable I/O privilege level
static inline void enable_io_privilege(void) {
    __asm__ volatile(
        "pushfq\n"              // Push RFLAGS
        "pop %%rax\n"           // Pop into RAX
        "or $0x3000, %%rax\n"   // Set IOPL bits (bits 12-13)
        "push %%rax\n"          // Push modified flags
        "popfq\n"               // Pop into RFLAGS
        ::: "rax", "memory"
    );
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);

    // Clear screen and reset cursor
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

    // Initialize memory
    pmm_init(memory_map, desc_count, descriptor_size);
    init_kernel_heap();

    // Initialize graphics
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

    // NOW we're in kernel mode - critical setup order:
    
    // 1. Enable I/O privilege IMMEDIATELY
    enable_io_privilege();
    
    // 2. Install TSS (needed for I/O permissions)
    tss_init();
    
    // 3. Install GDT (done in idt.c)
    gdt_install();
    
    // 4. Now install IDT
    idt_install();
    
    // 5. Initialize serial port (NOW I/O should work)
    serial_init(COM1);
    
    // 6. Clear screen to show we're in kernel mode
    ClearScreen(0x000000);
    SetCursorPos(0, 0);
    
    printk(0xFFFFFFFF, 0x000000, "Kernel mode active\n");
    
    // Test if serial initialized
    extern volatile int serial_initialized;
    if (serial_initialized) {
        printk(0xFF00FF00, 0x000000, "Serial port initialized successfully\n");
        serial_write_string(COM1, "Serial port OK!\r\n");
    } else {
        printk(0xFFFF0000, 0x000000, "WARNING: Serial port init failed\n");
    }
    
    // 7. Remap PIC
    pic_remap();
    
    // 8. Enable interrupts
    __asm__ volatile("sti");
    
    // Wait for timer interrupt
    for (volatile int i = 0; i < 10000000; i++);
    
    // Check if timer interrupt fired
    extern volatile uint32_t interrupt_vector;
    printk(0xFFFFFF00, 0x000000, "Timer interrupts: %u\n", interrupt_vector);
    
    if (interrupt_vector == 0) {
        printk(0xFFFF0000, 0x000000, "ERROR: No interrupts!\n");
    } else {
        printk(0xFF00FF00, 0x000000, "Interrupts working!\n");
    }
    
    // 9. Unmask keyboard interrupt (IRQ1)
    uint8_t mask = inb(0x21);
    mask &= ~0x02;  // Unmask IRQ1
    outb(0x21, mask);
    
    printk(0xFFFFFFFF, 0x000000, "Keyboard enabled\n");
    
    if (serial_initialized) {
        serial_write_string(COM1, "\r\n=== AMQ OS Serial Console ===\r\n");
        serial_write_string(COM1, "Boot complete. Ready for input!\r\n\r\n");
    }
    
    printk(0xFFFFFFFF, 0x000000, "Starting shell...\n\n");
    
    // Initialize and run shell
    init_shell();

    // Should never reach here
    return EFI_SUCCESS;
}