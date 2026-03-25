#!/bin/bash
set -euo pipefail

# ============================================================================
# LaMetric Time Recovery — One-Click Boot Script (macOS)
# ============================================================================
# Boots the LaMetric Time from FEL mode via USB, waits for the serial console,
# syncs the clock, starts the widget carousel, and optionally starts the daemon.
# ============================================================================

SCRIPTDIR="$(cd "$(dirname "$0")" && pwd)"

# File names (edit here if your filenames differ)
FEL="$SCRIPTDIR/sunxi-fel"
UBOOT="$SCRIPTDIR/u-boot-AUTOBOOT.bin"
KERNEL="$SCRIPTDIR/lametric_515_wifi.uImage"
DTB="$SCRIPTDIR/sun5i-a13-lametric-515-padded.dtb"
DAEMON="$SCRIPTDIR/lametric_daemon.py"

echo ""
echo "  ============================================================"
echo "   LaMetric Time Recovery — One-Click Boot (macOS)"
echo "  ============================================================"
echo ""
echo "  Script directory: $SCRIPTDIR"
echo ""

# ============================================================================
# STEP 1 — Verify required files exist
# ============================================================================
echo "  [1/6] Checking required files..."

MISSING=0
for f in "$FEL" "$UBOOT" "$KERNEL" "$DTB"; do
    if [ ! -f "$f" ]; then
        echo "        MISSING: $(basename "$f")"
        MISSING=1
    fi
done

if [ "$MISSING" -eq 1 ]; then
    echo ""
    echo "  ERROR: One or more required files are missing from:"
    echo "         $SCRIPTDIR"
    echo ""
    echo "  Required firmware files (not in git — copy from your Windows setup):"
    echo "    - u-boot-AUTOBOOT.bin"
    echo "    - lametric_515_wifi.uImage"
    echo "    - sun5i-a13-lametric-515-padded.dtb"
    exit 1
fi

echo "        All files found."
echo ""

# ============================================================================
# STEP 2 — Check for FEL device
# ============================================================================
echo "  [2/6] Checking for device in FEL mode..."

FEL_FOUND=0
FEL_RETRIES=0
FEL_MAX=30

while [ $FEL_RETRIES -lt $FEL_MAX ]; do
    if "$FEL" ver >/dev/null 2>&1; then
        FEL_FOUND=1
        break
    fi

    if [ $FEL_RETRIES -eq 0 ]; then
        echo ""
        echo "        Device NOT detected in FEL mode."
        echo ""
        echo "        To enter FEL mode:"
        echo "          1. Unplug the LaMetric from USB"
        echo "          2. Hold the round button on top of the device"
        echo "          3. While holding, plug the USB cable back in"
        echo "          4. Release the button after 2 seconds"
        echo ""
        echo "        Waiting for FEL device (timeout: 60 seconds)..."
    fi

    FEL_RETRIES=$((FEL_RETRIES + 1))

    if [ $((FEL_RETRIES % 5)) -eq 0 ]; then
        printf "."
    fi

    sleep 2
done

if [ "$FEL_FOUND" -eq 0 ]; then
    echo ""
    echo "  ERROR: Timed out waiting for FEL device."
    echo "         Make sure the device is in FEL mode and the USB cable supports data."
    echo ""
    echo "         On macOS, no special driver is needed (libusb works natively)."
    echo "         If it still fails, check: system_profiler SPUSBDataType | grep -A5 'sunxi\\|1f3a'"
    exit 1
fi

echo "        FEL device detected!"
echo ""

echo "  --------------------------------------------------------"
"$FEL" ver
echo "  --------------------------------------------------------"
echo ""

# ============================================================================
# STEP 3 — Load u-boot, kernel, and DTB
# ============================================================================
echo "  [3/6] Loading firmware via FEL..."
echo ""
echo "        u-boot : $(basename "$UBOOT")"
echo "        kernel : $(basename "$KERNEL")"
echo "        DTB    : $(basename "$DTB")"
echo ""

if ! "$FEL" uboot "$UBOOT" write 0x42000000 "$KERNEL" write 0x43000000 "$DTB"; then
    echo ""
    echo "  ERROR: FEL upload failed."
    echo "         Try unplugging and re-entering FEL mode."
    exit 1
fi

echo ""
echo "        Firmware loaded successfully! Device is booting..."
echo ""

# ============================================================================
# STEP 4 — Wait for serial port to appear
# ============================================================================
echo "  [4/6] Waiting for USB serial port..."
echo "        The kernel takes ~10 seconds to boot and enable USB gadget serial."
echo ""

SERIAL_PORT=""
COM_RETRIES=0
COM_MAX=30

while [ $COM_RETRIES -lt $COM_MAX ]; do
    # Look for the CDC ACM serial device (typical macOS paths)
    for dev in /dev/cu.usbmodem* /dev/tty.usbmodem*; do
        if [ -c "$dev" ] 2>/dev/null; then
            SERIAL_PORT="$dev"
            break 2
        fi
    done

    COM_RETRIES=$((COM_RETRIES + 1))

    if [ $((COM_RETRIES % 3)) -eq 0 ]; then
        printf "."
    fi

    sleep 2
done

if [ -z "$SERIAL_PORT" ]; then
    echo ""
    echo "  WARNING: Timed out waiting for serial port (60 seconds)."
    echo "           The device may still be booting. Check:"
    echo "             ls /dev/cu.usbmodem*"
    echo "             ls /dev/tty.usbmodem*"
    echo ""
    echo "  You can still run: python3 $DAEMON"
    echo ""
else
    echo ""
    echo "        Found serial port: $SERIAL_PORT"
    echo ""

    # Brief pause to let the device finish init
    sleep 3

    # ============================================================================
    # STEP 5 — Sync time and start carousel
    # ============================================================================
    echo "  [5/6] Syncing time and starting carousel on $SERIAL_PORT..."
    echo ""

    TIMESTR=$(date +%H:%M:%S)

    # Send commands via Python (cross-platform serial write)
    python3 -c "
import serial, time
port = serial.Serial('$SERIAL_PORT', 115200, timeout=2)
time.sleep(0.5)
port.write(b'time $TIMESTR\n')
time.sleep(0.5)
port.write(b'carousel\n')
time.sleep(0.5)
port.close()
print('        Time synced: $TIMESTR')
print('        Carousel mode started.')
" 2>&1 || echo "  WARNING: Could not send commands to $SERIAL_PORT."

    echo ""
fi

# ============================================================================
# STEP 6 — Offer to start the daemon
# ============================================================================
echo "  [6/6] Boot complete!"
echo ""
echo "  ============================================================"
echo "   LaMetric Time is RUNNING"
echo "  ============================================================"
echo ""

if [ -f "$DAEMON" ]; then
    echo "  The REST API daemon provides a web interface on port 8080."
    if [ -n "$SERIAL_PORT" ]; then
        echo "  To start it, run:"
        echo ""
        echo "      python3 $DAEMON --port $SERIAL_PORT"
    else
        echo "  To start it, run:"
        echo ""
        echo "      python3 $DAEMON"
    fi
    echo ""

    read -rp "  Start the REST API daemon now? [y/N] " answer
    if [[ "$answer" =~ ^[Yy]$ ]]; then
        echo ""
        echo "  Starting daemon..."
        echo ""
        if [ -n "$SERIAL_PORT" ]; then
            python3 "$DAEMON" --port "$SERIAL_PORT"
        else
            python3 "$DAEMON"
        fi
    fi
else
    echo "  (lametric_daemon.py not found — skipping daemon prompt)"
fi

echo ""
echo "  Done."
