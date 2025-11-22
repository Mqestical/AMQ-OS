#!/bin/bash
echo "=== Compiling Bootloader and Kernel ==="

# Compiler flags
CFLAGS="-I/usr/include/efi -I/usr/include/efi/x86_64 -I../includes \
-fpic -ffreestanding -fno-stack-protector -fno-stack-check -fshort-wchar \
-mno-red-zone -maccumulate-outgoing-args"

LDFLAGS="-nostdlib -znocombreloc -T /usr/lib/elf_x86_64_efi.lds -shared -Bsymbolic -L /usr/lib"

# ============================================================
# PART 1: Compile BOOTLOADER (working_strings.c)
# ============================================================
echo ""
echo "--- Compiling Bootloader ---"

gcc $CFLAGS -c working_strings.c -o working_strings.o
if [ $? -ne 0 ]; then
    echo "❌ Bootloader compilation failed"
    exit 1
fi

# Link bootloader
ld $LDFLAGS /usr/lib/crt0-efi-x86_64.o working_strings.o -o working_strings.so -lefi -lgnuefi
if [ $? -ne 0 ]; then
    echo "❌ Bootloader linking failed"
    exit 1
fi

# Convert to EFI
objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym \
        -j .rel -j .rela -j .reloc --target=efi-app-x86_64 \
        working_strings.so working_strings.efi
if [ $? -ne 0 ]; then
    echo "❌ Bootloader objcopy failed"
    exit 1
fi

echo "✓ Bootloader compiled: working_strings.efi"

# ============================================================
# PART 2: Compile KERNEL (kernel/*.c files)
# ============================================================
echo ""
echo "--- Compiling Kernel ---"

KERNEL_OBJS=""
mkdir -p obj

for SRC in kernel/*.c; do
    OBJ="obj/$(basename "${SRC%.c}.o")"
    gcc $CFLAGS -c "$SRC" -o "$OBJ"
    if [ $? -ne 0 ]; then
        echo "❌ Kernel compilation failed for $SRC"
        exit 1
    fi
    KERNEL_OBJS="$KERNEL_OBJS $OBJ"
    echo "  Compiled: $SRC → $OBJ"
done

# Link kernel
ld $LDFLAGS /usr/lib/crt0-efi-x86_64.o $KERNEL_OBJS -o kernel.so -lefi -lgnuefi
if [ $? -ne 0 ]; then
    echo "❌ Kernel linking failed"
    exit 1
fi

# Convert to EFI
objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym \
        -j .rel -j .rela -j .reloc --target=efi-app-x86_64 \
        kernel.so kernel.efi
if [ $? -ne 0 ]; then
    echo "❌ Kernel objcopy failed"
    exit 1
fi

echo "✓ Kernel compiled: kernel.efi"

# ============================================================
# PART 3: Update disk image
# ============================================================
echo ""
echo "=== Updating disk image ==="

LOOP=$(sudo losetup -fP --show efi_boot.img)
sudo mount "${LOOP}p1" /mnt/efi

# Copy BOOTLOADER to BOOTX64.EFI (UEFI boots this first)
sudo cp working_strings.efi /mnt/efi/EFI/BOOT/BOOTX64.EFI

# Copy KERNEL to kernel.efi (bootloader will load this)
sudo cp kernel.efi /mnt/efi/kernel.efi

sudo umount /mnt/efi
sudo losetup -d "$LOOP"

echo "✓ Disk updated:"
echo "  - BOOTX64.EFI ← working_strings.efi (bootloader)"
echo "  - kernel.efi ← kernel.efi (kernel)"

# ============================================================
# PART 4: Update VirtualBox VM
# ============================================================
echo ""
echo "=== Updating VirtualBox ==="

VBoxManage controlvm "EFI_Test" poweroff 2>/dev/null
sleep 2

VBoxManage storageattach "EFI_Test" --storagectl "SATA" --port 0 --device 0 --medium none 2>/dev/null

# Create new VDI with timestamp
TIMESTAMP=$(date +%s)
VBoxManage convertfromraw efi_boot.img efi_boot_${TIMESTAMP}.vdi --format VDI
VBoxManage storageattach "EFI_Test" --storagectl "SATA" --port 0 --device 0 --type hdd \
    --medium "$(pwd)/efi_boot_${TIMESTAMP}.vdi"

echo "✓ VirtualBox updated"

# ============================================================
# PART 5: Start VM
# ============================================================
echo ""
echo "=== Starting VM ==="
VBoxManage startvm "EFI_Test"

echo ""
echo "✅ Done! VM is booting..."
echo "   Boot sequence: BOOTX64.EFI (bootloader) → kernel.efi (kernel)"

# Clean up old VDI files (keep last 3)
ls -t efi_boot_*.vdi | tail -n +4 | xargs -r rm -f
