CROSS_COMPILE ?= aarch64-linux-gnu-
CC = $(CROSS_COMPILE)gcc
CFLAGS = -O2 -Wall -Wextra -Werror -std=gnu11
LDFLAGS = -static
OUT ?= $(abspath ../../../../../output/userspace/hall/diagnostics)

.DEFAULT_GOAL := all

$(OUT):
	mkdir -p "$@"

$(OUT)/w202-hall-events: main.c | $(OUT)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

.PHONY: all
all: $(OUT)/w202-hall-events
