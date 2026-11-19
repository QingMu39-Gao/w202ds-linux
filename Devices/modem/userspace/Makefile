# SPDX-License-Identifier: MIT
CROSS_COMPILE ?= aarch64-linux-gnu-
CC := $(CROSS_COMPILE)gcc
HOSTCC ?= cc
PYTHON ?= python3
OUT ?= $(abspath ../../../output/userspace/modem)
CPPFLAGS ?=
CFLAGS ?= -std=gnu11 -O2 -Wall -Wextra -Werror
LDFLAGS ?= -static
HOSTLDFLAGS ?=

.PHONY: all check install
all: $(OUT)/w202-modem-storage $(OUT)/w202-modem-at $(OUT)/w202-modem-netcheck $(OUT)/w202-android-runtime $(OUT)/runtime-properties.bin

$(OUT):
	mkdir -p "$@"

$(OUT)/w202-modem-at: at/w202-modem-at.c | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LDFLAGS) -o "$@"

$(OUT)/w202-modem-netcheck: at/w202-modem-netcheck.c | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LDFLAGS) -o "$@"

$(OUT)/w202-android-runtime: runtime/w202-android-runtime.c | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LDFLAGS) -o "$@"

$(OUT)/runtime-properties.bin: runtime/make-property-area.py runtime/runtime-properties.json | $(OUT)
	$(PYTHON) $< runtime/runtime-properties.json "$@"

$(OUT)/modem-at-fixture: tests/modem-at-fixture.c at/w202-modem-at.c | $(OUT)
	$(HOSTCC) $(CPPFLAGS) $(CFLAGS) $< $(HOSTLDFLAGS) -o "$@"

check: $(OUT)/modem-at-fixture
	$(PYTHON) -B tests/test_modem_at.py "$(OUT)/modem-at-fixture"
	$(PYTHON) -B tests/test_modem_storage.py

# Stage only. Enabling services and provisioning private inputs belong to integration.
install: all
	test -n "$(DESTDIR)" && test "$(DESTDIR)" != /
	mkdir -p "$(DESTDIR)"
	cp -a rootfs/. "$(DESTDIR)/"
	install -m 0755 rootfs/usr/libexec/w202-modem-runtime "$(DESTDIR)/usr/libexec/w202-modem-runtime"
	install -m 0755 "$(OUT)/w202-modem-storage" "$(DESTDIR)/usr/libexec/w202-modem-storage"
	install -m 0755 rootfs/usr/libexec/w202-cellular "$(DESTDIR)/usr/libexec/w202-cellular"
	install -m 0755 rootfs/usr/local/sbin/w202-cellular "$(DESTDIR)/usr/local/sbin/w202-cellular"
	install -D -m 0755 "$(OUT)/w202-modem-at" "$(DESTDIR)/usr/local/sbin/w202-modem-at"
	install -D -m 0755 "$(OUT)/w202-modem-netcheck" "$(DESTDIR)/usr/local/sbin/w202-modem-netcheck"
	install -D -m 0755 "$(OUT)/w202-android-runtime" "$(DESTDIR)/usr/libexec/w202-android-runtime"
	install -D -m 0644 "$(OUT)/runtime-properties.bin" "$(DESTDIR)/usr/share/w202ds/modem/runtime-properties.bin"
	# Immutable CP firmware is board payload; private NV is provisioned separately.
	install -D -m 0644 "../firmware/stock-partitions/nr_modem_a.img" "$(DESTDIR)/usr/lib/firmware/w202ds/modem/nr_modem_a.img"
	install -D -m 0644 "../firmware/stock-partitions/nr_phy_a.img" "$(DESTDIR)/usr/lib/firmware/w202ds/modem/nr_phy_a.img"
	install -D -m 0644 "../firmware/stock-partitions/pm_sys_a.img" "$(DESTDIR)/usr/lib/firmware/w202ds/modem/pm_sys_a.img"
	install -D -m 0644 "../../sensorhub/firmware/stock-partitions/ch_sys_a.img" "$(DESTDIR)/usr/lib/firmware/w202ds/modem/ch_sys_a.img"
	install -D -m 0644 "../firmware/boot-prepare/vbmeta.bin" "$(DESTDIR)/usr/lib/firmware/w202-diag/vbmeta.bin"
	install -D -m 0644 "../firmware/boot-prepare/vboot-key-hash.bin" "$(DESTDIR)/usr/lib/firmware/w202-diag/vboot-key-hash.bin"

$(OUT)/w202-modem-storage: runtime/w202-modem-storage.c ../../userspace-common/source/native.h Makefile | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wno-unused-function -Wno-misleading-indentation -I../../userspace-common/source $< $(STORAGE_LDFLAGS) -o "$@"
