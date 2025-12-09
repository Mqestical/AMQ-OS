#!/bin/bash
set -e

echo "=== Compiling Bootloader and Kernel ==="

# =========[ DIRECTORIES ]=========
OBJDIR="obj"
SODIR="bin_so"
VDIDIR="vdi"

mkdir -p "$OBJDIR" "$SODIR" "$VDIDIR"

# =========[ FLAGS ]=========
CFLAGS="-I/usr/include/efi -I/usr/include/efi/x86_64 -I./includes \
-fpic -ffreestanding -fno-stack-protector -fno-stack-check -fshort-wchar \
-mno-red-zone -maccumulate-outgoing-args"

LDFLAGS="-nostdlib -znocombreloc -T /usr/lib/elf_x86_64_efi.lds \
-shared -Bsymbolic -L /usr/lib"

# NASM flags for assembly
NASMFLAGS="-f elf64"

# =========[ PART 1: BOOTLOADER ]=========
echo ""
echo "--- Compiling Bootloader (bootloader.c) ---"

BOOT_SRC="bootloader.c"
BOOT_OBJ="$OBJDIR/bootloader.o"
BOOT_SO="$SODIR/bootloader.so"

gcc $CFLAGS -c "$BOOT_SRC" -o "$BOOT_OBJ"

ld $LDFLAGS /usr/lib/crt0-efi-x86_64.o "$BOOT_OBJ" -o "$BOOT_SO" -lefi -lgnuefi

objcopy -j .text -j .sdata -j .data -j .dynamic \
        -j .dynsym -j .rel -j .rela -j .reloc \
        --target=efi-app-x86_64 "$BOOT_SO" BOOTX64.EFI

echo "✓ Bootloader compiled: BOOTX64.EFI"

# =========[ PART 2: KERNEL ]=========
echo ""
echo "--- Compiling Kernel (recursive *.c and *.s in src/kernel) ---"

KERNEL_OBJS=""

# Compile all .c files
while IFS= read -r SRC; do
    OBJ="$OBJDIR/$(basename "${SRC%.c}.o")"
    gcc $CFLAGS -c "$SRC" -o "$OBJ"
    echo "  Compiled C: $SRC → $OBJ"
    KERNEL_OBJS="$KERNEL_OBJS $OBJ"
done < <(find src/kernel -type f -name "*.c")

# Compile all .s/.asm files with NASM
while IFS= read -r SRC; do
    BASENAME=$(basename "$SRC")
    OBJ="$OBJDIR/${BASENAME%.*}.o"
    nasm $NASMFLAGS "$SRC" -o "$OBJ"
    echo "  Compiled ASM: $SRC → $OBJ"
    KERNEL_OBJS="$KERNEL_OBJS $OBJ"
done < <(find src/kernel -type f \( -name "*.s" -o -name "*.asm" \))

KERNEL_SO="$SODIR/kernel.so"

ld $LDFLAGS /usr/lib/crt0-efi-x86_64.o $KERNEL_OBJS -o "$KERNEL_SO" -lefi -lgnuefi

objcopy -j .text -j .sdata -j .data -j .dynamic \
        -j .dynsym -j .rel -j .rela -j .reloc \
        --target=efi-app-x86_64 "$KERNEL_SO" kernel.efi

echo "✓ Kernel compiled: kernel.efi"

# =========[ PART 3: DISK IMAGE ]=========
echo ""
echo "=== Creating EFI Disk Image ==="

EFI_IMG="efi_boot.img"
sudo rm -f "$EFI_IMG"

# Create 64MB raw FAT32 image
dd if=/dev/zero of="$EFI_IMG" bs=1M count=64
mkfs.vfat "$EFI_IMG"

# Mount and copy files
sudo mkdir -p /mnt/efi
sudo mount -o loop "$EFI_IMG" /mnt/efi
sudo mkdir -p /mnt/efi/EFI/BOOT
sudo cp BOOTX64.EFI /mnt/efi/EFI/BOOT/
sudo cp kernel.efi /mnt/efi/
sudo umount /mnt/efi

echo "✓ Disk updated: $EFI_IMG"

# =========[ PART 4: DATA DISK ]=========
echo ""
echo "=== Creating Data Disk for Filesystem ==="

DATA_DISK="data_disk.img"
DATA_VDI="${VDIDIR}/data_disk.vdi"

# Create 10MB raw disk
dd if=/dev/zero of="$DATA_DISK" bs=1M count=10

# Convert to VDI (only if it doesn't exist)
if [ ! -f "$DATA_VDI" ]; then
    VBoxManage convertfromraw "$DATA_DISK" "$DATA_VDI" --format VDI
    echo "✓ Data disk created: $DATA_VDI"
else
    echo "✓ Using existing data disk: $DATA_VDI"
fi

echo "✓ Data disk ready: $DATA_VDI"

# =========[ PART 5: VDI MANAGEMENT ]=========
echo ""
echo "=== Updating VirtualBox ==="

VM_NAME="EFI_Test"

# Power off VM if running
VBoxManage controlvm "$VM_NAME" poweroff 2>/dev/null || true
sleep 1
VBoxManage storageattach "$VM_NAME" --storagectl "SATA" --port 0 --device 0 --medium none 2>/dev/null || true

# Convert raw image to VDI
TIMESTAMP=$(date +%s)
VDI="${VDIDIR}/efi_boot_${TIMESTAMP}.vdi"
VBoxManage convertfromraw "$EFI_IMG" "$VDI" --format VDI

# Attach boot VDI to VM
VBoxManage storageattach "$VM_NAME" --storagectl "SATA" --port 0 --device 0 \
    --type hdd --medium "$(pwd)/$VDI"

# Attach data disk to SATA Port 1 (with absolute path)
VBoxManage storageattach "$VM_NAME" --storagectl "SATA" --port 1 --device 0 \
    --type hdd --medium "$(pwd)/$DATA_VDI" 2>/dev/null || true

# Cleanup old VDIs (keep latest 3)
ls -t ${VDIDIR}/efi_boot_*.vdi | tail -n +4 | xargs -r rm -f

echo "✓ VirtualBox updated: $VDI"

# =========[ PART 6: START VM ]=========
echo ""
echo "=== Starting VM ==="
VBoxManage startvm "$VM_NAME"
echo ""
echo "✅ Done!"