# SPDX-License-Identifier: GPL-2.0
snd-soc-wm8960-objs := wm8960.o
obj-m += snd-soc-wm8960.o
snd-soc-max9759-objs := max9759.o
obj-m += snd-soc-max9759.o
snd-soc-volume-gpio-objs := volume-gpio.o
obj-m += snd-soc-volume-gpio.o
dtbo-y += tagtagtag-sound.dtbo

targets += $(dtbo-y)    
always  := $(dtbo-y)

all: tagtagtag-mixerd
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f tagtagtag-mixerd tagtagtag-mixerd-test

install: snd-soc-wm8960.ko snd-soc-max9759.ko tagtagtag-sound.dtbo tagtagtag-mixerd
	install -o root -m 644 snd-soc-wm8960.ko /lib/modules/$(shell uname -r)/kernel/sound/soc/codecs/
	install -o root -m 644 snd-soc-max9759.ko /lib/modules/$(shell uname -r)/kernel/sound/soc/codecs/
	install -o root -m 644 snd-soc-volume-gpio.ko /lib/modules/$(shell uname -r)/kernel/sound/soc/codecs/
	depmod -a
	install -o root -m 644 tagtagtag-sound.dtbo /boot/overlays/
	sed /boot/config.txt -i -e "s/^#dtparam=i2c_arm=on/dtparam=i2c_arm=on/"
	grep -q -E "^dtparam=i2c_arm=on" /boot/config.txt || printf "dtparam=i2c_arm=on\n" >> /boot/config.txt
	sed /boot/config.txt -i -e "s/^#dtoverlay=i2s-mmap/dtoverlay=i2s-mmap/"
	grep -q -E "^dtoverlay=i2s-mmap" /boot/config.txt || printf "dtoverlay=i2s-mmap\n" >> /boot/config.txt
	sed /boot/config.txt -i -e "s/^#dtparam=i2s=on/dtparam=i2s=on/"
	grep -q -E "^dtparam=i2s=on" /boot/config.txt || printf "dtparam=i2s=on\n" >> /boot/config.txt
	sed /boot/config.txt -i -e "s/^#dtoverlay=tagtagtag-sound/dtoverlay=tagtagtag-sound/"
	grep -q -E "^dtoverlay=tagtagtag-sound" /boot/config.txt || printf "dtoverlay=tagtagtag-sound\n" >> /boot/config.txt
	install -o root -m 644 mixer.conf.default /var/lib/tagtagtag-sound/mixer.conf.default
	test -e /var/lib/tagtagtag-sound/mixer.conf || install -o root -m 644 mixer.conf.default /var/lib/tagtagtag-sound/mixer.conf
	install -o root -m 755 tagtagtag-mixerd /usr/local/sbin/tagtagtag-mixerd
	install -o root -m 644 tagtagtag-mixerd.service /lib/systemd/system/tagtagtag-mixerd.service
	systemctl enable tagtagtag-mixerd

tagtagtag-mixerd : tagtagtag-mixerd.c
	cc -Wall -Werror -lasound -lpthread $< -o $@

tagtagtag-mixerd-test : tagtagtag-mixerd.c
	cc -Wall -Werror -lasound -lpthread -DTEST $< -o $@

test : tagtagtag-mixerd-test
	./tagtagtag-mixerd-test

.PHONY: all clean install
