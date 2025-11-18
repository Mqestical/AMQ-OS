#include <efi.h>
#include <efilib.h>
#include "print.h"
#include "memory.h"

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
    
    CHAR16 msg2[] = L"PMM init\r\n";
    //uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, msg2);
    pmm_init(memory_map, desc_count, descriptor_size);
    
    CHAR16 msg3[] = L"Heap init\r\n";
    //uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, msg3);
    init_kernel_heap();

    // *** INITIALIZE GRAPHICS ***
    CHAR16 msg4[] = L"Graphics init\r\n";
    //uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, msg4);
    init_graphics(ST);
    
    // Debug: Show framebuffer info
    CHAR16 debug[200];
    SPrint(debug, sizeof(debug), L"FB Base: %lx, Width: %d, Height: %d, Pitch: %d\r\n", 
           (uint64_t)fb.base, fb.width, fb.height, fb.pitch);
    //uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, debug);
    
    // Show pixel format
    SPrint(debug, sizeof(debug), L"PixelFormat: %d\r\n", gop->Mode->Info->PixelFormat);
    //uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, debug);
    
    // *** DIAGNOSTIC TESTS ***
    CHAR16 msg_test[] = L"Running graphics tests...\r\n";
    //uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, msg_test);
    
    // Test 1: Draw a single white pixel at (100, 100)
    put_pixel(100, 100, 0xFFFFFFFF);
    
    // Test 2: Draw a red square
    for(int y = 110; y < 120; y++) {
        for(int x = 100; x < 110; x++) {
            put_pixel(x, y, 0xFFFF0000); // Red square
        }
    }
    
    // Test 3: Draw a blue square (alternative color format)
    for(int y = 110; y < 120; y++) {
        for(int x = 130; x < 140; x++) {
            put_pixel(x, y, 0xFF0000FF); // Blue square
        }
    }
    
    // Test 4: Try drawing a character directly
    draw_char(200, 100, 'A', 0xFFFF0000, 0xFF000000); // Red 'A' on black
    draw_char(210, 100, 'B', 0xFF00FF00, 0xFF000000); // Green 'B' on black
    draw_char(220, 100, 'C', 0xFF0000FF, 0xFF000000); // Blue 'C' on black
    
    // Test 5: Test printc with default colors (white on black)
    SetCursorPos(200, 100);
    printc('T');
    printc('E');
    printc('S');
    printc('T');
    
    // Test 6: Test with custom colors
    ClearScreen(0x0000FF);
    SetCursorPos(10, 10);
    SetColors(0xFFFF0000, 0xFFFFFFFF); // Red on white
     for (int i = 0; i < 31; i++) { // W
    char krnlmsg[] = "Welcome to memory, Superuser.";
        printc(krnlmsg[i]);
    }


    CHAR16 msg5[] = L"Graphics tests done. Running kernel_main\r\n\r\n";
    //uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, msg5);
    
    kernel_main();

    CHAR16 msg6[] = L"\r\nPrinting to framebuffer...\r\n";
    //uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, msg6);
    
    // Set colors: Black text on white background (more readable)
    SetColors(0xFF000000, 0xFFFFFFFF);
    
    // Print everything with cursor system
    CHAR16 msg7[] = L"\r\nKERNEL DONE!\r\n";
    //uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, msg7);
    
    return EFI_SUCCESS;
}