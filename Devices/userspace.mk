# SPDX-License-Identifier: GPL-2.0-only
.DEFAULT_GOAL := help
USERSPACE_DEVICES := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
OUT ?= $(USERSPACE_DEVICES)/../output/userspace
PYTHON ?= python3
HOSTCC ?= cc
COMPONENTS ?= audio camera charging drm hall keyboard modem sensorhub touchscreen usb3 wcn
SUPPORTED := audio camera charging drm hall keyboard modem sensorhub touchscreen usb3 wcn
COMPILED := camera charging drm hall keyboard modem usb3 wcn

ifneq ($(strip $(filter-out $(SUPPORTED),$(COMPONENTS))),)
$(error Unsupported COMPONENTS: $(filter-out $(SUPPORTED),$(COMPONENTS)))
endif

.PHONY: help all install check diagnostics $(addprefix build-,$(SUPPORTED)) $(addprefix install-,$(SUPPORTED))
help:
	@printf '%s\n' \
	  'all: build runtime tools (AArch64 GCC by default)' \
	  'install DESTDIR=/target/staging: stage tools and service templates' \
	  'check: native protocol/storage tests; no device access' \
	  'diagnostics: build explicit DRM, Hall and keyboard diagnostics' \
	  'Select COMPONENTS="audio camera charging drm hall keyboard modem sensorhub touchscreen usb3 wcn" and OUT=/path/to/output' \
	  'Experimental GPU/VPU builds are documented in their component README files.'

all: $(addprefix build-,$(filter $(COMPILED),$(COMPONENTS)))
install: $(addprefix install-,$(COMPONENTS))

$(addprefix build-,$(COMPILED)):
	$(MAKE) -C "$(USERSPACE_DEVICES)/$(@:build-%=%)/userspace" OUT="$(abspath $(OUT))/$(@:build-%=%)" all

$(addprefix install-,$(SUPPORTED)):
	$(MAKE) -C "$(USERSPACE_DEVICES)/$(@:install-%=%)/userspace" OUT="$(abspath $(OUT))/$(@:install-%=%)" DESTDIR="$(if $(DESTDIR),$(abspath $(DESTDIR)),$(abspath $(OUT))/$(@:install-%=%)/rootfs)" install

# Host and target binaries have separate output directories.
check:
	$(MAKE) -C "$(USERSPACE_DEVICES)/charging/userspace" PYTHON="$(PYTHON)" check
	$(MAKE) -C "$(USERSPACE_DEVICES)/usb3/userspace" PYTHON="$(PYTHON)" check
	$(MAKE) -C "$(USERSPACE_DEVICES)/keyboard/userspace" OUT="$(abspath $(OUT))/native/keyboard" HOSTCC="$(HOSTCC)" check
	$(MAKE) -C "$(USERSPACE_DEVICES)/modem/userspace" OUT="$(abspath $(OUT))/native/modem" HOSTCC="$(HOSTCC)" PYTHON="$(PYTHON)" check
	$(MAKE) -C "$(USERSPACE_DEVICES)/wcn/userspace" OUT="$(abspath $(OUT))/native/wcn" CC="$(HOSTCC)" PYTHON="$(PYTHON)" test
	$(PYTHON) -B "$(USERSPACE_DEVICES)/wcn/userspace/gnss/test_protocol.py"

diagnostics:
	$(MAKE) -C "$(USERSPACE_DEVICES)/drm/userspace" OUT="$(abspath $(OUT))/drm" diagnostics
	$(MAKE) -C "$(USERSPACE_DEVICES)/hall/userspace" OUT="$(abspath $(OUT))/hall" diagnostics
	$(MAKE) -C "$(USERSPACE_DEVICES)/keyboard/userspace" OUT="$(abspath $(OUT))/keyboard" diagnostics
