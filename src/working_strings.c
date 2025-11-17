#include <efi.h>
#include <efilib.h>

EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Volume;
EFI_FILE_PROTOCOL *Root;

EFI_STATUS
EFIAPI
efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    InitializeLib(ImageHandle, SystemTable);
    
    uefi_call_wrapper(ST->ConOut->ClearScreen, 1, ST->ConOut);

    CHAR16 msg[] = {
        0x0057,0x0065,0x006C,0x0063,0x006F,0x006D,0x0065,0x0020,
        0x0074,0x006F,0x0020,0x0041,0x004D,0x0051,0x0020,0x004F,
        0x0070,0x0065,0x0072,0x0061,0x0074,0x0069,0x006E,0x0067,
        0x0020,0x0053,0x0079,0x0073,0x0074,0x0065,0x006D,0x002C,
        0x0020,0x0053,0x0075,0x0070,0x0065,0x0072,0x0075,0x0073,
        0x0065,0x0072,0x002E,0x0020,0x004C,0x006F,0x0061,0x0064,
        0x0069,0x006E,0x0067,0x0020,0x006B,0x0065,0x0072,0x006E,
        0x0065,0x006C,0x002C,0x0020,0x0073,0x0074,0x0061,0x006E,
        0x0064,0x0062,0x0079,0x002E,0x002E,0x0000
    };

    uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, msg); 

    EFI_FILE_PROTOCOL *KernelFile;
    gBS->LocateProtocol(&gEfiSimpleFileSystemProtocolGuid, NULL, (VOID**)&Volume);
    Volume->OpenVolume(Volume, &Root);
    Root->Open(Root, &KernelFile, L"kernel.elf", EFI_FILE_MODE_READ, 0);

    // ---- Get file size ----
    EFI_FILE_INFO *FileInfo;
    UINTN FileInfoSize = sizeof(EFI_FILE_INFO) + 200;
    FileInfo = AllocatePool(FileInfoSize);
    KernelFile->GetInfo(KernelFile, &gEfiFileInfoGuid, &FileInfoSize, FileInfo);

    UINTN FileSize = (UINTN)FileInfo->FileSize;

    // ---- Read file ----
    VOID *KernelBuffer = AllocatePool(FileSize);
    KernelFile->Read(KernelFile, &FileSize, KernelBuffer);

    // ---- Allocate pages where kernel will be loaded ----
    EFI_PHYSICAL_ADDRESS TargetAddress = 0xFFFFFFFF80000000;
    UINTN NumberOfPages = EFI_SIZE_TO_PAGES(FileSize);

    EFI_STATUS Status = gBS->AllocatePages(
        AllocateAddress,
        EfiLoaderData,
        NumberOfPages,
        &TargetAddress
    );

    // ---- Copy kernel into memory ----
    CopyMem((VOID*)TargetAddress, KernelBuffer, FileSize);

    FreePool(FileInfo);
    UINTN MemoryMapSize = 0;
EFI_MEMORY_DESCRIPTOR *MemoryMap = NULL;
UINTN MapKey;
UINTN DescriptorSize;
UINT32 DescriptorVersion;
gBS->GetMemoryMap(&MemoryMapSize, MemoryMap, &MapKey, &DescriptorSize, &DescriptorVersion);
MemoryMap = AllocatePool(MemoryMapSize);
gBS->GetMemoryMap(&MemoryMapSize, MemoryMap, &MapKey, &DescriptorSize, &DescriptorVersion);
    while (1);

    return EFI_SUCCESS;
}