# SPDX-License-Identifier: MIT
CROSS_COMPILE ?= aarch64-linux-gnu-
CC := $(CROSS_COMPILE)gcc
OUT ?= $(abspath ../../../output/userspace/wcn)
PYTHON ?= python3
CPPFLAGS ?=
CFLAGS ?= -std=gnu11 -O2 -Wall -Wextra -Werror
LDFLAGS ?= -static -s
FIRMWARE ?= ../firmware

.PHONY: all test install install-gnss
all: $(OUT)/w202-bt-init $(OUT)/w202ds-gnss

$(OUT)/w202-bt-init: bt-init/w202-bt-init.c
	mkdir -p "$(OUT)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LDFLAGS) -o "$@"

test: all
	$(PYTHON) tests/test-init.py "$(OUT)/w202-bt-init" "$(FIRMWARE)"

# Stage only: no module loads, service changes or device access.
install: all install-gnss
	test -n "$(DESTDIR)" && test "$(DESTDIR)" != /
	mkdir -p "$(DESTDIR)"
	cp -a rootfs/. "$(DESTDIR)/"
	install -D -m 0755 "$(OUT)/w202-bt-init" "$(DESTDIR)/usr/libexec/w202-bt-init"
	for name in gnssmodem.bin wcnmodem.bin wifi_board_config.ini wifi_board_config_ab.ini bt_configure_pskey.ini bt_configure_rf.ini; do \
	  install -D -m 0644 "$(FIRMWARE)/$$name" "$(DESTDIR)/usr/lib/firmware/$$name" || exit; \
	done

# Optional standalone NMEA tool; no GNSS power request or daemon activation.
install-gnss: gnss
	test -n "$(DESTDIR)" && test "$(DESTDIR)" != /
	install -D -m 0755 "$(OUT)/w202ds-gnss" "$(DESTDIR)/usr/libexec/w202ds-gnss"

.PHONY: gnss
gnss: $(OUT)/w202ds-gnss
$(OUT)/w202ds-gnss: gnss/w202ds-gnss.c ../../userspace-common/source/native.h Makefile
	mkdir -p "$(OUT)"
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wno-unused-function -Wno-misleading-indentation -I../../userspace-common/source $< $(GNSS_LDFLAGS) -o "$@"
