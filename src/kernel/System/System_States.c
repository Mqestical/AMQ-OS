#include <efi.h>
#include <efilib.h>
#include "print.h"
#include "string_helpers.h"
#include "System_States.h"



unsigned int system_reboot(void) {
    ClearScreen(BLACK);
    SetCursorPos(0, 0);
    PRINT(BROWN, BLACK, "[SYSTEM] Initiating reboot sequence...\n");
    PRINT(GREEN, BLACK, "[ADMINISTRATOR] SYSTEM IS REBOOTING NOW!\n");
    EFI_STATUS status = RT->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
    if (EFI_ERROR(status)) {
        PRINT(RED, BLACK, "[SYSTEM] Reboot failed with status: 0x");
        print_unsigned(status, 16);
        PRINT(RED, BLACK, "\n");
        return (unsigned int)status;
    }
    return status;
}

unsigned int system_shutdown(void) {
    PRINT(WHITE, BLACK, "[SYSTEM] Initiating shutdown sequence...\n");
    PRINT(GREEN, BLACK, "[ADMINISTRATOR] SYSTEM IS SHUTTING DOWN NOW!\n");
    
    EFI_STATUS status = RT->ResetSystem(EfiResetShutdown, EFI_SUCCESS, 0, NULL);
    if (EFI_ERROR(status)) {
        PRINT(RED, BLACK, "[SYSTEM] Shutdown failed with status: 0x");
        print_unsigned(status, 16);
        PRINT(RED, BLACK, "\n");
        return (unsigned int)status;
    }
    return status;
}
