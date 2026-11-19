# SPDX-License-Identifier: GPL-2.0-only
.DEFAULT_GOAL := help
FIRMWARE ?= ../firmware
.PHONY: help install
help:
	@printf '%s\n' 'install DESTDIR=/target/staging: touch mechanism and mandatory controller firmware'
install:
	test -n "$(DESTDIR)" && test "$(DESTDIR)" != /
	mkdir -p "$(DESTDIR)"
	cp -a rootfs/. "$(DESTDIR)/"
	install -D -m 0644 "$(FIRMWARE)/Himax_firmware_guoxian.bin" "$(DESTDIR)/usr/lib/firmware/Himax_firmware_guoxian.bin"
