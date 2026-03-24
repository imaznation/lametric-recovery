#!/bin/bash
# Build kernel with WiFi + Audio + Display support
set -e

WINDIR="/mnt/c/path/to/lametric-recovery"
CC=arm-linux-gnueabihf-gcc
KDIR="/home/kernel/linux-5.15.175"

echo "=== Step 1: Compile shell_init.c ==="
$CC -static -O2 -o /home/kernel/initramfs/init "$WINDIR/shell_init.c" 2>&1
chmod +x /home/kernel/initramfs/init

# Ensure alsa.conf exists
mkdir -p /home/kernel/initramfs/home/kernel/alsa-sysroot/share/alsa
cat > /home/kernel/initramfs/home/kernel/alsa-sysroot/share/alsa/alsa.conf << 'ALSA'
pcm.!default { type hw; card 0; }
ctl.!default { type hw; card 0; }
ALSA

# Ensure amixer/aplay exist
test -f /home/kernel/initramfs/amixer || echo "WARNING: amixer not in initramfs"
test -f /home/kernel/initramfs/aplay || echo "WARNING: aplay not in initramfs"

echo ""
echo "=== Step 2: Configure kernel ==="
cd "$KDIR"
export ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-

# Start from current config, add WiFi
scripts/config --enable BLK_DEV_INITRD
scripts/config --set-str INITRAMFS_SOURCE "/home/kernel/initramfs"
scripts/config --enable DEVTMPFS
scripts/config --enable DEVTMPFS_MOUNT
scripts/config --enable DEVMEM
scripts/config --disable STRICT_DEVMEM

# I2C + SPI + GPIO
scripts/config --enable I2C_MV64XXX
scripts/config --enable I2C_CHARDEV
scripts/config --enable SPI_SUN4I
scripts/config --enable SPI_SPIDEV
scripts/config --enable GPIO_SYSFS
scripts/config --enable REGULATOR_FIXED_VOLTAGE

# USB
scripts/config --enable USB_GADGET
scripts/config --enable USB_MUSB_SUNXI
scripts/config --enable USB_G_SERIAL
scripts/config --enable USB_EHCI_HCD
scripts/config --enable USB_EHCI_PLATFORM
scripts/config --enable USB_OHCI_HCD
scripts/config --enable USB_OHCI_PLATFORM

# Audio
scripts/config --enable SOUND
scripts/config --enable SND
scripts/config --enable SND_SOC
scripts/config --enable SND_SUN4I_CODEC
scripts/config --enable SND_OSSEMUL
scripts/config --enable SND_PCM_OSS
scripts/config --enable SND_PCM_OSS_PLUGINS
scripts/config --enable SND_MIXER_OSS
scripts/config --enable DMADEVICES
scripts/config --enable DMA_SUN4I

# WiFi
scripts/config --enable WIRELESS
scripts/config --enable CFG80211
scripts/config --enable MAC80211
scripts/config --enable WLAN
scripts/config --enable WLAN_VENDOR_REALTEK
scripts/config --enable RTL8XXXU
scripts/config --enable NEW_LEDS
scripts/config --enable LEDS_CLASS
scripts/config --enable FW_LOADER
scripts/config --enable PACKET
scripts/config --enable INET
scripts/config --enable CRYPTO
scripts/config --enable CRYPTO_AES
scripts/config --enable CRYPTO_CCM
scripts/config --enable CRYPTO_ARC4

# Embed WiFi firmware in kernel
scripts/config --enable FIRMWARE_IN_KERNEL
if [ -d /home/kernel/firmware/rtlwifi ]; then
    scripts/config --set-str EXTRA_FIRMWARE "rtlwifi/rtl8723bu_nic.bin"
    scripts/config --set-str EXTRA_FIRMWARE_DIR "/home/kernel/firmware"
    echo "WiFi firmware will be embedded in kernel"
else
    echo "WARNING: WiFi firmware not found at /home/kernel/firmware/rtlwifi/"
fi

# Kernel cmdline
scripts/config --set-str CMDLINE "clk_ignore_unused"
scripts/config --enable CMDLINE_EXTEND

# Disable unused
scripts/config --disable DRM
scripts/config --disable VIDEO_DEV
scripts/config --disable MEDIA_SUPPORT
scripts/config --disable BT

make olddefconfig 2>&1 | tail -1

echo "--- Key config ---"
grep CONFIG_RTL8XXXU .config | head -1
grep CONFIG_CFG80211 .config | head -1
grep CONFIG_SND_SUN4I_CODEC .config | head -1
grep EXTRA_FIRMWARE .config | head -2

echo ""
echo "=== Step 3: Build kernel ==="
make -j$(nproc) uImage LOADADDR=0x40008000 2>&1 | tail -5

echo ""
echo "=== Step 4: Create uImage ==="
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
open('$WINDIR/lametric_515_wifi.uImage', 'wb').write(bytes(hdr) + raw)
print('Kernel: %d bytes (%.1f MB)' % (len(raw) + 64, (len(raw) + 64) / 1048576))
"

echo ""
echo "=== DONE ==="
