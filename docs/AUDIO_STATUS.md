# Audio Status — WORKING

## Final Status: AUDIO FULLY WORKING

Audio playback on the LaMetric Time is fully functional through the sun4i-codec driver.

## Root Cause

The PA (power amplifier) enable GPIO (PD24) was configured with the wrong polarity in the device tree.

- **Broken:** `GPIO_ACTIVE_LOW` — PA was disabled when the driver thought it was enabled
- **Fixed:** `GPIO_ACTIVE_HIGH` — PA correctly powers on when the GPIO is driven high

The external amplifier on the LaMetric Time is **active-high**, meaning PD24 must be driven HIGH to enable the PA. The original device tree had `GPIO_ACTIVE_LOW`, which inverted the logic and kept the amplifier off.

## Working Audio Methods

All three playback methods now produce audible sound:

1. **aplay (ALSA)** — `aplay /sounds/positive1.wav`
2. **OSS /dev/dsp** — direct writes to `/dev/dsp` (used by `beep`, `rawbeep`, `testtone`)
3. **PIO (GPIO bitbang)** — `gptone` command for codec-free tone generation

## Audio Commands

| Command | Description |
|---------|-------------|
| `beep <freq> <ms>` | Play a tone at given frequency and duration |
| `rawbeep <freq> <ms>` | Low-level tone, bypasses mixer |
| `testtone` | Quick test tone to verify audio |
| `atest` | Audio test with diagnostics |
| `gptone` | Tone via GPIO/PIO method |
| `volume <0-63>` | Set PA volume (63 = max) |
| `codec` | Show codec register status |
| `play <file>` | Play a WAV file |
| `timer <seconds>` | Countdown timer with alarm |
| `audiodebug` | Full debug with register dump |

## Hardware

- **Codec:** sun4i-codec (Allwinner A13 built-in DAC)
- **PA GPIO:** PD24 (active-high)
- **Gain GPIOs:** PD25 (gain1), PD26 (gain0)
- **Audio jack detect:** PG12
- **PA Volume range:** 0-63 via ALSA mixer
