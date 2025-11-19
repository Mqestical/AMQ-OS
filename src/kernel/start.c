#include <efi.h>
#include <efilib.h>
#include "print.h"
#include "memory.h"
#include "idt.h"
#include "PICR.h"
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
    //uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, msg1);

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



    pmm_init(memory_map, desc_count, descriptor_size); // PMM Initialization

    init_kernel_heap(); // Kernel heap initialization

    init_graphics(ST); // Graphics initialization

    idt_install();     // Set up IDT
    pic_remap();       // Remap PIC
    
    __asm__ volatile("sti");  // Enable interrupts NOW

    EFI_STATUS status;
    status = uefi_call_wrapper(BS->ExitBootServices, 2, ImageHandle, map_key);
    for (;;) {
    ClearScreen(0x0000FF);
    SetCursorPos(10, 10);
    SetColors(0xFFFFFFFF, 0x000000); // Red on white

    for (int i = 0; i < 19; i++) { // W
        char krnlmsg[] = "\t\t\t\tWelcome to AMQ OS!\r\n\n\n\n\n\n\n\n\n";
        printc(krnlmsg[i]);
    }

    for(int j = 0; j < 71; j++) {
        char krnlmsg2[] = "\n\n\n\n\n\n\n\n\n\n\n\n\n\n\nThis is a very early stage of AMQ-OSfaults and crashes may occur\n\n\n\n\n";
        printc(krnlmsg2[j]);
    }

    for (int o = 0; o < 36; o++) {
         char krnlmsg3[] = "\n\nType ""info"" for more information.";
        printc(krnlmsg3[o]);
        }
    }
    return EFI_SUCCESS;
}
