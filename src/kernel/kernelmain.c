
#include "memory.h"
#include "print.h"
#include <efi.h>
#include <efilib.h>

void kernel_main() {
    CHAR16 msg[] = L"kernel_main running\r\n";
    //uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, msg);
    
    CHAR16 letters[] = L"Welcome to memory, Superuser.\r\n";
}