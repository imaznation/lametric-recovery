# LaMetric WiFi Implementation Plan

## Hardware
- RTL8723BU USB WiFi module on internal USB hub
- WiFi power: PD22 (active-HIGH, must be HIGH for module to power on)
- USB1 VBUS: PF04 (powers internal hub)

## Kernel Config
Enable: WIRELESS, CFG80211, MAC80211, RTL8XXXU, LEDS_CLASS, FW_LOADER
Disable: BT (bluetooth not needed)

## DTS Additions
```dts
/ {
    wifi_power: regulator-wifi {
        compatible = "regulator-fixed";
        regulator-name = "wifi-power";
        regulator-min-microvolt = <3300000>;
        regulator-max-microvolt = <3300000>;
        gpio = <&pio 3 22 GPIO_ACTIVE_HIGH>;  /* PD22 */
        enable-active-high;
        regulator-always-on;
    };
    reg_usb1_vbus: regulator-usb1-vbus {
        compatible = "regulator-fixed";
        regulator-name = "usb1-vbus";
        regulator-min-microvolt = <5000000>;
        regulator-max-microvolt = <5000000>;
        gpio = <&pio 5 4 GPIO_ACTIVE_HIGH>;  /* PF04 */
        enable-active-high;
        regulator-always-on;
    };
};
&usbphy { usb1_vbus-supply = <&reg_usb1_vbus>; };
```

## Firmware
rtl8723bu_nic.bin → /lib/firmware/rtlwifi/ in initramfs
Or build into kernel with EXTRA_FIRMWARE

## Userspace
- Static wpa_supplicant (~3-5MB)
- Static busybox for udhcpc, ip, ping
- wpa_supplicant.conf with SSID/PSK

## Connect Sequence
1. ip link set wlan0 up
2. wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant.conf
3. udhcpc -i wlan0
