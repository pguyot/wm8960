# WM8960 driver for RaspberryPi

Pristine WM8960 Linux driver directly taken from Linux source tree.
Based on d2912cb15bdda8ba4a5dd73396ad62641af2f520 commit of [wm8960.c](https://github.com/torvalds/linux/blob/d2912cb15bdda8ba4a5dd73396ad62641af2f520/sound/soc/codecs/wm8960.c) and [wm8960.h](https://github.com/torvalds/linux/blob/d2912cb15bdda8ba4a5dd73396ad62641af2f520/sound/soc/codecs/wm8960.h) files.

Compatible with recent versions of Raspbian (with kernel version > 4.18, e.g. Raspbian Buster).

## Datasheet

WM8960's datasheet can be found here:
https://statics.cirrus.com/pubs/proDatasheet/WM8960_v4.4.pdf

## Installation

Clone source code.
Optionally edit overlay file to suit your hardware.
Compile and install with

    make
    sudo make install

Makefile will automatically edit /boot/config.txt and add/enable if required the following params and overlays:

    dtparam=i2c_arm=on
    dtoverlay=i2s-mmap
    dtparam=i2s=on
    dtoverlay=wm8960

You might want to review changes before rebooting.

After reboot, edit mixer settings with alsamixer. In particular you will probably want to enable switches "Left Output Mixer PCM" and "Right Output Mixer PCM" (cf schema on page 1 of datasheet) and push up the Headphone or Speaker volumes.

## Overlay

wm8960 is our own overlay. It defines an ALSA sound card using built-in simple-sound-card driver and based on WM8960 codec.
It is derived from https://github.com/respeaker/seeed-voicecard/blob/master/seeed-2mic-voicecard-overlay.dts

It defines the following overrides:
- `mclk_frequency` clock frequency is set to 12 MHz. However, the driver does not use the value as the WM8960 is being used in slave mode (RaspberryPi generates the necessary bit clock) and therefore the driver does not need to know the master clock frequency.
- `alsaname` defines the name of the card and defaults to wm8960.

For example, you can change ALSA name with the following line in `/boot/config.txt`:

    dtoverlay=wm8960,alsaname=mycard

## Known limitations

- Some configuration switches are not exposed (e.g. MICBIAS level).
- WM8960 can play sounds at 12 kHz and 24 kHz yet these frequencies are not reported as available. This is a Linux/ALSA limitation and would require additional code. ALSA plug interface will do the necessary conversion to 48 kHz.

