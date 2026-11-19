# SPDX-License-Identifier: GPL-2.0-only
.DEFAULT_GOAL := help
OUT ?= $(abspath ../../../output/userspace/audio)
AUDIO_FIRMWARE ?= $(OUT)/firmware
PYTHON ?= python3
AUDIO_AUDCP ?= ../firmware/stock-partitions/l_agdsp_a.img
AUDIO_ACF ?= ../firmware/vendor/aw883xx_acf.bin
AUDIO_DSP_XML ?= ../firmware/vendor/dsp_vbc.xml
.PHONY: help install prepare-firmware
help:
	@printf '%s\n' 'Audio: install DESTDIR=/target/staging (no module loads or service activation)'
prepare-firmware:
	$(PYTHON) ../firmware/prepare.py --audcp "$(AUDIO_AUDCP)" --acf "$(AUDIO_ACF)" --dsp-vbc-xml "$(AUDIO_DSP_XML)" --param-id 75 --out "$(AUDIO_FIRMWARE)"
install: prepare-firmware
	test -n "$(DESTDIR)" && test "$(DESTDIR)" != /
	install -D -m 0755 rootfs/usr/libexec/w202-audio-start "$(DESTDIR)/usr/libexec/w202-audio-start"
	install -D -m 0644 rootfs/usr/lib/systemd/system/w202-audio.service "$(DESTDIR)/usr/lib/systemd/system/w202-audio.service"
	install -D -m 0644 rootfs/usr/lib/modprobe.d/w202-audio.conf "$(DESTDIR)/usr/lib/modprobe.d/w202-audio.conf"
	install -D -m 0644 rootfs/usr/lib/udev/rules.d/89-w202ds-audio.rules "$(DESTDIR)/usr/lib/udev/rules.d/89-w202ds-audio.rules"
	install -D -m 0644 rootfs/usr/share/alsa-card-profile/mixer/profile-sets/w202ds.conf "$(DESTDIR)/usr/share/alsa-card-profile/mixer/profile-sets/w202ds.conf"
	for name in w202ds/agdsp.bin aw88166_acf.bin w202ds/vbc-speaker.bin; do \
	  install -D -m 0644 "$(AUDIO_FIRMWARE)/$$name" "$(DESTDIR)/usr/lib/firmware/$$name" || exit; \
	done
