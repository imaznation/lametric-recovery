# LaMetric Time Firmware Deep Dive
**Version: 2.3.9 (SA1 platform), Kernel 4.4.13SA01.1**

## System Architecture
- BusyBox init + supervisord (10+ microservices)
- D-Bus IPC between all services
- Qt4/Embedded for GUI apps

## Audio Pipeline
App → MPD → PulseAudio → ALSA plug → hw:0,0 (sunxi-codec) + hw:1,0 (loopback)
- S08gain: writes 0 to /sys/devices/platform/sunxi-codec/gain at boot (pop prevention)
- PulseAudio runs as system daemon
- MPD outputs to PulseAudio
- ALSA loopback (snd_aloop) mirrors audio for visualization

## Hidden Features
- **SSH via port knocking**: 7623→6732→8675→6623→1732→8675 (TCP SYN, 5s window)
- **Developer API**: port 8080 (plain HTTP), port 4343 (HTTPS)
- **Spotify Connect**: spotifyd built in
- **Bluetooth A2DP Sink**: receive audio from phone
- **Dual WiFi**: station (wlan0) + AP mode (wlan1, SSID=LMxxxx)
- **UPnP/SSDP**: network discovery
- **SNMP**: network management
- **Python 2.7**: full interpreter
- **USB audio gadget**: modules available (g_audio.ko, usb_f_uac1/2)
- **IFTTT integration**: dedicated daemon
- **iPhone notifications**: BLE ANCS mirroring

## Key Services (supervisord)
| Service | Binary | Purpose |
|---------|--------|---------|
| daemon | 1.9MB | Core: audio, battery, BT, brightness, clock, network, notifications |
| WindowsServer | 1.3MB | Display: framebuffer, scroll, widgets, apps |
| api | 922KB | REST API (FastCGI) |
| btnotifications | 818KB | iPhone BLE notification mirroring |
| push_api | 704KB | Local push notifications |
| iftttd | 615KB | IFTTT integration |
| device_push_handler | 569KB | Cloud MQTT push |
| qclient | 356KB | MQTT cloud client |
| spotifyd | 339KB | Spotify Connect |

## REST API Endpoints (port 80/443)
/api/v1/{info,device,apps,wifi,bluetooth,audio,battery,display,clock,sounds,schedule,screensaver,upgrade,setup,privacy,log,test,i18n,proxy,repo}

## Hardware
- STM32 MCU: 3 variants (GD32F130, STM32F030, TLC5929)
- Light sensor: JSA1127 on I2C2 @0x29
- PMIC: AXP209 on I2C0 @0x34
- WiFi: RTL8723BU or RTL8723DU (auto-detected)
- Display: /dev/fb0 (37×8 pixels via SPI2→STM32→MY9163/TLC5929)

## Cloud/Auth
- MQTT client for push notifications
- OAuth2 credentials hardcoded in firmware
- SSL private key embedded in firmware
- GPG-signed firmware updates

## Sound Files (66 total)
28 notification sounds, 13 alarm tones, 6 system sounds, Alexa demos, ALSA tests

## Partitions
- mmcblk0p1: FAT (firmware updates)
- mmcblk0p7: ext2+LUKS (encrypted user data)
- mmcblk0p9: serial number storage
- Root: squashfs (read-only)
