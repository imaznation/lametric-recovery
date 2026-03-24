# LaMetric Time Recovery — Serial Command Reference

Complete reference for all commands available over the USB serial console (115200 baud).

Connect via PuTTY, Tera Term, or any serial terminal to the COM port that appears after boot.

---

## Display Commands

| Command | Arguments | Description |
|---------|-----------|-------------|
| `text` | `<string>` | Display static text on the white LED sections (S2+S3). Supports uppercase, lowercase, digits, and symbols. |
| `scroll` | `<string>` | Smooth horizontal pixel-level scroll across all 37 columns. Blocks until scroll completes. |
| `icon` | `<name>` | Show an 8x8 RGBW color icon on S1 (the leftmost 8 columns). See icon list below. |
| `icontext` | `<icon> <text>` | Show a color icon on S1 and static text on S2+S3. Example: `icontext sun 72F` |
| `white` | | Fill the entire display with white pixels. |
| `gradient` | | Show a brightness gradient pattern across the display. |
| `fill` | `<B> <G> <R> <W>` | Fill S1 with a specific RGBW color (0-255 each) and white sections with the W value. |
| `color` | `<R> <G> <B> [W]` | Set LED channel brightness via I2C (0-127 each). Controls the STM32 brightness registers. |
| `bright` | `<0-255>` | Set overall white channel brightness via I2C register `0x20`. |

---

## Clock and Time

| Command | Arguments | Description |
|---------|-----------|-------------|
| `clock` | | Start persistent clock display (HH:MM with blinking colon and clock icon on S1). Runs until any key is pressed. Requires time to be set first. |
| `settime` | `HH:MM` | Set the device clock (hours and minutes). Seconds default to 0. |
| `time` | `HH:MM` or `HH:MM:SS` | Set the device clock with optional seconds precision. Preferred over `settime`. |

---

## Widget Carousel

| Command | Arguments | Description |
|---------|-----------|-------------|
| `carousel` | | Start the production widget carousel. Auto-rotates between clock, weather (if set), and notifications. Use left/right buttons to navigate, action button to dismiss notifications. Send `exit` over serial to stop. |
| `weather` | `<icon> <temp>` | Store weather data for the carousel. Typically sent by the PC daemon. Example: `weather sun 72F` |
| `notify` | `[icon] <text>` | Push a notification into the carousel. Displayed on next rotation or immediately if action button is pressed. Example: `notify bell Meeting` |
| `dismiss` | | Dismiss the current notification from the carousel. |

---

## Audio

| Command | Arguments | Description |
|---------|-----------|-------------|
| `beep` | `<freq> <duration_ms>` | Play a tone at the given frequency (Hz) for the given duration (milliseconds). Uses the sun4i-codec DAC. Audio is fully working. Example: `beep 800 300` |
| `rawbeep` | `<freq> <duration_ms>` | Low-level tone generation bypassing mixer setup. Works for quick audio testing. |
| `testtone` | | Play a test tone to verify audio output is working. |
| `atest` | | Audio test with diagnostics — plays a tone and reports codec status. |
| `gptone` | | Generate a tone using GPIO/PIO method (bypasses the codec entirely). |
| `volume` | `<0-63>` | Set PA (power amplifier) volume level. Range 0-63, where 63 is maximum. |
| `codec` | | Show audio codec register status (DAC_DPC, FIFOC, DAC_ACTL, etc.). |
| `play` | `<file>` | Play a WAV file through the audio codec. Example: `play /sounds/positive1.wav` |
| `timer` | `<seconds>` | Start a countdown timer. Displays remaining time and plays an alarm tone when finished. |
| `audiodebug` | | Full audio debug: sets all mixer controls, enables PA GPIOs, opens `/dev/dsp`, plays a test tone, and dumps codec registers during playback. |

---

## Sensors and Brightness

| Command | Arguments | Description |
|---------|-----------|-------------|
| `lux` | | Read the JSA1127 ambient light sensor (I2C address `0x29`) and display the current lux value. |
| `luxmon` | | Start continuous lux monitoring with auto-brightness. Maps lux to display brightness (15-127 range). Updates ~3x per second. Press any key to stop. |

---

## Buttons

| Command | Arguments | Description |
|---------|-----------|-------------|
| `buttons` | | Enter button test mode. Displays which button is pressed: Left (`<`), Action (`O`), Right (`>`), VolUp, VolDn. Press any key on the serial console to exit. |
| `demo` | | Interactive demo with button-driven mode switching. Cycles through clock, hello text, scroll, brightness, and beep modes using the device buttons. |

