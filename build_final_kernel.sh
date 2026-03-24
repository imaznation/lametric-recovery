#!/bin/bash
set -e

KDIR=/home/kernel/linux-5.15.175
IR=/home/kernel/ir_final
INIT_SRC=/mnt/c/path/to/lametric-recovery/mini_init7.c
OUTPUT=/mnt/c/path/to/lametric-recovery/console_uImage

export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabihf-

# Build init
echo "=== Building init ==="
arm-linux-gnueabihf-gcc -static -O2 -o /home/kernel/cinit "$INIT_SRC" 2>&1 | grep -v warning || true
ls -la /home/kernel/cinit

# Create initramfs
echo "=== Creating initramfs ==="
rm -rf "$IR"
mkdir -p "$IR/dev" "$IR/proc" "$IR/sys" "$IR/tmp" "$IR/lib/modules"
cp /home/kernel/cinit "$IR/init"
chmod +x "$IR/init"
mknod "$IR/dev/console" c 5 1
mknod "$IR/dev/null" c 1 3
mknod "$IR/dev/mem" c 1 1
mknod "$IR/dev/zero" c 1 5

# Copy USB gadget modules
cd "$KDIR"
for m in g_serial.ko libcomposite.ko u_serial.ko usb_f_acm.ko usb_f_serial.ko configfs.ko; do
    f=$(find . -name "$m" -print -quit 2>/dev/null)
    if [ -n "$f" ]; then
        cp "$f" "$IR/lib/modules/"
        echo "  Found: $m"
    fi
done

echo "=== Initramfs contents ==="
ls -la "$IR/init"
ls "$IR/lib/modules/"

# Create CPIO
cd "$IR"
find . | cpio -H newc -o 2>/dev/null > /home/kernel/final.cpio
echo "CPIO: $(stat -c%s /home/kernel/final.cpio) bytes"

# Rebuild kernel
echo "=== Building kernel ==="
cd "$KDIR"
scripts/config --set-str INITRAMFS_SOURCE "/home/kernel/final.cpio"
make olddefconfig 2>&1 | tail -1
make -j$(nproc) uImage LOADADDR=0x40008000 2>&1 | tail -5
cp arch/arm/boot/uImage "$OUTPUT"
ls -lh "$OUTPUT"
echo "=== DONE ==="
