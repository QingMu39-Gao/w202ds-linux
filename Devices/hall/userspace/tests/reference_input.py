#!/usr/bin/python3 -I
"""Map raw Hall state to a standard input switch with integrator-selected timing."""
import argparse
import fcntl
import json
import os
import select
import signal
import socket
import struct
import sys
import time


class Debounce:
    def __init__(self, delay):
        self.delay = delay
        self.deadline = None
        self.state = False

    def update(self, active, now):
        if not active:
            self.deadline = None
            self.state = False
        elif not self.state:
            if self.deadline is None:
                self.deadline = now + self.delay
            if now >= self.deadline:
                self.state = True
                self.deadline = None
        return self.state


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True)
    parser.add_argument('--switch-code', type=int, required=True)
    parser.add_argument('--active-delay-ms', type=int, default=0)
    parser.add_argument('--name', default='Hall switch')
    args = parser.parse_args()
    name = args.name.encode('utf-8')
    if os.geteuid() != 0:
        parser.error('uinput access requires root')
    if not 0 <= args.switch_code <= 16 or not 0 <= args.active_delay_ms <= 60000 or len(name) > 79:
        parser.error('Switch code, delay or device name outside supported bounds')
    running = True

    def stop(*unused):
        nonlocal running
        running = False

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    debounce = Debounce(args.active_delay_ms / 1000)
    event = struct.Struct('llHHi')
    fd = os.open('/dev/uinput', os.O_WRONLY | os.O_NONBLOCK | os.O_CLOEXEC)
    created = False
    try:
        fcntl.ioctl(fd, 0x40045564, 5)  # UI_SET_EVBIT(EV_SW)
        fcntl.ioctl(fd, 0x4004556d, args.switch_code)  # UI_SET_SWBIT
        fcntl.ioctl(fd, 0x405c5503, struct.pack('HHHH80sI', 6, 0, 0, 1, name, 0))  # UI_DEV_SETUP
        fcntl.ioctl(fd, 0x5501)  # UI_DEV_CREATE
        created = True
        notify = os.environ.get('NOTIFY_SOCKET')
        if notify:
            address = '\0' + notify[1:] if notify.startswith('@') else notify
            with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as notifier:
                notifier.connect(address)
                notifier.sendall(b'READY=1')
        with open(args.source, 'r') as source:
            poll = select.poll()
            poll.register(source.fileno(), select.POLLPRI | select.POLLERR)
            last = False
            while running:
                source.seek(0)
                value = source.read().strip()
                if value not in ('0', '1'):
                    raise ValueError('Invalid raw Hall state')
                now = time.monotonic()
                state = debounce.update(value == '1', now)
                if state != last:
                    os.write(fd, event.pack(0, 0, 5, args.switch_code, int(state))
                             + event.pack(0, 0, 0, 0, 0))
                    last = state
                    print(json.dumps({'switch': args.switch_code, 'active': state,
                                      'monotonic': now}), flush=True)
                timeout = 500
                if debounce.deadline is not None:
                    timeout = min(timeout, max(1, int((debounce.deadline - now) * 1000)))
                poll.poll(timeout)
    finally:
        if created:
            fcntl.ioctl(fd, 0x5502)  # UI_DEV_DESTROY
        os.close(fd)


if __name__ == '__main__':
    main()
