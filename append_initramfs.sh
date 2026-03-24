#!/bin/bash
set -e
KERNEL=/mnt/c/path/to/lametric-recovery/kernel_raw.bin
INITRAMFS=/mnt/c/path/to/lametric-recovery/console_initramfs.gz
OUTPUT=/mnt/c/path/to/lametric-recovery/kernel_combined.bin

cat "$KERNEL" "$INITRAMFS" > "$OUTPUT"
echo "Kernel: $(stat -c%s "$KERNEL")"
echo "Initramfs: $(stat -c%s "$INITRAMFS")"
echo "Combined: $(stat -c%s "$OUTPUT")"