---

## Hardware and Diagnostics

| Command | Arguments | Description |
|---------|-----------|-------------|
| `info` | | Show device info: kernel version, uptime, I2C status, SPI status. |
| `dmesg` | | Print the kernel log ring buffer. |
| `regdump` | | Dump sun4i audio codec registers (DAC_DPC, FIFOC, FIFOS, ACTL, ADC_ACTL). |
| `i2c` | `<reg> <val>` | Send a raw I2C write to the STM32 display controller (address `0x21`). Values in decimal. Example: `i2c 32 100` (set white brightness to 100). |
| `anim` | | Trigger the STM32 boot animation (sends I2C command `0x50, 0x01`). Sets PE4 LOW (animation mode). |
| `stop` | | Stop any running display loop or animation. Kills the SPI refresh child process if running. |
| `wifi` | `on` or `off` | Enable or disable the WiFi interface (`wlan0`). Hardware must be present (RTL8723BU/DU). |

---

## System

| Command | Arguments | Description |
|---------|-----------|-------------|
| `run` | `<command>` | Execute a shell command and display the output. Uses `/bin/sh` if available, otherwise direct exec. Example: `run ls /proc` |
| `ls` | `[path]` | List directory contents. Defaults to `/` if no path given. |
| `cat` | `<file>` | Read and display file contents. Example: `cat /proc/cpuinfo` |
| `reboot` | | Reboot the device immediately. All state is lost (RAM-only system). |

---

## Display Debugging and Test Commands

These commands are primarily for reverse-engineering and debugging the display protocol.

| Command | Arguments | Description |
|---------|-----------|-------------|
| `colormap` | | Show sections in different colors: S1 = red RGBW, S2 = bright white, S3 = dim white. Useful for identifying section boundaries. |
| `coltest` | | Alternating bright/dim columns in S3, all bright in S2. Tests column ordering. |
| `test` | `<N>` | Light up only byte N of the 488-byte SPI frame. For mapping individual pixels. |
| `sect` | `<1\|2\|3>` | Light up all pixels in only the specified section (1, 2, or 3). |
| `range` | `<start> <end>` | Light up bytes from `start` to `end` in the SPI frame. |
| `stride` | `<start> <stride> <count>` | Light up bytes at positions: start, start+stride, start+2*stride, etc. |
| `multi` | `<b1> <b2> ...` | Light up multiple specific byte positions in the SPI frame. |
| `checker` | | Display a checkerboard pattern on S1 with per-column RGBW coloring. |

---

## Available Icons (27 total)

Use these names with `icon`, `icontext`, `notify`, and `weather` commands.

### Weather
`sun` `cloud` `rain` `snow` `thunder`

### Status
`check` `x_mark` `warning` `info` `heart`

### Arrows
`up` `down` `left` `right` `trend_up` `trend_down`

### Miscellaneous
`clock` `music` `mail` `bell` `chart` `star` `wifi` `battery` `home` `fire` `smile`

---

## Quick Examples

```
# Display text
text HELLO WORLD

# Clock with time set
time 14:30:00
clock

# Weather icon + temperature
icontext sun 72F

# Start production carousel
time 14:30:00
carousel

# Push a notification (works inside carousel)
notify bell Meeting in 5m

# Auto-brightness monitoring
luxmon

# Play a tone (audio fully working)
beep 440 500

# Test audio output
testtone

# Set volume to max
volume 63

# Play a sound file
play /sounds/positive1.wav

# Start a 60-second countdown timer
timer 60

# Set RGBW color on S1
fill 0 0 200 0

# System info
info
dmesg
```

---

## Notes

- **All commands are case-sensitive** and must be lowercase.
- **Time is not persistent** across reboots (RAM-only system). Sync time from the PC after each boot.
- **The carousel** is the recommended production mode. It handles clock display, weather updates, and notifications automatically.
- **Button navigation** works in `carousel`, `demo`, and `buttons` modes. Left/Right navigate, Action selects/dismisses.
- **SPI buzzing fix**: The display refresh rate is tuned to prevent audible buzzing from the LED drivers. If buzzing occurs, it indicates the SPI rate needs adjustment.
- **See also**: `COMMANDS.md` commands can be sent programmatically via the REST API daemon (`lametric_daemon.py`) or directly over the serial port using any serial library.
