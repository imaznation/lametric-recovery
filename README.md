# LaMetric Time Recovery

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Buy Me a Coffee](https://img.shields.io/badge/Buy%20Me%20a%20Coffee-support-yellow?logo=buy-me-a-coffee)](https://buymeacoffee.com/imaznation)


**Bring a bricked LaMetric Time back to life via USB — no eMMC required.**

A complete recovery system for the LaMetric Time smart clock when the internal storage (micro SD / eMMC) dies. The device boots entirely from RAM over USB using the Allwinner FEL protocol, running a custom Linux 5.15 kernel with full display control, interactive buttons, color icons, scrolling text, a REST API, and more.



## The Story

I bought a LaMetric Time a few years back and loved it — it sat on my desk showing the time, weather, and notifications. Then life got busy, I unplugged it, and it sat in a drawer for years.

When I finally plugged it back in... nothing. Completely dead. No lights, no boot, no response. After some research, I realized the internal micro SD card had died — NAND flash loses its charge when left unpowered for extended periods, and years in a drawer was enough to corrupt it beyond recovery.

Most people would throw it away. I decided to see if I could bring it back to life.

### Down the Rabbit Hole

The first breakthrough was discovering that with a dead storage device, the Allwinner A13 processor falls into **USB FEL mode** — a bare-metal recovery protocol baked into the chip's ROM. This meant I could talk to the processor directly over USB and load code into RAM.

What followed was one of the deepest embedded reverse-engineering projects I've ever done:

**Week 1: Getting a heartbeat.** I started with nothing — no documentation for the LaMetric's hardware, no schematics, no source code. Online resources incorrectly identified the SoC as an R16/A33, but FEL told me it was actually an **Allwinner A13 (sun5i)**. I built a minimal u-boot from scratch, discovering that the stock video and LED drivers crash on LaMetric hardware and had to be disabled. After days of trial and error, I got a Linux 5.15 kernel booting from RAM — but with no display, no output, and no way to know if it was even running.

**Week 2: First pixels.** The display is driven by an STM32 microcontroller that sits between the A13 and the LED matrix. I had to reverse-engineer the entire communication protocol — disassembling both the Linux kernel driver and the STM32 firmware to understand the SPI frame format, I2C commands, and the critical PE4 GPIO that switches between animation mode and direct pixel control. The moment I saw my first "HELLO" appear on the 37x8 LED matrix, I knew this was going to work.

The display mapping alone was a puzzle. The 488-byte SPI frame has three physical sections with different byte layouts, including "row-shifted" columns at section boundaries where frame row R maps to physical row R+1. It took dozens of checkerboard tests and ASCII dumps to map all 37 columns correctly. The breakthrough: S1 starts at byte 0 (not byte 4 as I initially assumed), and the [B,G,R,W] channel order was backwards from what the documentation suggested.

**Week 3: Sound and fury.** Audio was the hardest challenge. The A13 has an internal codec with a built-in amplifier, and I could see all the registers being set correctly — DAC enabled, mixer routing configured, PA volume at max. But no sound came out. I spent days trying every possible register combination, every DAPM switch permutation, even toggling the PA GPIO at audio frequencies to bypass the codec entirely.

The root cause turned out to be **a single bit in the device tree**: the PA enable GPIO (PD24) was configured as `GPIO_ACTIVE_LOW` when the hardware is actually `GPIO_ACTIVE_HIGH`. This meant every time the kernel tried to turn the speaker ON, it was turning it OFF, and vice versa. One character change in the DTS fixed everything. I'll never forget the moment I heard that first beep.

**Week 4: Making it a product.** With display, audio, and buttons all working, I built a full app framework — a 6-app launcher with clock, weather, business metrics, timer, notifications, and a sound browser. Added auto-brightness from the onboard light sensor, smooth text scrolling with bounce animation, 27 hand-crafted RGBW pixel art icons, and a REST API daemon that auto-fetches weather and accepts push notifications from any webhook. The whole system boots from RAM in under 10 seconds, plays a startup chime, and launches straight into the app carousel.

### What I Learned

This project touched nearly every layer of the embedded stack — from bare-metal register poking and ARM disassembly, through kernel driver development and device tree configuration, to userspace app development and REST API design. Some key discoveries:

- **PE4 is the mode switch**: HIGH = accept SPI pixel data, LOW = play stored animation. This was the final key to getting display control working. It was never documented anywhere.
- **The STM32 is always alive**: Even with a dead SD card, the display controller runs its own firmware from internal flash and responds to I2C/SPI. This is why the LaMetric briefly flashes its boot animation even when bricked.
- **S1 RGBW starts at byte 0**: The STM32 reads the first section from the very beginning of the 488-byte frame, not offset by 4 bytes as the kernel driver implied.
- **PA polarity matters**: One wrong GPIO flag = completely silent audio. The original firmware's BSP driver handled this differently from mainline Linux.
- **USB re-enumeration is fragile**: When the device transitions from FEL mode to kernel USB gadget, Windows can get confused by phantom device entries. Cleaning stale PnP devices fixes it.

### The Result

What started as "can I get a single LED to light up?" became a fully-featured smart display with 6 apps, audio notifications, auto-brightness, a REST API, and a PC daemon that fetches live weather. All running from RAM, booted over USB, on a device with completely dead storage.

The LaMetric Time is back on my desk where it belongs.


---

## Features

### Display
- **Pixel-perfect 37x8 LED display** — 8 RGBW color columns + 29 white columns, fully mapped
- **27 color RGBW icons** — weather (sun, cloud, rain, snow, thunder), status (check, X, warning, info, heart), arrows, trends, clock, music, mail, bell, chart, star, wifi, battery, home, fire, smile
- **Full font coverage** — uppercase + lowercase 5x7 pixel font with digits and symbols
- **Horizontal text scrolling** — smooth pixel-level scroll across all 37 columns
- **Auto-brightness** — JSA1127 ambient light sensor with lux-to-brightness mapping
- **Buzzing fix** — SPI rate reduction eliminates audible buzzing from LED drivers

### Widgets and Carousel
- **Widget carousel** — auto-rotating display of clock, weather, and notifications with configurable interval
- **Clock widget** — persistent HH:MM display with blinking colon and clock icon on the RGBW panel
- **Weather widget** — fetches live weather from Open-Meteo API (no API key required), displays icon + temperature
- **Push notifications** — display alerts with optional icon, sound, and auto-dismiss

### Interaction
- **Interactive button controls** — Left (`<`), Action (`O`), Right (`>`) + Volume buttons for carousel navigation
- **REST API daemon** — HTTP server on the PC for programmatic display control, weather integration, and carousel management
- **Full serial command set** — 40+ commands for display, audio, sensors, diagnostics, and system control

### Hardware
- **Audio fully working** — sun4i-codec with DAC, internal PA, and external amplifier. PA polarity fix: GPIO_ACTIVE_HIGH on PD24 (the PA enable is active-high, not active-low). Supports aplay, OSS /dev/dsp, and PIO playback.
- **WiFi hardware detected** — RTL8723BU interface enumerated (`wlan0`)
- **66 original sound files** — extracted from firmware (notifications, alarms, system sounds)

---

## How It Works

```
 Windows PC                    USB Cable                  LaMetric Time
+-----------+                +-----------+              +----------------+
| sunxi-fel | ---FEL-USB---> | Allwinner |              | STM32 Display  |
| loads     |                | A13 SoC   | ---SPI2----> | Controller     |
| u-boot +  |                |           | ---I2C2----> | (brightness)   |
| kernel +  |  USB CDC ACM   |  Linux    |              | MY9163/TLC5929 |
| initramfs | <--serial----> |  5.15     |              | LED Drivers    |
+-----------+                +-----------+              +----------------+
```

1. **Dead eMMC detected** — device enters USB FEL mode automatically on power-up
2. **sunxi-fel loads u-boot** — custom A13-minimal build (video/LED drivers disabled to prevent crash)
3. **u-boot loads Linux 5.15** — mainline kernel with custom device tree for LaMetric hardware
4. **Custom init provides serial console** — USB CDC ACM gadget creates a virtual COM port on the PC
5. **SPI pixel frames to STM32** — 488-byte frames at 12 MHz: header `0x04` + pixel data with PE4 frame sync
6. **I2C for display control** — STM32 at address `0x21` for brightness, animation, scan frequency
7. **I2C for light sensor** — JSA1127 at address `0x29` for ambient light readings

### Display Architecture

The LaMetric Time display is not directly driven by the A13 SoC. Instead:

- **A13** sends 488-byte SPI frames to an **STM32F030** microcontroller
- **STM32** reformats the data and drives **MY9163** or **TLC5929** LED driver ICs
- **Brightness** is controlled via I2C commands to the STM32 (register `0x20` for white, `0x11-0x13` for RGB)
- **PE4 GPIO** acts as a mode switch: HIGH = SPI data mode, LOW = stored animation mode

---

## Quick Start

### Prerequisites

- **Windows PC** with a USB port (WSL2 recommended for building)
- **ARM cross-compiler**: `arm-linux-gnueabihf-gcc` (in WSL2: `sudo apt install gcc-arm-linux-gnueabihf`)
- **sunxi-fel**: included in the repo (`sunxi-fel.exe` for Windows)
- **Zadig**: for installing WinUSB drivers (included)
- **Python 3** with `pyserial`: `pip install pyserial`

### USB Driver Setup

1. Plug in the bricked LaMetric Time via USB (it enters FEL mode automatically)
2. Run `zadig.exe`
3. Select the device with VID `1F3A`, PID `EFE8`
4. Install **WinUSB** driver
5. After boot, a second device appears (VID `1F3A`, PID `1010`) — install WinUSB for this too

### Build

```bash
# In WSL2:
cd /mnt/c/path/to/lametric-recovery

# Build the initramfs with shell_init
./make_initramfs_v8.sh

# Build the kernel + DTB + initramfs into a bootable image
./build_final_kernel.sh
```

### Boot Sequence

```bash
# Step 1: Load u-boot + kernel + DTB via FEL (from Windows cmd or PowerShell)
sunxi-fel.exe uboot u-boot-AUTOBOOT.bin write 0x42000000 lametric_515_wifi.uImage write 0x43000000 sun5i-a13-lametric-515-padded.dtb

# Step 2: Wait ~10 seconds for kernel boot
# A new COM port appears (USB CDC ACM serial gadget)

# Step 3: Connect via serial terminal (PuTTY, screen, or minicom)
#   Baud: 115200, Port: COM3 (check Device Manager)
```

### First Commands

Once connected to the serial console:

```
info              # Show device info, kernel version, uptime
time 14:30:00     # Set the clock (HH:MM:SS)
carousel          # Start widget carousel (clock + weather + notifications)
text HELLO        # Display static text
scroll Hello World!   # Scroll text across the display
clock             # Start real-time clock display
icon sun          # Show color sun icon on RGBW panel
icontext rain 52F # Weather icon + temperature
color 127 0 0 0   # Set RGB color (red max, green, blue, white)
bright 80         # Set brightness (0-127)
notify bell Alert # Push a notification
luxmon            # Start auto-brightness from light sensor
demo              # Run interactive demo with button controls
beep 800 300      # Play tone (frequency duration_ms)
```

### REST API (PC-side)

```bash
# Start the daemon (auto-detects COM port)
pip install pyserial
python lametric_daemon.py

# Server runs on http://localhost:8080
curl http://localhost:8080/api/v1/info
curl -X POST http://localhost:8080/api/v1/display -d '{"text": "HELLO"}'
curl -X POST http://localhost:8080/api/v1/scroll -d '{"text": "Hello World!"}'
curl http://localhost:8080/api/v1/clock
curl -X POST http://localhost:8080/api/v1/bright -d '{"value": 80}'
curl -X POST http://localhost:8080/api/v1/notification -d '{"text": "Alert!", "sound": "positive1"}'
```

### Weather Widget

Configure your location by editing the latitude/longitude in `weather_widget.py` or `lametric_daemon.py`, or set it at runtime via the REST API.

```bash
# One-shot weather display
python weather_widget.py --port COM3

# Continuous updates every 5 minutes
python weather_widget.py --port COM3 --loop 300

# Or set location via the daemon API
curl -X POST http://localhost:8080/api/v1/weather -d '{"latitude": 40.71, "longitude": -74.01}'
```

---

## Display Pixel Mapping

The 37x8 display is organized into three physical sections with a non-trivial frame layout:

### Frame Structure (488 bytes)

```
Byte Range    Section   Columns    Type         Notes
─────────────────────────────────────────────────────────────────
  0 - 255     S1        0-7        RGBW         4 bytes/pixel (B,G,R,W), stride 32
256 - 259     S2 extra  8-11 row0  White        Row-shifted: row 0 data
260 - 363     S2        8-20       White        13 bytes/row, positions 0-12
364 - 487     S3        21-36      White        16 bytes/row, positions 0-15
```

### Section Layout (left to right as seen on the device)

```
|<-- S1: 8 RGBW cols -->|<-- S2: 13 cols -->|<-- S3: 16 cols -->|
|  col 0 ... col 7      | col 8 ... col 20  | col 21 ... col 36 |
|  4 bytes/pixel (BGRW)  | 1 byte/pixel (W)  | 1 byte/pixel (W)  |
```

### Row-Shifted Extra Columns

S2 and S3 each have "extra" columns at the boundary that are **row-shifted by +1**:

- **S2 extra** (cols 8-11): Frame row R maps to physical row R+1. Row 0 wraps to bytes 256-259.
- **S3 extra** (cols 21-24): Frame row R maps to physical row R+1. Row 0 wraps to bytes 360-363.

### S1 RGBW Pixel Format

```
Byte offset = row * 32 + col * 4
  [offset+0] = Blue
  [offset+1] = Green
  [offset+2] = Red
  [offset+3] = White
```

Key discovery: **S1 starts at byte 0**, not byte 4 as initially assumed. The STM32 reads S1 data beginning at the first byte of the frame.

---

## API Reference

### Serial Console Commands

> Full details for every command are in [COMMANDS.md](COMMANDS.md).

**Display**

| Command | Description |
|---------|-------------|
| `text <string>` | Display static text on white sections (S2+S3) |
| `scroll <string>` | Smooth horizontal pixel-level scroll across all 37 columns |
| `icon <name>` | Show 8x8 RGBW color icon on S1 |
| `icontext <icon> <text>` | Icon on S1 + text on S2+S3 |
| `white` | Fill entire display with white |
| `gradient` | Show brightness gradient pattern |
| `fill <B> <G> <R> <W>` | Fill S1 with RGBW color and white sections with W value |
| `color <R> <G> <B> [W]` | Set LED channel brightness via I2C (0-127) |
| `bright <0-255>` | Set overall white brightness |
| `colormap` | Show sections in different colors (debug) |
| `coltest` | Alternating bright/dim columns (debug) |
| `test <N>` | Light up byte N of 488-byte frame (debug) |
| `sect <1\|2\|3>` | Light up one section only (debug) |
| `range <start> <end>` | Light up byte range in frame (debug) |
| `stride <start> <stride> <count>` | Light up bytes at stride intervals (debug) |
| `multi <b1> <b2> ...` | Light up multiple specific bytes (debug) |
| `checker` | Checkerboard pattern with RGBW coloring (debug) |

**Clock and Time**

| Command | Description |
|---------|-------------|
| `clock` | Start persistent clock (HH:MM with blinking colon and icon) |
| `settime <HH:MM>` | Set device clock (hours, minutes) |
| `time <HH:MM:SS>` | Set device clock with seconds precision |

**Widget Carousel**

| Command | Description |
|---------|-------------|
| `carousel` | Start production carousel (clock + weather + notifications) |
| `weather <icon> <temp>` | Store weather data for carousel display |
| `notify [icon] <text>` | Push a notification into the carousel |
| `dismiss` | Dismiss the current notification |

**Audio**

| Command | Description |
|---------|-------------|
| `beep <freq> <duration_ms>` | Play a tone via the audio codec (working) |
| `rawbeep <freq> <duration_ms>` | Low-level tone bypassing mixer setup (working) |
| `testtone` | Play a test tone to verify audio output |
| `atest` | Audio test with diagnostics |
| `gptone` | Generate a tone using GPIO/PIO method |
| `volume <0-63>` | Set PA volume level (0-63) |
| `codec` | Show audio codec register status |
| `play <file>` | Play a WAV file through the audio codec |
| `timer <seconds>` | Countdown timer with alarm tone |
| `audiodebug` | Full audio debug with register dump |

**Sensors and Buttons**

| Command | Description |
|---------|-------------|
| `lux` | Read ambient light sensor (lux value) |
| `luxmon` | Continuous lux monitoring with auto-brightness |
| `buttons` | Enter button test mode |
| `demo` | Interactive demo with button-driven mode switching |

**Hardware and System**

| Command | Description |
|---------|-------------|
| `info` | Device info: kernel version, uptime, I2C/SPI status |
| `dmesg` | Read kernel log |
| `regdump` | Dump audio codec registers |
| `i2c <reg> <val>` | Raw I2C write to STM32 (address 0x21) |
| `anim` | Trigger STM32 boot animation |
| `stop` | Stop display loop or animation |
| `wifi <on\|off>` | Enable/disable WiFi interface |
| `run <command>` | Execute a shell command |
| `ls [path]` | List directory contents |
| `cat <file>` | Read file contents |
| `reboot` | Reboot the device |

### Available Icons

**Weather:** `sun` `cloud` `rain` `snow` `thunder`
**Status:** `check` `x_mark` `warning` `info` `heart`
**Arrows:** `up` `down` `left` `right` `trend_up` `trend_down`
**Misc:** `clock` `music` `mail` `bell` `chart` `star` `wifi` `battery` `home` `fire` `smile`

### REST API Endpoints (`http://localhost:8080`)

| Method | Endpoint | Body | Description |
|--------|----------|------|-------------|
| `GET` | `/api/v1/info` | — | Device connection info and status |
| `POST` | `/api/v1/display` | `{"text": "hello"}` | Show static text |
| `POST` | `/api/v1/scroll` | `{"text": "long message"}` | Scroll text across the display |
| `POST` | `/api/v1/notification` | `{"text": "Alert!", "icon": "bell", "sound": "positive1", "duration": 5}` | Push notification with optional icon, sound, and auto-dismiss |
| `POST` | `/api/v1/metric` | `{"icon": "chart", "value": "42%", "trend": "up"}` | Display metric with icon and trend arrow |
| `GET` | `/api/v1/clock` | — | Sync time from PC and start clock mode |
| `POST` | `/api/v1/bright` | `{"value": 80}` | Set display brightness (0-255) |
| `POST` | `/api/v1/widget` | `{"type": "clock"}` | Add widget to carousel |
| `GET` | `/api/v1/widgets` | — | List active carousel widgets |
| `DELETE` | `/api/v1/widget/{id}` | — | Remove widget from carousel |
| `GET` | `/api/v1/weather` | — | Get cached weather data |
| `POST` | `/api/v1/weather` | `{"latitude": 40.71, "longitude": -74.01}` | Set weather location (user-configured lat/lon) |
| `GET` | `/api/v1/carousel` | — | Get carousel status and current widget |
| `POST` | `/api/v1/carousel` | `{"interval": 10}` | Configure carousel settings |

**Widget types for carousel:** `clock` `text` `metric` `notification`

**Notification sounds:** `positive1` `positive2` `negative1` `negative2` `alert` `notification`

---

## Hardware

### Device Specifications

| Component | Detail |
|-----------|--------|
| **SoC** | Allwinner A13 (sun5i, single Cortex-A8) |
| **RAM** | 512 MB DDR |
| **Storage** | Micro SD in removable socket (dead in bricked units) |
| **Display** | 37x8 LED matrix: 8 RGBW + 29 white columns |
| **Display MCU** | STM32F030 or GD32F130 (~32KB flash, ~4KB RAM) |
| **LED Drivers** | MY9163 or TLC5929 (auto-detected by firmware variant) |
| **Light Sensor** | JSA1127 on I2C2 @ 0x29 |
| **PMIC** | AXP209 on I2C0 @ 0x34 |
| **WiFi** | RTL8723BU or RTL8723DU (USB, auto-detected) |
| **Audio** | sun4i-codec DAC + internal PA + external amplifier (PD24) |
| **Buttons** | 5 GPIO buttons: Left (PB3), Action (PB2), Right (PB4), VolUp (PG11), VolDn (PG0) |
| **USB** | Single Micro-USB OTG port (MUSB controller) |
| **Original Firmware** | Linux 4.4.13 (Allwinner BSP), firmware v2.3.9 |

### Pin Map

```
PB2  = Action button          PB3  = Left button           PB4  = Right button
PB17 = I2C2 SCL (func 2)      PB18 = I2C2 SDA (func 2)
PD22 = WiFi control            PD23 = Display power
PD24 = Audio PA enable (HIGH)  PD25 = PA gain1              PD26 = PA gain0
PE0  = SPI2 sync GPIO          PE1  = SPI2 CLK (func 4)
PE2  = SPI2 MOSI (func 4)     PE3  = SPI2 CS0 (func 4)
PE4  = SPI2 frame GPIO         PE6  = STM32 RESET           PE7  = STM32 BOOT0
PG0  = Volume Down             PG1  = USB VBUS detect       PG2  = USB ID detect
PG11 = Volume Up               PG12 = Audio jack detect
```

### STM32 Display Protocol

The STM32 acts as an I2C slave (address `0x21`) and SPI slave:

| I2C Command | Description |
|-------------|-------------|
| `0x50, 0x01` | Start display (enables boot animation + SPI DMA) |
| `0x50, 0x00` | Stop animation, restart SPI DMA for direct pixel control |
| `0xB0, 0x01` | Signal SPI buffer ready |
| `0xAF, 0xBC` | Firmware handshake |
| `0x90, N` | Set scan frequency (1-200 Hz) |
| `0x20, N` | White channel brightness (0-127) |
| `0x11, N` | Red channel brightness (0-127) |
| `0x12, N` | Green channel brightness (0-127) |
| `0x13, N` | Blue channel brightness (0-127) |
| `0xAA, 0x01` | Flash write enable |
| `0xAA, 0x00` | Flash write commit |
| `0xC0, N` | Display refresh with mode (1/2/3) |

**SPI Frame Protocol:**
1. CS LOW, send header byte `0x04`, CS HIGH
2. PE4 HIGH (enter SPI data mode)
3. CS LOW, send 488 bytes pixel data, CS HIGH
4. PE4 stays HIGH for continuous data mode (LOW returns to animation mode)

### Original Firmware Architecture

The factory firmware (v2.3.9) runs a sophisticated embedded Linux system:

- **Init:** BusyBox init + supervisord managing 10+ microservices
- **IPC:** D-Bus between all services
- **Audio:** App -> MPD -> PulseAudio -> ALSA (sunxi-codec + loopback for visualization)
- **Display:** Qt4/Embedded WindowsServer with framebuffer, scroll engine, widget system
- **Network:** Dual WiFi (station + AP mode), UPnP/SSDP, SNMP
- **Cloud:** MQTT for push notifications, GPG-signed OTA updates
- **Apps:** opkg package manager for installable apps

**Hidden capabilities discovered in firmware analysis:**
- Spotify Connect (spotifyd daemon built in)
- Bluetooth A2DP sink (receive audio from phone)
- iPhone BLE notification mirroring (ANCS)
- IFTTT integration daemon
- Developer REST API on ports 8080/4343
- Full Python 2.7 interpreter
- USB audio gadget kernel modules
- 66 built-in sound files (notifications, alarms, Alexa demos)

---

## Project Structure

### Core Files

| File | Description |
|------|-------------|
| `shell_init.c` | Main init program: serial shell, display control, buttons, audio, all commands |
| `spi_display.h` | SPI frame builder: 37-column pixel mapping with section layout |
| `font5x7.h` | 5x7 pixel font: uppercase, lowercase, digits, symbols |
| `icons8x8.h` | 27 color icons (8x8 RGBW) with render functions |
| `auto_brightness.c` | JSA1127 ambient light sensor driver with auto-brightness |
| `display_server.c` | U-boot bare-metal display init (I2C bit-bang, GPIO, SPI) |
| `lametric_daemon.py` | PC-side REST API server (HTTP -> serial bridge) |
| `weather_widget.py` | Weather widget: Open-Meteo API -> icon + temperature display |
| `lametric_client.py` | Python serial client library |

### Device Tree & Kernel

| File | Description |
|------|-------------|
| `sun5i-a13-lametric-515.dts` | Custom device tree for Linux 5.15 on LaMetric hardware |
| `mainline-uImage` | Linux 5.15.202 kernel (sun5i, ~5.0 MB) |
| `sun5i-a13-lametric-515.dtb` | Compiled device tree blob |

### Boot Chain

| File | Description |
|------|-------------|
| `u-boot-AUTOBOOT.bin` | U-boot with auto-boot to kernel (FEL -> u-boot -> Linux) |
| `sunxi-fel.exe` | Allwinner FEL protocol tool (Windows) |
| `zadig.exe` | WinUSB driver installer |
| `boot.scr` | U-boot boot script |

### Build Scripts

| File | Description |
|------|-------------|
| `make_initramfs_v8.sh` | Build initramfs with shell_init + static binaries |
| `build_final_kernel.sh` | Compile kernel + DTB + initramfs |
| `build_autoboot_uboot.sh` | Build auto-booting u-boot |

### STM32 Firmware

| File | Description |
|------|-------------|
| `cortex_firmware.bin` | STM32F030 firmware binary (~32 KB) |
| `MY9163_V01.hex` | MY9163 LED driver variant (Intel HEX) |
| `TLC5929V01.hex` | TLC5929 LED driver variant (Intel HEX) |
| `stm32_disasm.txt` | Full Thumb disassembly of STM32 firmware |
| `stm32flash` | STM32 UART bootloader flash tool |

### Documentation

| File | Description |
|------|-------------|
| `COMMANDS.md` | Complete serial command reference (40+ commands) |
| `FIRMWARE_DEEPDIVE.md` | Complete analysis of original firmware v2.3.9 |
| `RECOVERY_NOTES.md` | Detailed recovery engineering notes |
| `display_disasm.txt` | Disassembly of the kernel display driver |

---

## Alternative Fix: SD Card Replacement

If you just want the device working normally with the original firmware:

1. Buy any micro SDHC card (4GB+, ~$5)
2. Open the LaMetric: peel bottom rubber pad, remove 2 Phillips screws, unclip back panel
3. Remove the dead micro SD card from the socket
4. Flash the replacement card with the recovery image
5. Insert, close, power on — device boots normally

The USB FEL recovery approach in this project is for those who want to understand the hardware, build custom firmware, or use the LaMetric as a programmable display without modifying the physical device.

---

## Known Limitations

- **Audio works but volume range is limited** — the mainline sun4i-codec driver uses PA volume 0-63; for maximum output, set `volume 63`
- **WiFi not connected** — hardware is detected but driver integration and network configuration are not complete
- **No persistent storage** — everything runs from RAM; state is lost on power cycle
- **Single USB port** — the same port is used for FEL boot and serial console (no simultaneous data transfer during boot)

---

## Contributing

This is an active reverse-engineering project. Contributions welcome in these areas:

- **Audio enhancements**: Additional sound effects, music playback, USB audio gadget
- **WiFi**: Completing RTL8723BU driver integration and network configuration
- **Bluetooth**: Enabling BT A2DP sink functionality
- **USB Audio Gadget**: Making the device act as a USB speaker
- **Framebuffer**: Implementing `/dev/fb0` for standard Linux display access
- **Additional widgets**: Custom carousel widgets (countdown timer, stock ticker, etc.)

---

## License

This project is released under the [MIT License](LICENSE).

> **Note on sound files:** The WAV files in  are extracted from the original LaMetric Time firmware for interoperability purposes. They are included to restore the device to its intended functionality. If you have licensing concerns, you can delete the  directory and the device will fall back to synthesized tones.

The original LaMetric Time firmware is proprietary and is not distributed with this project. STM32 firmware binaries are extracted from the original device for interoperability purposes only.

---

## Acknowledgments

- **sunxi community** — for the FEL tools, u-boot port, and comprehensive Allwinner documentation
- **Allwinner** — for the A13 SoC with its invaluable FEL recovery mode
- **LaMetric** — for building hardware worth recovering

---

## Support

If this project helped you recover your LaMetric Time, consider buying me a coffee!

[![Buy Me a Coffee](https://img.shields.io/badge/Buy%20Me%20a%20Coffee-support-yellow?logo=buy-me-a-coffee&style=for-the-badge)](https://buymeacoffee.com/imaznation)
