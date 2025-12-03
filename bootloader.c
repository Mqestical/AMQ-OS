#include <efi.h>
#include <efilib.h>

EFI_STATUS
EFIAPI
efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    InitializeLib(ImageHandle, SystemTable);
    
    uefi_call_wrapper(ST->ConOut->ClearScreen, 1, ST->ConOut);

    CHAR16 msg[] = L"Welcome to AMQ Operating System, Superuser. Loading kernel, standby...\n";
    uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, msg);

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Volume;
    EFI_FILE_PROTOCOL *Root, *KernelFile;
    EFI_STATUS Status;
    
    CHAR16 debug1[] = L"[1] Searching for filesystems...\n";
    uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, debug1);
    
    EFI_HANDLE *Handles = NULL;
    UINTN HandleCount = 0;
    
    Status = uefi_call_wrapper(BS->LocateHandleBuffer, 5,
        ByProtocol,
        &gEfiSimpleFileSystemProtocolGuid,
        NULL,
        &HandleCount,
        &Handles);
    
    if (EFI_ERROR(Status) || HandleCount == 0) {
        CHAR16 err1[] = L"ERROR: No filesystems found\n";
        uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, err1);
        while(1);
    }
    
    CHAR16 debug1b[] = L"[1b] Found filesystem, getting protocol...\n";
    uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, debug1b);
    
    Status = uefi_call_wrapper(BS->HandleProtocol, 3,
        Handles[0],
        &gEfiSimpleFileSystemProtocolGuid,
        (VOID**)&Volume);
    
    if (EFI_ERROR(Status)) {
        CHAR16 err1b[] = L"ERROR: HandleProtocol failed\n";
        uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, err1b);
        while(1);
    }
    
    CHAR16 debug2[] = L"[2] Opening volume...\n";
    uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, debug2);
    
    Status = uefi_call_wrapper(Volume->OpenVolume, 2, Volume, &Root);
    if (EFI_ERROR(Status)) {
        CHAR16 err2[] = L"ERROR: OpenVolume failed\n";
        uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, err2);
        while(1);
    }
    
    CHAR16 debug3[] = L"[3] Listing root directory...\n";
    uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, debug3);
    
    EFI_FILE_INFO *ListFileInfo;
    UINTN ListFileInfoSize = sizeof(EFI_FILE_INFO) + 200;
    Status = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, ListFileInfoSize, (VOID**)&ListFileInfo);
    
    CHAR16 *KernelFileName = NULL;
    
    for (int i = 0; i < 10; i++) {
        ListFileInfoSize = sizeof(EFI_FILE_INFO) + 200;
        Status = uefi_call_wrapper(Root->Read, 3, Root, &ListFileInfoSize, ListFileInfo);
        
        if (EFI_ERROR(Status) || ListFileInfoSize == 0) break;
        
        CHAR16 prefix[] = L"  Found: ";
        uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, prefix);
        uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, ListFileInfo->FileName);
        CHAR16 newline[] = L"\n";
        uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, newline);
        
        CHAR16 *fn = ListFileInfo->FileName;
        BOOLEAN isKernel = FALSE;
        
        if (fn[0] == L'k' || fn[0] == L'K') {
            isKernel = TRUE;
            KernelFileName = ListFileInfo->FileName;
            CHAR16 found[] = L"  ^^ Using this as kernel!\n";
            uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, found);
            break;
        }
    }
    
    if (!KernelFileName) {
        CHAR16 err[] = L"ERROR: kernel.efi not found in directory listing\n";
        uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, err);
        while(1);
    }
    
    uefi_call_wrapper(Root->SetPosition, 2, Root, 0);
    
    CHAR16 debug3b[] = L"[3b] Opening kernel with exact filename...\n";
    uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, debug3b);
    
    Status = uefi_call_wrapper(Root->Open, 5, Root, &KernelFile, KernelFileName, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status)) {
        CHAR16 err3b[] = L"ERROR: Cannot open kernel using found filename\n";
        uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, err3b);
        while(1);
    }

    CHAR16 msg2[] = L"[4] kernel.efi opened successfully\n";
    uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, msg2);

    CHAR16 debug4[] = L"[5] Getting file info...\n";
    uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, debug4);
    
    EFI_FILE_INFO *FileInfo;
    UINTN FileInfoSize = sizeof(EFI_FILE_INFO) + 200;
    Status = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, FileInfoSize, (VOID**)&FileInfo);
    
    Status = uefi_call_wrapper(KernelFile->GetInfo, 4, KernelFile, &gEfiFileInfoGuid, &FileInfoSize, FileInfo);
    UINTN FileSize = (UINTN)FileInfo->FileSize;

    CHAR16 debug5[] = L"[6] Reading kernel into memory...\n";
    uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, debug5);
    
    VOID *KernelBuffer;
    Status = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, FileSize, &KernelBuffer);
    
    Status = uefi_call_wrapper(KernelFile->Read, 3, KernelFile, &FileSize, KernelBuffer);
    uefi_call_wrapper(KernelFile->Close, 1, KernelFile);

    CHAR16 msg3[] = L"[7] kernel.efi loaded into memory\n";
    uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, msg3);

    CHAR16 debug6[] = L"[8] Calling LoadImage...\n";
    uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, debug6);
    
    EFI_HANDLE KernelHandle;
    Status = uefi_call_wrapper(BS->LoadImage, 6,
        FALSE,
        ImageHandle,
        NULL,
        KernelBuffer,
        FileSize,
        &KernelHandle);

    if (EFI_ERROR(Status)) {
        CHAR16 err4[] = L"ERROR: LoadImage failed\n";
        uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, err4);
        while (1);
    }

    CHAR16 msg4[] = L"\n[9] Starting kernel...\n\n";
    uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, msg4);

    Status = uefi_call_wrapper(BS->StartImage, 3, KernelHandle, NULL, NULL);

    if (EFI_ERROR(Status)) {
        CHAR16 err5[] = L"\nERROR: StartImage failed\n";
        uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, err5);
        while (1);
    }

    CHAR16 msg5[] = L"\n[10] Kernel returned\n";

    uefi_call_wrapper(BS->FreePool, 1, FileInfo);
    uefi_call_wrapper(BS->FreePool, 1, KernelBuffer);

    CHAR16 done[] = L"\nBootloader done. Press any key...\n";

    for (;;){}
    return EFI_SUCCESS;
}