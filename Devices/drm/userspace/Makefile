# SPDX-License-Identifier: GPL-2.0-only
.DEFAULT_GOAL := all
CROSS_COMPILE ?= aarch64-linux-gnu-
CC = $(CROSS_COMPILE)gcc
OUT ?= $(abspath ../../../output/userspace/drm)
CPPFLAGS ?=
CFLAGS ?= -std=gnu11 -O2 -Wall -Wextra -Werror
LDFLAGS ?= -static

.PHONY: all diagnostics install
all: $(OUT)/w202-backlight-attach
$(OUT):
	mkdir -p "$@"
$(OUT)/w202-backlight-attach: source/backlight-attach.c | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LDFLAGS) -o "$@"
$(OUT)/test-input: tests/test-input.c | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LDFLAGS) -o "$@"
diagnostics: $(OUT)/test-input

# Stage native DRM preparation; console/idle/lid policy belongs to integration.
install: all
	test -n "$(DESTDIR)" && test "$(DESTDIR)" != /
	mkdir -p "$(DESTDIR)"
	cp -a rootfs/. "$(DESTDIR)/"
	install -D -m 0755 "$(OUT)/w202-backlight-attach" "$(DESTDIR)/usr/libexec/w202-backlight-attach"
