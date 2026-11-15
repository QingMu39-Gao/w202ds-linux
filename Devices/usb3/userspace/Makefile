# SPDX-License-Identifier: GPL-2.0-only
.DEFAULT_GOAL := help
PYTHON ?= python3
.PHONY: help install check
help:
	@printf '%s\n' 'USB ACM/NCM: install DESTDIR=/target/staging'

# Stage only. Never bind a host UDC or activate services while building.
install: all
	test -n "$(DESTDIR)" && test "$(DESTDIR)" != /
	install -D -m 0755 "$(OUT)/w202-usb-control" "$(DESTDIR)/usr/libexec/w202-usb-control"
	install -D -m 0755 rootfs/usr/libexec/w202-usb-gadget "$(DESTDIR)/usr/libexec/w202-usb-gadget"
	install -D -m 0755 rootfs/usr/libexec/w202-usb-role "$(DESTDIR)/usr/libexec/w202-usb-role"
	install -D -m 0644 rootfs/usr/lib/systemd/system/w202-usb-gadget.service "$(DESTDIR)/usr/lib/systemd/system/w202-usb-gadget.service"
	install -D -m 0644 rootfs/usr/lib/udev/rules.d/70-w202-usb-role.rules "$(DESTDIR)/usr/lib/udev/rules.d/70-w202-usb-role.rules"

check:
	$(PYTHON) -B tests/test-role.py

CC ?= cc
OUT ?= $(abspath ../../../output/userspace/usb3)
CFLAGS ?= -std=gnu11 -Os -Wall -Wextra -Werror -Wno-unused-function -Wno-misleading-indentation
.PHONY: all stage
all: $(OUT)/w202-usb-control
$(OUT)/w202-usb-control: source/w202-usb-control.c ../../userspace-common/source/native.h
	mkdir -p "$(OUT)"
	$(CC) $(CFLAGS) -I../../userspace-common/source $< $(LDFLAGS) -o "$@"
stage: all
	$(MAKE) install DESTDIR="$(OUT)/rootfs"
	install -D -m 0755 "$(OUT)/w202-usb-control" "$(OUT)/rootfs/usr/libexec/w202-usb-control"
