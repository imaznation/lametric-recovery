# LaMetric Time Recovery — Developer Guide

## What is this?
A complete recovery system for bricked LaMetric Time smart clocks (dead eMMC/SD card). Boots the device entirely from RAM over USB using the Allwinner FEL protocol.

## Architecture
- **SoC**: Allwinner A13 (sun5i), Cortex-A8, 256MB DDR3
- **Display**: STM32 → MY9163/TLC5929 LED drivers, 37x8 pixel matrix (8 RGBW + 29 white)
- **Audio**: sun4i-codec DAC → external amplifier (PD24 enable, PD25/PD26 gain). **PA is ACTIVE_HIGH.**
- **Boot**: USB FEL → u-boot (autoboot) → Linux 5.15 kernel with built-in initramfs
- **Serial**: USB CDC ACM gadget → COM port on PC (115200 baud)

## Key files
| File | Purpose | Lines |
|------|---------|-------|
| `shell_init.c` | Main firmware: 6 apps, 40+ commands, display, audio, buttons, sensors | ~2500 |
| `spi_display.h` | SPI2 display driver with full 37-column pixel mapping | ~150 |
| `font5x7.h` | Complete ASCII font with vertical centering (+1 row offset) | ~110 |
| `icons8x8.h` | 27 RGBW 8x8 pixel art icons with render functions | ~550 |
| `sun5i-a13-lametric-515.dts` | Device tree for LaMetric hardware | ~150 |
| `build_wifi_kernel.sh` | Kernel build script (WiFi, audio, SPI, I2C, USB gadget) | ~80 |
| `lametric_daemon.py` | PC-side REST API daemon with weather, metrics, notifications | ~1100 |
| `boot_lametric.bat` | One-click Windows boot script | ~290 |

## Build
```bash
# In WSL2 with arm-linux-gnueabihf-gcc installed:
./build_wifi_kernel.sh
```

## Flash (correct addresses!)
```bash
sunxi-fel uboot u-boot-AUTOBOOT.bin write 0x42000000 lametric_515_wifi.uImage write 0x43000000 sun5i-a13-lametric-515-padded.dtb
```
**Critical**: kernel at `0x42000000`, DTB at `0x43000000`. The u-boot autoboot command (`bootm 0x42000000 - 0x43000000`) expects these exact addresses.

## Display pixel mapping
- **S1** (cols 0-7): RGBW, offset = `row * 32 + col * 4`, channels [B,G,R,W]
- **S2 extra** (cols 8-11): row-shifted (+1), row 0 at bytes 256-259
- **S2 main** (cols 12-20): `frame[260 + row*13 + (col-12)]`
- **S3 extra** (cols 21-24): row-shifted (+1), row 0 at bytes 360-363
- **S3 main** (cols 25-36): `frame[364 + row*16 + (col-25)]`

## Audio — critical details
- PA GPIO (PD24) is **ACTIVE_HIGH** — `GPIO_ACTIVE_HIGH` in DTS, NOT `GPIO_ACTIVE_LOW`
- This was the root cause of months of silent audio debugging
- All DAPM switches must be enabled: DACPAS, MIXPAS, PA_MUTE, LDACLMIXS, RDACRMIXS, PA_EN
- WAV files are boosted 18x from original firmware levels (hardware amp gain is low)
- `cmd_volume_max()` sets up the full audio path; `audio_ensure_init()` calls it once

## App framework
6 apps cycle with the O button, < > navigates within each app:

| App | O button | < > buttons | Data source |
|-----|----------|-------------|-------------|
| Clock | → Weather | 24h / 12h / uptime / seconds | Local time (synced from PC) |
| Weather | → Metrics | temp / conditions / hi-lo / wind | `weather` serial command or daemon |
| Metrics | → Timer | Cycle through named metrics | `metric` serial command or REST API |
| Timer | → Notifs | Start-stop (>) / reset (<) | `timer` serial command |
| Notifications | → Sounds | Next (>) / dismiss (<) | `notify` serial command or REST API |
| Sounds | → Clock | Play next (>) / play prev (<) | Embedded WAV files in /sounds/ |

## Text rendering
- Font is 5x7 pixels, rendered with +1 row offset for vertical centering on 8-row display
- Auto-scroll: bounce (ping-pong) for text that slightly overflows, wrap for long text
- Scroll speed: 1 pixel per 100ms frame

## Common pitfalls
1. **Wrong FEL addresses**: Must be `0x42000000` (kernel) and `0x43000000` (DTB), not `0x43000000`/`0x43F00000`
2. **PA polarity**: `GPIO_ACTIVE_HIGH`, not `GPIO_ACTIVE_LOW` — inverted polarity = silent audio
3. **S1 byte offset**: Starts at byte 0, not byte 4 — there are no "header bytes" in S1
4. **SPI refresh rate**: Keep ≤200fps to avoid EMI buzzing through the speaker
5. **Kernel size**: Must stay under ~8MB or u-boot's `CONFIG_SYS_BOOTM_LEN` limit is exceeded
6. **USB re-enumeration**: After FEL flash, Windows may need phantom device cleanup for COM port to appear
7. **ALSA config**: Static aplay needs full `pcm.hw` and `ctl.hw` definitions with `@args` in alsa.conf
