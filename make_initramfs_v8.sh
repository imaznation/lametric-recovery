#!/bin/bash
set -e
IR=/tmp/ir_v8
rm -rf $IR && mkdir -p $IR/{bin,lib/modules,dev,proc,sys,lib}

cp /tmp/rootfs_mod/bin/busybox $IR/bin/busybox
chmod +x $IR/bin/busybox
for cmd in sh mount insmod ls cat echo mkdir ln sleep find grep reboot devmem; do
    ln -s busybox $IR/bin/$cmd
done

cp /tmp/rootfs_mod/lib/ld-linux.so.3 $IR/lib/
cp /tmp/rootfs_mod/lib/libc.so.6 $IR/lib/
cp /tmp/rootfs_mod/lib/libdl.so.2 $IR/lib/
cp /tmp/rootfs_mod/lib/libpthread.so.0 $IR/lib/

KMOD=/tmp/rootfs_mod/lib/modules/4.4.13SA01.1/kernel
cp $KMOD/fs/configfs/configfs.ko $IR/lib/modules/
cp $KMOD/drivers/usb/gadget/libcomposite.ko $IR/lib/modules/
cp $KMOD/drivers/usb/gadget/function/usb_f_mass_storage.ko $IR/lib/modules/

mknod $IR/dev/console c 5 1
mknod $IR/dev/null c 1 3
mknod $IR/dev/mem c 1 1

# Init that dumps diagnostics to fixed DRAM address then warm reboots
cat > $IR/init << 'INITSCRIPT'
#!/bin/sh
B=/bin/busybox
$B mount -t proc proc /proc
$B mount -t sysfs sysfs /sys
$B mount -t devtmpfs devtmpfs /dev

# Collect all debug info into a string
LOG="=== RECOVERY v8 ===\n"
LOG="${LOG}UDC: $($B ls /sys/class/udc/ 2>&1)\n"
LOG="${LOG}MUSB: $($B find /sys/devices -path "*musb*" -name "mode" 2>/dev/null)\n"

# Read musb mode
for f in $($B find /sys/devices -path "*musb*" -name "mode" 2>/dev/null); do
    LOG="${LOG}MODE[$f]: $($B cat $f 2>&1)\n"
done

# Try forcing peripheral mode
for f in $($B find /sys/devices -path "*musb*" -name "mode" 2>/dev/null); do
    $B echo "b_peripheral" > "$f" 2>/dev/null
    LOG="${LOG}AFTER_FORCE[$f]: $($B cat $f 2>&1)\n"
done

LOG="${LOG}UDC_AFTER: $($B ls /sys/class/udc/ 2>&1)\n"

# Try loading modules
$B insmod /lib/modules/configfs.ko 2>/dev/null; LOG="${LOG}configfs: $?\n"
$B mount -t configfs none /sys/kernel/config 2>/dev/null; LOG="${LOG}mount_configfs: $?\n"
$B insmod /lib/modules/libcomposite.ko 2>/dev/null; LOG="${LOG}libcomposite: $?\n"
$B insmod /lib/modules/usb_f_mass_storage.ko 2>/dev/null; LOG="${LOG}usb_f_mass: $?\n"

LOG="${LOG}UDC_FINAL: $($B ls /sys/class/udc/ 2>&1)\n"
LOG="${LOG}CONFIGFS: $($B ls /sys/kernel/config/ 2>&1)\n"
LOG="${LOG}GADGET_DIR: $($B ls /sys/kernel/config/usb_gadget/ 2>&1)\n"
LOG="${LOG}=== END ===\n"

# Write log to known DRAM address (0x5F000000) using devmem or dd
# Use printf to output, pipe through dd to /dev/mem
$B echo -e "$LOG" > /tmp/debug.txt
# Copy to physical memory at 0x5F000000 via /dev/mem
$B dd if=/tmp/debug.txt of=/dev/mem bs=1 seek=$((0x5F000000)) 2>/dev/null

# Also try writing a magic marker
$B echo "LAMETRIC_DEBUG_OK" | $B dd of=/dev/mem bs=1 seek=$((0x5FF00000)) 2>/dev/null

# Warm reboot (preserves DRAM)
$B sleep 2
$B echo b > /proc/sysrq-trigger
INITSCRIPT
chmod +x $IR/init

dd if=/dev/zero of=/tmp/rd8.img bs=1M count=8 2>/dev/null
mke2fs -F -d $IR /tmp/rd8.img 2>&1 | tail -1
gzip -9 -c /tmp/rd8.img > /tmp/rd8.gz
/tmp/uboot_fix/tools/mkimage -A arm -T ramdisk -C gzip -d /tmp/rd8.gz /mnt/c/path/to/lametric-recovery/ramdisk_v8.uImage
ls -la /mnt/c/path/to/lametric-recovery/ramdisk_v8.uImage
echo "DONE"
