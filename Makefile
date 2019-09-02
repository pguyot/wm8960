# SPDX-License-Identifier: GPL-2.0
snd-soc-wm8960-objs := wm8960.o
obj-m += snd-soc-wm8960.o
dtbo-y += wm8960.dtbo

targets += $(dtbo-y)    
always  := $(dtbo-y)

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

install: snd-soc-wm8960.ko wm8960.dtbo
	cp snd-soc-wm8960.ko /lib/modules/$(shell uname -r)/kernel/sound/soc/codecs/
	depmod -a
	cp wm8960.dtbo /boot/overlays/
	sed /boot/config.txt -i -e "s/^#dtoverlay=wm8960/dtoverlay=wm8960/"
	grep -q -E "^dtoverlay=wm8960" /boot/config.txt || printf "dtoverlay=wm8960\n" >> /boot/config.txt

.PHONY: all clean install
