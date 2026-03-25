#!/bin/bash
# Quick rebuild: recompile shell_init.c → rebuild kernel with built-in initramfs
#
# Run from WSL:
#   bash /mnt/c/path/to/lametric-recovery/rebuild_shell.sh
#
# Prerequisites:
#   - ARM cross-compiler: sudo apt install gcc-arm-linux-gnueabihf
#   - Linux 5.15.175 kernel source at $KDIR with CONFIG_INITRAMFS_SOURCE set
#   - Initramfs directory at $INITRAMFS_DIR with device nodes and modules
set -e

# Auto-detect source directory from script location
SRCDIR="$(cd "$(dirname "$0")" && pwd)"
CC="${CC:-arm-linux-gnueabihf-gcc}"
KDIR="${KDIR:-/home/kernel/linux-5.15.175}"
INITRAMFS_DIR="${INITRAMFS_DIR:-/home/kernel/initramfs}"
OUTPUT="${OUTPUT:-$SRCDIR/lametric_515_shell.uImage}"

echo "=== Step 1: Compile shell_init.c ==="
$CC -static -O2 -o "$INITRAMFS_DIR/init" "$SRCDIR/shell_init.c" 2>&1
chmod +x "$INITRAMFS_DIR/init"
ls -la "$INITRAMFS_DIR/init"

echo ""
echo "=== Step 2: Rebuild kernel (built-in initramfs) ==="
cd "$KDIR"
export ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-

# Initramfs source should already be set; verify
grep CONFIG_INITRAMFS_SOURCE .config

# Force initramfs rebuild (prevent stale CPIO cache)
rm -f usr/initramfs_data.cpio usr/initramfs_data.cpio.zst

# Rebuild (only init changed, kernel config same)
make -j$(nproc) uImage LOADADDR=0x40008000 2>&1 | tail -10

echo ""
echo "=== Step 3: Create fastboot uImage ==="
python3 -c "
import struct, zlib, time
f = open('arch/arm/boot/uImage', 'rb')
f.seek(12); ds = struct.unpack('>I', f.read(4))[0]; f.seek(64)
raw = f.read(ds)
hdr = bytearray(64)
struct.pack_into('>I', hdr, 0, 0x27051956)
struct.pack_into('>I', hdr, 8, int(time.time()))
struct.pack_into('>I', hdr, 12, len(raw))
struct.pack_into('>I', hdr, 16, 0x40008000)
struct.pack_into('>I', hdr, 20, 0x40008000)
struct.pack_into('>I', hdr, 24, zlib.crc32(raw) & 0xFFFFFFFF)
hdr[28] = 5; hdr[29] = 2; hdr[30] = 2; hdr[31] = 0
hdr[32:44] = b'LaMetricOS\x00\x00'
hdr[4:8] = b'\x00\x00\x00\x00'
struct.pack_into('>I', hdr, 4, zlib.crc32(bytes(hdr)) & 0xFFFFFFFF)
open('$OUTPUT', 'wb').write(bytes(hdr) + raw)
print('Kernel uImage: %d bytes (%.1f MB)' % (len(raw) + 64, (len(raw) + 64) / 1048576))
"

echo ""
echo "=== DONE ==="
echo "Output: $OUTPUT"
echo "Deploy: sunxi-fel uboot u-boot-AUTOBOOT.bin write 0x42000000 lametric_515_shell.uImage write 0x43000000 sun5i-a13-lametric-515-padded.dtb"
