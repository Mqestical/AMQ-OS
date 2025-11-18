#include <efi.h>
#include <efilib.h>

extern void kernel_main(void);
extern void pmm_init(EFI_MEMORY_DESCRIPTOR* map, UINTN desc_count, UINTN desc_size);
extern void init_kernel_heap(void);
extern void init_kernel_stack(void);

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);
    
    // Clear screen and reset cursor
    uefi_call_wrapper(ST->ConOut->ClearScreen, 1, ST->ConOut);
    uefi_call_wrapper(ST->ConOut->SetCursorPosition, 3, ST->ConOut, 0, 0);
    
    CHAR16 msg1[] = L"KERNEL STARTING\r\n\r\n";
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
    
    CHAR16 msg2[] = L"PMM init\r\n";
    uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, msg2);
    pmm_init(memory_map, desc_count, descriptor_size);
    
    CHAR16 msg3[] = L"Heap init\r\n";
    uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, msg3);
    init_kernel_heap();
    
    CHAR16 msg4[] = L"Stack init\r\n\r\n";
    uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, msg4);
    init_kernel_stack();

    CHAR16 msg5[] = L"Running kernel_main\r\n\r\n";
    uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, msg5);
    
    kernel_main();

    CHAR16 msg6[] = L"\r\n\r\nKERNEL DONE!\r\n";
    uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, msg6);
    
    return EFI_SUCCESS;
}