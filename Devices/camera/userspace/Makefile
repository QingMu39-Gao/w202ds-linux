# SPDX-License-Identifier: GPL-2.0-only
.DEFAULT_GOAL := help
.PHONY: help install
help:
	@printf '%s\n' 'Torch: install DESTDIR=/target/staging (udev template only)'
install: all
	install -D -m 0755 "$(OUT)/w202-torch-control" "$(DESTDIR)/usr/libexec/w202-torch-control"
	test -n "$(DESTDIR)" && test "$(DESTDIR)" != /
	install -D -m 0644 rootfs/usr/lib/udev/rules.d/70-w202-torch.rules "$(DESTDIR)/usr/lib/udev/rules.d/70-w202-torch.rules"

CC ?= cc
OUT ?= $(abspath ../../../output/userspace/camera)
NATIVE := ../../userspace-common/source
CFLAGS ?= -std=gnu11 -Os -Wall -Wextra -Werror -Wno-unused-function -Wno-misleading-indentation
all: $(OUT)/w202-torch-control
$(OUT)/w202-torch-control: source/w202-torch-control.c $(NATIVE)/native.h
	mkdir -p "$(OUT)"
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(NATIVE) $< $(LDFLAGS) -o "$@"
stage: all
	install -D -m 0755 "$(OUT)/w202-torch-control" "$(OUT)/rootfs/usr/libexec/w202-torch-control"
	install -D -m 0644 rootfs/usr/lib/udev/rules.d/70-w202-torch.rules "$(OUT)/rootfs/usr/lib/udev/rules.d/70-w202-torch.rules"
