# SPDX-License-Identifier: GPL-2.0-only
.DEFAULT_GOAL := all
CROSS_COMPILE ?= aarch64-linux-gnu-
CC = $(CROSS_COMPILE)gcc
HOSTCC ?= cc
OUT ?= $(abspath ../../../output/userspace/vpu)
CPPFLAGS ?=
CFLAGS ?= -std=gnu11 -O2 -Wall -Wextra -Werror
LDFLAGS ?= -static
HOSTLDFLAGS ?=
DRIVER := ../source
CLIENTS := test-codec-api test-dmabuf-userspace w202-vpu-native-idr w202-vpu-native-ip w202-vpu-idr-replay w202-vpu-request-idr w202-vpu-request-ip w202-vpu-request-hevc
NATIVE_CLIENTS := w202-vpu-native-idr w202-vpu-native-ip
HEADERS := $(wildcard source/*.h) $(wildcard $(DRIVER)/*.h)

.PHONY: all check diagnostics install
all: $(OUT)/w202-vpu-ranges-test
diagnostics: $(addprefix $(OUT)/,$(CLIENTS))
check: $(OUT)/native/w202-vpu-ranges-test $(OUT)/native/w202-vpu-params-test
	"$(OUT)/native/w202-vpu-ranges-test"
	"$(OUT)/native/w202-vpu-params-test"

# The removed Bionic worker service must never be installed again.
install:
	test -n "$(DESTDIR)" && test "$(DESTDIR)" != /
	install -d "$(DESTDIR)/etc/systemd/system"
	ln -sfn /dev/null "$(DESTDIR)/etc/systemd/system/w202-vpu.service"

$(OUT) $(OUT)/native:
	mkdir -p "$@"

$(OUT)/w202-vpu-ranges-test: source/w202-vpu-ranges-test.c $(DRIVER)/w202_vpu_ranges.c $(DRIVER)/w202_vpu_ranges.h | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) source/w202-vpu-ranges-test.c $(DRIVER)/w202_vpu_ranges.c $(LDFLAGS) -o "$@"

$(OUT)/native/w202-vpu-ranges-test: source/w202-vpu-ranges-test.c $(DRIVER)/w202_vpu_ranges.c $(DRIVER)/w202_vpu_ranges.h | $(OUT)/native
	$(HOSTCC) $(CPPFLAGS) $(CFLAGS) source/w202-vpu-ranges-test.c $(DRIVER)/w202_vpu_ranges.c $(HOSTLDFLAGS) -o "$@"

$(OUT)/native/w202-vpu-params-test: source/w202-vpu-params-test.c $(DRIVER)/w202_vpu_h264.c $(DRIVER)/w202_vpu_hevc.c $(HEADERS) | $(OUT)/native
	$(HOSTCC) $(CPPFLAGS) $(CFLAGS) $< $(DRIVER)/w202_vpu_h264.c $(DRIVER)/w202_vpu_hevc.c $(HOSTLDFLAGS) -o "$@"

# Native clients reuse the driver's pure parameter builder.
$(addprefix $(OUT)/,$(NATIVE_CLIENTS)): $(OUT)/%: source/%.c $(DRIVER)/w202_vpu_h264.c $(HEADERS) | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(DRIVER)/w202_vpu_h264.c $(LDFLAGS) -o "$@"

$(addprefix $(OUT)/,$(filter-out $(NATIVE_CLIENTS),$(CLIENTS))): $(OUT)/%: source/%.c $(HEADERS) | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LDFLAGS) -o "$@"
