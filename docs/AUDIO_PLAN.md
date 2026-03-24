# LaMetric Audio Implementation Plan
**Based on kernel driver analysis + bare-metal code**

## Step 1: Kernel Config
Remove `--disable SOUND` from build scripts. sunxi_defconfig already has:
- CONFIG_SOUND=y, CONFIG_SND=y, CONFIG_SND_SOC=y, CONFIG_SND_SUN4I_CODEC=y
Also add: `CONFIG_SND_PCM_OSS=y` for easy /dev/dsp access from C

## Step 2: DTS Addition
```dts
&codec {
    allwinner,pa-gpios = <&pio 3 24 GPIO_ACTIVE_LOW>; /* PD24, active-LOW */
    status = "okay";
};
```

## Step 3: PA Gain GPIOs (in init)
```c
// PD25=HIGH, PD26=HIGH for max gain
// GPIO 121 (PD25), GPIO 122 (PD26)
gpio_export(121); gpio_direction(121, "out"); gpio_set(121, 1);
gpio_export(122); gpio_direction(122, "out"); gpio_set(122, 1);
```

## Step 4: Beep via OSS /dev/dsp
```c
int fd = open("/dev/dsp", O_WRONLY);
int fmt = AFMT_S16_LE; ioctl(fd, SNDCTL_DSP_SETFMT, &fmt);
int ch = 1; ioctl(fd, SNDCTL_DSP_CHANNELS, &ch);
int rate = 48000; ioctl(fd, SNDCTL_DSP_SPEED, &rate);
// Generate square wave and write()
```

## Key Facts
- PD24 is ACTIVE-LOW (LOW = PA ON)
- Codec driver auto-manages PA GPIO via DAPM
- 700ms delay on speaker-on for DAC stabilization
- No routing property needed (hardcoded in driver)
- Need /usr/share/alsa/alsa.conf in initramfs if using alsa-lib
