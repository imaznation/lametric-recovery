# LaMetric Time Recovery Kit — Setup on a New PC

This guide explains how to set up the LaMetric Time Recovery Kit on any Windows PC so you can boot, control, and use the device without the original firmware.

---

## 1. Files to Copy

Copy the following files into a single folder (e.g., `C:\lametric-recovery\`):

### Required for boot

| File | Purpose |
|------|---------|
| `sunxi-fel.exe` | Allwinner FEL USB boot loader tool |
| `libusb\MinGW64\dll\libusb-1.0.dll` | USB library (copy into same folder as sunxi-fel.exe) |
| `u-boot-AUTOBOOT.bin` | U-Boot bootloader (auto-boots kernel + DTB) |
| `lametric_515_wifi.uImage` | Linux 5.15 kernel with WiFi + USB gadget serial |
| `sun5i-a13-lametric-515-padded.dtb` | Device tree blob (hardware description) |
| `boot_lametric.bat` | One-click boot script |

### Required for daemon and widgets

| File | Purpose |
|------|---------|
| `lametric_daemon.py` | REST API daemon (HTTP server on port 8080) |
| `weather_widget.py` | Weather display widget (Open-Meteo, no API key needed) |

### Optional

| File | Purpose |
|------|---------|
| `zadig.exe` | USB driver installer (only needed for first-time driver setup) |
| `platform-tools\` | Android platform tools with `fastboot.exe` (not needed for autoboot) |
| `sounds\` | 66 WAV sound files extracted from the original firmware |

### Minimal copy command

From the source machine, copy these files to a USB stick or network share:

```
sunxi-fel.exe
u-boot-AUTOBOOT.bin
lametric_515_wifi.uImage
sun5i-a13-lametric-515-padded.dtb
boot_lametric.bat
lametric_daemon.py
weather_widget.py
zadig.exe
```

Also copy the libusb DLL into the same directory as `sunxi-fel.exe`:

```
copy libusb\MinGW64\dll\libusb-1.0.dll .
```

---

## 2. USB Driver Setup (Zadig)

The LaMetric Time's Allwinner A13 SoC exposes a USB device in FEL (firmware-exchange) mode. Windows does not have a built-in driver for this, so you need to install one using Zadig.

### Steps

1. **Put the LaMetric in FEL mode:**
   - Unplug the device from USB
   - Hold the round button on top of the device
   - While holding, plug the USB cable into the PC
   - Release the button after 2 seconds
   - Windows should chime (new USB device detected)

2. **Run `zadig.exe`** (included in the kit, or download from https://zadig.akeo.ie/)

3. **Select the FEL device:**
   - Click **Options > List All Devices**
   - In the dropdown, look for one of:
     - `USB Device(VID_1f3a_PID_efe8)` (most common)
     - `sunxi FEL device`
     - `Unknown Device #...`
   - The Vendor ID should be **1F3A** and Product ID should be **EFE8**

4. **Install the driver:**
   - Set the target driver to **libusbK** (recommended) or **WinUSB**
   - Click **Replace Driver** (or **Install Driver** if no driver is assigned)
   - Wait for the installation to complete

5. **Verify:** Run `sunxi-fel.exe ver` — it should print something like:
   ```
   AWUSBFEX soc=00001625(A13) ...
   ```

> **Note:** You only need to do this once per PC. The driver persists across reboots.

---

## 3. Python + pyserial Installation

Python is needed for the REST API daemon and the weather widget. The device communicates with the PC over USB gadget serial (a virtual COM port).

### Option A: Install Python system-wide (recommended)

1. Download Python 3.10+ from https://www.python.org/downloads/
2. **Check "Add Python to PATH"** during installation
3. Open a command prompt and install pyserial:
   ```
   pip install pyserial
   ```

### Option B: Use the embedded Python (portable, no install)

If a `python-embed` folder is included in the kit:

```
python-embed\python.exe -m pip install pyserial
```

> If pip is not available in the embedded distribution, download `get-pip.py` from https://bootstrap.pypa.io/get-pip.py and run:
> ```
> python-embed\python.exe get-pip.py
> python-embed\python.exe -m pip install pyserial
> ```

### Verify

```
python -c "import serial; print('pyserial OK')"
```

---

## 4. How to Boot the LaMetric (boot_lametric.bat)

### Quick version

1. Put the device in FEL mode (unplug, hold button, plug in, release)
2. Double-click `boot_lametric.bat`
3. Wait — the script handles everything automatically

### What the script does

1. Verifies all required files are present
2. Detects the FEL device (with instructions if not found)
3. Uploads u-boot, kernel, and DTB via `sunxi-fel`
4. Waits for the USB serial (COM) port to appear (~10 seconds)
5. Syncs the PC clock to the device (HH:MM:SS with seconds)
6. Starts the widget carousel (clock + weather + notifications)
7. Offers to launch the REST API daemon

### Manual boot (if the script fails)

```
sunxi-fel.exe uboot u-boot-AUTOBOOT.bin write 0x42000000 lametric_515_wifi.uImage write 0x43000000 sun5i-a13-lametric-515-padded.dtb
```

Then open a serial terminal (PuTTY, Tera Term, or similar) at 115200 baud on the new COM port that appears.

---

## 5. How to Start the REST API Daemon

The daemon bridges HTTP requests to serial commands, exposing a REST API on `http://localhost:8080`. It also runs a background weather fetcher (Open-Meteo API, no key required) and manages the widget carousel.

### Start

```
python lametric_daemon.py
```

The daemon auto-detects the COM port. If it picks the wrong one, set it manually:

```
python lametric_daemon.py --port COM4
```

### Key daemon features

- **Widget carousel** — auto-rotates between clock, weather, and custom widgets
- **Weather integration** — fetches live weather from Open-Meteo API every 5 minutes
- **Push notifications** — display alerts with optional sound and auto-dismiss
- **Configurable location** — set latitude/longitude for weather via the API (no default location is assumed)
- **CORS enabled** — browser-based clients can call the API directly

### API examples

```bash
# Show text on the display
curl -X POST http://localhost:8080/api/v1/display -d "{\"text\": \"HELLO\"}"

# Scroll a long message
curl -X POST http://localhost:8080/api/v1/scroll -d "{\"text\": \"Hello World!\"}"

# Push a notification with sound
curl -X POST http://localhost:8080/api/v1/notification -d "{\"text\": \"Alert!\", \"sound\": \"positive1\", \"duration\": 5}"

# Start clock mode
curl http://localhost:8080/api/v1/clock

# Set brightness (0-255)
curl -X POST http://localhost:8080/api/v1/bright -d "{\"value\": 200}"

# Add a widget to the carousel
curl -X POST http://localhost:8080/api/v1/widget -d "{\"type\": \"clock\"}"

# Set weather location (latitude, longitude)
curl -X POST http://localhost:8080/api/v1/weather -d "{\"latitude\": 40.71, \"longitude\": -74.01}"

# Get current weather data
curl http://localhost:8080/api/v1/weather

# List carousel widgets
curl http://localhost:8080/api/v1/widgets

# Get carousel status
curl http://localhost:8080/api/v1/carousel

# Device info
curl http://localhost:8080/api/v1/info
```

### Python example

```python
import requests

requests.post("http://localhost:8080/api/v1/display", json={"text": "HI"})
requests.post("http://localhost:8080/api/v1/notification", json={
    "text": "Server Down!", "sound": "positive1", "duration": 3
})
requests.post("http://localhost:8080/api/v1/widget", json={"type": "clock"})
```

---

## 6. How to Run the Weather Widget

The weather widget fetches the current temperature from the Open-Meteo API (free, no API key) and displays it with a weather icon. Configure your location by editing the latitude/longitude in the script or by using the daemon's REST API.

### One-shot (display current weather once)

```
python weather_widget.py
```

### Continuous updates (every 5 minutes)

```
python weather_widget.py --loop 300
```

### Specify a different COM port

```
python weather_widget.py --port COM4
```

> **Note:** The weather widget talks directly to the serial port, so do not run it at the same time as `lametric_daemon.py` — they will conflict over the COM port. Use the daemon's weather API instead if the daemon is running:
> ```
> curl -X POST http://localhost:8080/api/v1/weather -d "{\"latitude\": YOUR_LAT, \"longitude\": YOUR_LON}"
> ```

---

## 7. Troubleshooting

### "sunxi-fel.exe" is not recognized / fails silently

- Make sure `libusb-1.0.dll` is in the same folder as `sunxi-fel.exe`
- If you get a DLL error, copy the DLL from `libusb\MinGW64\dll\libusb-1.0.dll`

### FEL device not detected

- **Wrong driver:** Open Device Manager, find the device under "Universal Serial Bus devices" or "Other devices." If it says anything other than libusbK/WinUSB, re-run Zadig.
- **Wrong mode:** The device must be in FEL mode. Unplug, hold the button, plug in, release after 2 seconds.
- **USB cable:** Some cables are charge-only (no data lines). Try a different cable.
- **USB port:** Try a USB 2.0 port instead of USB 3.0. Some USB 3.0 controllers have compatibility issues with the A13's FEL mode.
- **Hub:** Plug directly into the PC — avoid USB hubs.

### FEL upload succeeds but no COM port appears

- The kernel takes about 10-15 seconds to boot and start the USB gadget serial.
- Check Device Manager for "Ports (COM & LPT)" — a new "USB Serial Device" should appear.
- If you see an unknown device instead, Windows may need the USB CDC ACM driver. On Windows 10/11 this is usually automatic. Try unplugging and replugging.
- If the COM port appears and disappears, the kernel may be crashing. Check with a UART adapter on the debug pads (115200 baud, 3.3V).

### COM port detected but commands fail

- Close any other program using the same COM port (PuTTY, Tera Term, Arduino IDE, etc.)
- Try a different baud rate in the daemon: `python lametric_daemon.py --baud 115200`
- Power-cycle the device and re-run the boot script.

### "pip install pyserial" fails

- Make sure Python is in your PATH: `python --version`
- On some systems, use `py -m pip install pyserial` or `python3 -m pip install pyserial`
- Behind a corporate proxy: `pip install --proxy http://proxy:port pyserial`

### Display is garbled or shows wrong characters

- The display uses SPI with a specific byte order. If the kernel or DTB is wrong, the display will be garbled.
- Make sure you are using the matching DTB file (`sun5i-a13-lametric-515-padded.dtb`) with the kernel.

### Device boots but WiFi does not work

- WiFi needs the RTL8723BU driver, which is built into the `lametric_515_wifi.uImage` kernel.
- The WiFi chip may need firmware blobs. These are baked into the initramfs.
- Run `ip link` over the serial console to check if `wlan0` is present.

### Daemon port 8080 is already in use

- Another program is using port 8080. Either stop it, or edit `HTTP_PORT` in `lametric_daemon.py`.
- On Windows: `netstat -ano | findstr :8080` will show which process is using the port.

---

## Architecture Overview

```
  [Windows PC]                    [LaMetric Time]
  +-------------------+           +-------------------+
  |                   |   USB     |  Allwinner A13    |
  | sunxi-fel.exe  ------FEL---->|  (FEL mode)       |
  |                   |           |                   |
  | boot_lametric.bat |           |  u-boot           |
  |                   |           |    -> kernel       |
  | lametric_daemon.py|   USB     |    -> initramfs   |
  |   :8080 HTTP   ------CDC---->|  /dev/ttyGS0      |
  |                   |  serial   |  (shell_init)     |
  | weather_widget.py |           |                   |
  +-------------------+           +-------------------+
```

1. `sunxi-fel.exe` pushes u-boot + kernel + DTB over USB FEL protocol
2. U-Boot starts, loads kernel + DTB from the addresses FEL wrote to RAM
3. Kernel boots, starts the init process (shell_init) from the built-in initramfs
4. Init enables USB gadget serial — a COM port appears on the PC
5. The PC sends text commands over the COM port to control the display, carousel, etc.
6. `lametric_daemon.py` wraps this in an HTTP REST API on port 8080 with weather and carousel management
