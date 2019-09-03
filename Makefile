# SPDX-License-Identifier: GPL-2.0
snd-soc-wm8960-objs := wm8960.o
obj-m += snd-soc-wm8960.o
snd-soc-max9759-objs := max9759.o
obj-m += snd-soc-max9759.o
dtbo-y += tagtagtag-sound.dtbo

targets += $(dtbo-y)    
always  := $(dtbo-y)

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

install: snd-soc-wm8960.ko snd-soc-max9759.ko tagtagtag-sound.dtbo
	cp snd-soc-wm8960.ko /lib/modules/$(shell uname -r)/kernel/sound/soc/codecs/
	cp snd-soc-max9759.ko /lib/modules/$(shell uname -r)/kernel/sound/soc/codecs/
	depmod -a
	cp tagtagtag-sound.dtbo /boot/overlays/
	sed /boot/config.txt -i -e "s/^#dtparam=i2c_arm=on/dtparam=i2c_arm=on/"
	grep -q -E "^dtparam=i2c_arm=on" /boot/config.txt || printf "dtparam=i2c_arm=on\n" >> /boot/config.txt
	sed /boot/config.txt -i -e "s/^#dtoverlay=i2s-mmap/dtoverlay=i2s-mmap/"
	grep -q -E "^dtoverlay=i2s-mmap" /boot/config.txt || printf "dtoverlay=i2s-mmap\n" >> /boot/config.txt
	sed /boot/config.txt -i -e "s/^#dtparam=i2s=on/dtparam=i2s=on/"
	grep -q -E "^dtparam=i2s=on" /boot/config.txt || printf "dtparam=i2s=on\n" >> /boot/config.txt
	sed /boot/config.txt -i -e "s/^#dtoverlay=tagtagtag-sound/dtoverlay=tagtagtag-sound/"
	grep -q -E "^dtoverlay=tagtagtag-sound" /boot/config.txt || printf "dtoverlay=tagtagtag-sound\n" >> /boot/config.txt

.PHONY: all clean install
