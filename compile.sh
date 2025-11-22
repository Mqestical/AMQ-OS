#!/bin/bash
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

# =========[ PART 1: BOOTLOADER ]=========
echo ""
echo "--- Compiling Bootloader (bootloader.c) ---"

BOOT_SRC="bootloader.c"
BOOT_OBJ="$OBJDIR/bootloader.o"
BOOT_SO="$SODIR/bootloader.so"

gcc $CFLAGS -c "$BOOT_SRC" -o "$BOOT_OBJ"
if [ $? -ne 0 ]; then echo "❌ Bootloader compilation failed"; exit 1; fi

ld $LDFLAGS /usr/lib/crt0-efi-x86_64.o "$BOOT_OBJ" \
   -o "$BOOT_SO" -lefi -lgnuefi
if [ $? -ne 0 ]; then echo "❌ Bootloader linking failed"; exit 1; fi

objcopy -j .text -j .sdata -j .data -j .dynamic \
        -j .dynsym -j .rel -j .rela -j .reloc \
        --target=efi-app-x86_64 \
        "$BOOT_SO" BOOTX64.EFI

echo "✓ Bootloader compiled: BOOTX64.EFI"

# =========[ PART 2: KERNEL ]=========
echo ""
echo "--- Compiling Kernel (recursive *.c in src/kernel) ---"

KERNEL_OBJS=""

# recursively find C files inside src/kernel/
while IFS= read -r SRC; do
    OBJ="$OBJDIR/$(basename "${SRC%.c}.o")"
    gcc $CFLAGS -c "$SRC" -o "$OBJ"
    if [ $? -ne 0 ]; then echo "❌ Kernel compilation failed for $SRC"; exit 1; fi
    echo "  Compiled: $SRC → $OBJ"
    KERNEL_OBJS="$KERNEL_OBJS $OBJ"
done < <(find src/kernel -type f -name "*.c")

# link kernel
KERNEL_SO="$SODIR/kernel.so"

ld $LDFLAGS /usr/lib/crt0-efi-x86_64.o $KERNEL_OBJS \
   -o "$KERNEL_SO" -lefi -lgnuefi
if [ $? -ne 0 ]; then echo "❌ Kernel linking failed"; exit 1; fi

# convert to real EFI
objcopy -j .text -j .sdata -j .data -j .dynamic \
        -j .dynsym -j .rel -j .rela -j .reloc \
        --target=efi-app-x86_64 \
        "$KERNEL_SO" kernel.efi

echo "✓ Kernel compiled: kernel.efi"

# =========[ PART 3: DISK IMAGE ]=========
echo ""
echo "=== Updating Disk Image ==="

LOOP=$(sudo losetup -fP --show efi_boot.img)
sudo mount "${LOOP}p1" /mnt/efi

sudo cp BOOTX64.EFI /mnt/efi/EFI/BOOT/BOOTX64.EFI
sudo cp kernel.efi /mnt/efi/kernel.efi

sudo umount /mnt/efi
sudo losetup -d "$LOOP"

echo "✓ Disk updated."

# =========[ PART 4: VDI MANAGEMENT ]=========
echo ""
echo "=== Updating VirtualBox ==="

VBoxManage controlvm "EFI_Test" poweroff 2>/dev/null
sleep 2

VBoxManage storageattach "EFI_Test" --storagectl "SATA" \
    --port 0 --device 0 --medium none 2>/dev/null

TIMESTAMP=$(date +%s)
VDI="${VDIDIR}/efi_boot_${TIMESTAMP}.vdi"

VBoxManage convertfromraw efi_boot.img "$VDI" --format VDI
VBoxManage storageattach "EFI_Test" \
    --storagectl "SATA" --port 0 --device 0 \
    --type hdd --medium "$(pwd)/$VDI"

echo "✓ VirtualBox updated"
echo "  VDI stored in $VDIDIR"

# clean up old VDIs (keep latest 3)
ls -t ${VDIDIR}/efi_boot_*.vdi | tail -n +4 | xargs -r rm -f

echo ""
echo "=== Starting VM ==="
VBoxManage startvm "EFI_Test"

echo ""
echo "✅ Done!"
