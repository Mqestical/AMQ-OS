#include <efi.h>
#include <efilib.h>
#include "print.h"
#include "memory.h"
#include "idt.h"
#include "PICR.h"
#include "in_out_b.h"
extern void kernel_main(void);
extern void pmm_init(EFI_MEMORY_DESCRIPTOR* map, UINTN desc_count, UINTN desc_size);
extern void init_kernel_heap(void);
extern char font8x8_basic[128][8];

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);

    // Clear screen and reset cursor
    uefi_call_wrapper(ST->ConOut->ClearScreen, 1, ST->ConOut);
    uefi_call_wrapper(ST->ConOut->SetCursorPosition, 3, ST->ConOut, 0, 0);

    CHAR16 msg1[] = L"KERNEL STARTING\r\n\r\n";
    uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, msg1);

    // Get memory map (first call to get size)
    EFI_MEMORY_DESCRIPTOR *memory_map = NULL;
    UINTN memory_map_size = 0;
    UINTN map_key = 0;
    UINTN descriptor_size = 0;
    UINT32 descriptor_version = 0;

    // First call - get required size
    uefi_call_wrapper(BS->GetMemoryMap, 5, &memory_map_size, memory_map, &map_key, &descriptor_size, &descriptor_version);
    
    // Add extra space for potential changes
    memory_map_size += 2 * descriptor_size;
    
    // Allocate memory for map
    uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, memory_map_size, (void**)&memory_map);
    
    // Second call - actually get the map
    uefi_call_wrapper(BS->GetMemoryMap, 5, &memory_map_size, memory_map, &map_key, &descriptor_size, &descriptor_version);

    UINTN desc_count = memory_map_size / descriptor_size;

    // Initialize PMM with the memory map
    pmm_init(memory_map, desc_count, descriptor_size);

    // Initialize kernel heap
    init_kernel_heap();

    // Initialize graphics BEFORE exiting boot services
    init_graphics(ST);

    CHAR16 msg2[] = L"Installing GDT and IDT...\r\n";
    uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, msg2);

    // Install GDT and IDT while still in boot services
    gdt_install();
    idt_install();

    CHAR16 msg3[] = L"Exiting boot services...\r\n";
    uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, msg3);

    // Exit boot services (CRITICAL: use the map_key we just got)
    EFI_STATUS status = uefi_call_wrapper(BS->ExitBootServices, 2, ImageHandle, map_key);
    
    if (EFI_ERROR(status)) {
        // If it fails, get the memory map again and retry
        uefi_call_wrapper(BS->GetMemoryMap, 5, &memory_map_size, memory_map, &map_key, &descriptor_size, &descriptor_version);
        status = uefi_call_wrapper(BS->ExitBootServices, 2, ImageHandle, map_key);
        
        if (EFI_ERROR(status)) {
            // Failed to exit boot services - halt
            while(1) __asm__ volatile("hlt");
        }
    }

    // NOW we're out of UEFI - can set up interrupts
    pic_remap();  // Remap PIC (only call ONCE)
    
    // Unmask only keyboard interrupt (IRQ1)
    outb(0x21, 0xFD);  // 0xFD = 11111101 - unmask IRQ1 only
    outb(0xA1, 0xFF);  // Mask all slave PIC interrupts
    
    // Enable interrupts
    __asm__ volatile("sti");

    // Main kernel loop
    for (;;) {
        ClearScreen(0x0000FF);
        SetCursorPos(10, 10);
        SetColors(0xFFFFFFFF, 0x000000);

        char krnlmsg2[] = "\n\n\n\n\n\n\n\n\n\n\n\n\n\n\nThis is a very early stage of AMQ-OS\nfaults and crashes may occur\n\n\n\n\n";
        for (int j = 0; krnlmsg2[j] != '\0'; j++) {
            printc(krnlmsg2[j]);
        }

        char krnlmsg3[] = "\n\nType \"info\" for more information.";
        for (int o = 0; krnlmsg3[o] != '\0'; o++) {
            printc(krnlmsg3[o]);
        }
        
        // Don't return - stay in loop
        while(1) {
            __asm__ volatile("hlt");
        }
    }

    return EFI_SUCCESS;
}