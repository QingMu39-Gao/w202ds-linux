# SPDX-License-Identifier: GPL-2.0-only
.DEFAULT_GOAL := help
PYTHON ?= python3
.PHONY: help install check stage all
help:
	@printf '%s\n' 'Charging: install DESTDIR=/target/staging'
install: all
	test -n "$(DESTDIR)" && test "$(DESTDIR)" != /
	install -D -m 0755 rootfs/usr/libexec/w202-charging "$(DESTDIR)/usr/libexec/w202-charging"
	install -D -m 0755 "$(OUT)/w202-power-control" "$(DESTDIR)/usr/libexec/w202-power-control"
	install -D -m 0644 rootfs/usr/lib/systemd/system/w202-charging.service "$(DESTDIR)/usr/lib/systemd/system/w202-charging.service"
	install -D -m 0644 rootfs/usr/lib/systemd/system/w202-charging.timer "$(DESTDIR)/usr/lib/systemd/system/w202-charging.timer"
check:
	$(PYTHON) -B tests/test-policy.py
	$(PYTHON) -B tests/test-controls.py

CC ?= cc
OUT ?= $(abspath ../../../output/userspace/charging)
NATIVE := ../../userspace-common/source
CFLAGS ?= -std=gnu11 -Os -Wall -Wextra -Werror -Wno-unused-function -Wno-misleading-indentation
all: $(OUT)/w202-power-control
$(OUT)/w202-power-control: source/w202-power-control.c $(NATIVE)/native.h
	mkdir -p "$(OUT)"
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(NATIVE) $< $(LDFLAGS) -o "$@"
stage: all
	install -D -m 0755 "$(OUT)/w202-power-control" "$(OUT)/rootfs/usr/libexec/w202-power-control"
