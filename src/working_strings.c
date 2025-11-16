#include <efi.h>
#include <efilib.h>

EFI_STATUS
EFIAPI
efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    InitializeLib(ImageHandle, SystemTable);
    
    uefi_call_wrapper(ST->ConOut->ClearScreen, 1, ST->ConOut);
    
    // Define strings on the stack (not static)
    CHAR16 test[] = {
        0x0054, 0x0045, 0x0053, 0x0054,  // "TEST"
        0x000D, 0x000A,                   // "\r\n"
        0x0000
    };
    
    CHAR16 hello[] = {
        0x0048, 0x0065, 0x006C, 0x006C, 0x006F,  // "Hello"
        0x0020,                                   // " "
        0x0057, 0x006F, 0x0072, 0x006C, 0x0064,  // "World"
        0x0021,                                   // "!"
        0x000D, 0x000A,                          // "\r\n"
        0x0000
    };
    
    CHAR16 working[] = {
        0x0049, 0x0074, 0x0020, 0x0077, 0x006F, 0x0072, 0x006B, 0x0073, 0x0021,  // "It works!"
        0x000D, 0x000A,
        0x0000
    };
    
    // Output strings
    uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, test);
    uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, hello);
    uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, working);
    
    while(1);
    
    return EFI_SUCCESS;
}
