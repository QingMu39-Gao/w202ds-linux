# SPDX-License-Identifier: GPL-2.0-only
.DEFAULT_GOAL := help
CROSS_COMPILE ?= aarch64-linux-gnu-
CC = $(CROSS_COMPILE)gcc
OUT ?= $(abspath ../../../output/userspace/hall)

.PHONY: help diagnostics
help:
	@printf '%s\n' 'Hall: diagnostics OUT=/path/to/output; input mapping and policy belong to the system integrator'

diagnostics:
	$(MAKE) -C tests/gpio-events CC="$(CC)" OUT="$(abspath $(OUT))/diagnostics"


CC ?= cc
OUT ?= $(abspath ../../../output/userspace/hall)
NATIVE := ../../userspace-common/source
CFLAGS ?= -std=gnu11 -Os -Wall -Wextra -Werror -Wno-unused-function -Wno-misleading-indentation
all: $(OUT)/w202-hall-input
$(OUT)/w202-hall-input: source/w202-hall-input.c $(NATIVE)/native.h
	mkdir -p "$(OUT)"
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(NATIVE) $< $(LDFLAGS) -o "$@"
stage: all
	install -D -m 0755 "$(OUT)/w202-hall-input" "$(OUT)/rootfs/usr/libexec/w202-hall-input"

.PHONY: install
install: all
	test -n "$(DESTDIR)" && test "$(DESTDIR)" != /
	install -D -m 0755 "$(OUT)/w202-hall-input" "$(DESTDIR)/usr/libexec/w202-hall-input"
