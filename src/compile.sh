#!/bin/bash

echo "=== Compiling EFI Application ==="

# 1. Compile all source files
# Current directory: working_strings.c
# Kernel directory: start.c, memory.c, print.c
SRC_FILES="working_strings.c $(ls kernel/*.c)"

OBJ_FILES=""
for SRC in $SRC_FILES; do
    OBJ=$(basename "${SRC%.c}.o")  # strips directory for object file name
    gcc -I/usr/include/efi -I/usr/include/efi/x86_64 \
        -fpic -ffreestanding -fno-stack-protector \
        -fno-stack-check -fshort-wchar -mno-red-zone \
        -maccumulate-outgoing-args \
        -c $SRC -o $OBJ

    if [ $? -ne 0 ]; then
        echo "❌ Compilation failed for $SRC"
        exit 1
    fi

    OBJ_FILES="$OBJ_FILES $OBJ"
done

# 2. Link all object files into a single EFI shared object
ld -nostdlib -znocombreloc -T /usr/lib/elf_x86_64_efi.lds \
   -shared -Bsymbolic -L /usr/lib /usr/lib/crt0-efi-x86_64.o \
   $OBJ_FILES -o kernel.so -lefi -lgnuefi

if [ $? -ne 0 ]; then
    echo "❌ Linking failed"
    exit 1
fi

# 3. Convert the shared object to an EFI executable
objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym \
        -j .rel -j .rela -j .reloc --target=efi-app-x86_64 \
        kernel.so kernel.efi

if [ $? -ne 0 ]; then
    echo "❌ objcopy failed"
    exit 1
fi

echo "✓ Compilation successful"

# 4. Update disk image
echo "=== Updating disk image ==="
LOOP=$(sudo losetup -fP --show efi_boot.img)
sudo mount ${LOOP}p1 /mnt/efi
sudo cp kernel.efi /mnt/efi/EFI/BOOT/BOOTX64.EFI
sudo umount /mnt/efi
sudo losetup -d $LOOP

echo "✓ Disk updated"

# 5. Update VirtualBox VM
echo "=== Updating VirtualBox ==="
VBoxManage controlvm "EFI_Test" poweroff 2>/dev/null
sleep 2

VBoxManage storageattach "EFI_Test" --storagectl "SATA" --port 0 --device 0 --medium none 2>/dev/null

# Create new VDI with timestamp
TIMESTAMP=$(date +%s)
VBoxManage convertfromraw efi_boot.img efi_boot_${TIMESTAMP}.vdi --format VDI

VBoxManage storageattach "EFI_Test" --storagectl "SATA" --port 0 --device 0 --type hdd --medium "$(pwd)/efi_boot_${TIMESTAMP}.vdi"

echo "✓ VirtualBox updated"

# 6. Start VM
echo "=== Starting VM ==="
VBoxManage startvm "EFI_Test"

echo ""
echo "✅ Done! VM is booting..."

# Clean up old VDI files (keep last 3)
ls -t efi_boot_*.vdi | tail -n +4 | xargs -r rm -f
