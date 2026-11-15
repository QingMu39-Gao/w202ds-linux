#!/usr/bin/env python3
"""Build the hardware ModemManager adapter with an explicit target SDK."""
import argparse
import os
from pathlib import Path
import subprocess

SOURCE = Path(__file__).resolve().parent
PROJECT = SOURCE.parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cross-file', type=Path, required=True)
    parser.add_argument('--source', type=Path, default=SOURCE/'ModemManager')
    parser.add_argument('--jobs', type=int, default=8)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    if not args.cross_file.is_file():
        parser.error('target SDK cross file does not exist')
    source = args.source.resolve()
    if not (source/'src/plugins/w202ds').is_dir():
        parser.error('source has no W202DS hardware plugin')
    output = PROJECT/'output/userspace/modem'
    build = output/'modemmanager'
    setup = ['meson', 'setup']
    if (build/'meson-private/coredata.dat').is_file():
        setup += ['--reconfigure']
    options = [
        '--prefix=/opt/w202ds/modemmanager', '--libdir=lib',
        '--sysconfdir=/etc', '--buildtype=release', '--wrap-mode=nofallback',
        '--auto-features=disabled', '--cross-file', str(args.cross_file.resolve()),
        '-Dplugin_w202ds=enabled', '-Dudev=true', '-Dudevdir=/usr/lib/udev',
        '-Ddbus_policy_dir=/usr/share/dbus-1/system.d',
        '-Dsystemdsystemunitdir=no',
        '-Dpolkit=strict', '-Dmbim=false', '-Dqmi=false', '-Dqrtr=false',
        '-Dintrospection=false', '-Dvapi=false', '-Dman=false', '-Dgtk_doc=false',
        '-Dtests=false', '-Dexamples=false', '-Dbash_completion=false',
    ]
    subprocess.run([*setup, str(build), str(source), *options], check=True)
    subprocess.run(['meson', 'compile', '-C', str(build), '-j', str(args.jobs)], check=True)
    # Stage only: provisioning and service activation belong to system integration.
    subprocess.run(['meson', 'install', '-C', str(build), '--no-rebuild',
                    '--destdir', str(output/'rootfs')], check=True,
                   env=dict(os.environ))


if __name__ == '__main__':
    main()
