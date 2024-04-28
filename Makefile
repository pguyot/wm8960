# SPDX-License-Identifier: GPL-2.0
KERNELRELEASE ?= $(shell uname -r)

snd-soc-wm8960-objs := wm8960.o
obj-m += snd-soc-wm8960.o
snd-soc-max9759-objs := max9759.o
obj-m += snd-soc-max9759.o
snd-soc-volume-gpio-objs := volume-gpio.o
obj-m += snd-soc-volume-gpio.o
dtbo-y += tagtagtag-sound.dtbo

targets += $(dtbo-y)

# Gracefully supporting the new always-y without cutting off older target with kernel 4.x
ifeq ($(firstword $(subst ., ,$(KERNELRELEASE))),4)
	always := $(dtbo-y)
else
	always-y := $(dtbo-y)
endif

ifeq ($(wildcard /boot/firmware/config.txt),)
    BOOT_CONFIG := /boot/config.txt
else
    BOOT_CONFIG := /boot/firmware/config.txt
endif

all: tagtagtag-mixerd
	make -C /usr/src/linux-headers-$(KERNELRELEASE) M=$(shell pwd) modules

# dtbo rule is no longer available
ifeq ($(firstword $(subst ., ,$(KERNELRELEASE))),6)
all: tagtagtag-sound.dtbo

tagtagtag-sound.dtbo: tagtagtag-sound-overlay-patched.dts
	dtc -I dts -O dtb -o $@ $<

tagtagtag-sound-overlay-patched.dts: tagtagtag-sound-overlay.dts
	sed -e "s|^#define EXTCON_JACK_LINE_OUT 23|/* #define EXTCON_JACK_LINE_OUT 23 */|" $< > $@
endif

clean:
	make -C /usr/src/linux-headers-$(KERNELRELEASE) M=$(shell pwd) clean
	rm -f tagtagtag-mixerd tagtagtag-mixerd-test

install: snd-soc-wm8960.ko snd-soc-max9759.ko snd-soc-volume-gpio.ko tagtagtag-sound.dtbo tagtagtag-mixerd
	if test -e /lib/modules/$(KERNELRELEASE)/kernel/sound/soc/codecs/snd-soc-wm8960.ko.xz; then \
	    echo "Disabling built-in wm8960 driver" ; \
	    mv /lib/modules/$(KERNELRELEASE)/kernel/sound/soc/codecs/snd-soc-wm8960.ko.xz \
	        /lib/modules/$(KERNELRELEASE)/kernel/sound/soc/codecs/snd-soc-wm8960.ko.xz.disabled ; \
	fi
	install -o root -m 644 snd-soc-wm8960.ko /lib/modules/$(KERNELRELEASE)/kernel/sound/soc/codecs/
	install -o root -m 644 snd-soc-max9759.ko /lib/modules/$(KERNELRELEASE)/kernel/sound/soc/codecs/
	install -o root -m 644 snd-soc-volume-gpio.ko /lib/modules/$(KERNELRELEASE)/kernel/sound/soc/codecs/
	depmod -a $(KERNELRELEASE)
	install -o root -m 644 tagtagtag-sound.dtbo /boot/overlays/
	sed $(BOOT_CONFIG) -i -e "s/^#dtparam=i2c_arm=on/dtparam=i2c_arm=on/"
	grep -q -E "^dtparam=i2c_arm=on" $(BOOT_CONFIG) || printf "dtparam=i2c_arm=on\n" >> $(BOOT_CONFIG)
	sed $(BOOT_CONFIG) -i -e "s/^#dtoverlay=i2s-mmap/dtoverlay=i2s-mmap/"
	grep -q -E "^dtoverlay=i2s-mmap" $(BOOT_CONFIG) || printf "dtoverlay=i2s-mmap\n" >> $(BOOT_CONFIG)
	sed $(BOOT_CONFIG) -i -e "s/^#dtparam=i2s=on/dtparam=i2s=on/"
	grep -q -E "^dtparam=i2s=on" $(BOOT_CONFIG) || printf "dtparam=i2s=on\n" >> $(BOOT_CONFIG)
	sed $(BOOT_CONFIG) -i -e "s/^#dtoverlay=tagtagtag-sound/dtoverlay=tagtagtag-sound/"
	grep -q -E "^dtoverlay=tagtagtag-sound" $(BOOT_CONFIG) || printf "dtoverlay=tagtagtag-sound\n" >> $(BOOT_CONFIG)
	install -D -o root -m 644 mixer.conf.default /var/lib/tagtagtag-sound/mixer.conf.default
	test -e /var/lib/tagtagtag-sound/mixer.conf || install -o root -m 644 mixer.conf.default /var/lib/tagtagtag-sound/mixer.conf
	install -o root -m 755 tagtagtag-mixerd /usr/local/sbin/tagtagtag-mixerd
	install -o root -m 644 tagtagtag-mixerd.service /lib/systemd/system/tagtagtag-mixerd.service
	systemctl enable tagtagtag-mixerd

tagtagtag-mixerd : tagtagtag-mixerd.c
	cc -Wall -Werror $< -lasound -lpthread -o $@

tagtagtag-mixerd-test : tagtagtag-mixerd.c
	cc -Wall -Werror -DTEST $< -lasound -lpthread -o $@

test : tagtagtag-mixerd-test
	./tagtagtag-mixerd-test

.PHONY: all clean install
