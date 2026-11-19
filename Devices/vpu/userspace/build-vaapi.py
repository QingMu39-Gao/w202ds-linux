#!/usr/bin/env python3
"""Build the pinned native V4L2 Request VA-API driver from source."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[2]
PIN = '4f60c51f7841dd74bb61c94a4ad31e630bbdab01'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--host-root', type=Path, required=True)
    parser.add_argument('--proot', type=Path, required=True)
    parser.add_argument('--source', type=Path, default=HERE/'libva-v4l2_request')
    args = parser.parse_args()
    source = args.source.resolve()
    if subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip() != PIN:
        parser.error('VA-API source does not match the supported commit')
    if subprocess.check_output(['git', '-C', str(source), 'status', '--porcelain'], text=True).strip():
        parser.error('VA-API source has local modifications')
    out = (PROJECT/'output/userspace/vpu').resolve()
    copied = out/'vaapi-source'
    if copied.exists():
        shutil.rmtree(copied)
    shutil.copytree(source, copied, ignore=shutil.ignore_patterns('.git'))
    for patch in ('profile', 'layout'):
        subprocess.run(['git', 'apply', '--unidiff-zero',
                        str(HERE/'source'/('libva-v4l2-request-w202-'+patch+'.patch'))],
                       cwd=copied,
                       env=dict(os.environ, GIT_CEILING_DIRECTORIES=str(copied.parent)), check=True)
    sdk = args.sdk.resolve()
    cross = out/'vaapi-arm64.ini'
    libraries = sdk/'usr/lib/aarch64-linux-gnu'
    flags = ['--sysroot='+str(sdk), '-Wl,-rpath-link,'+str(libraries)]
    cross.write_text('\n'.join([
        '[binaries]', "c = '/usr/bin/aarch64-linux-gnu-gcc'", "ar = '/usr/bin/aarch64-linux-gnu-ar'",
        "strip = '/usr/bin/aarch64-linux-gnu-strip'",
        'pkg-config = '+repr(['env', 'PKG_CONFIG_SYSROOT_DIR='+str(sdk),
                            'PKG_CONFIG_LIBDIR='+str(libraries/'pkgconfig')+':'+str(sdk/'usr/share/pkgconfig'),
                            'PKG_CONFIG_PATH=', '/usr/bin/pkg-config']),
        '[host_machine]', "system = 'linux'", "cpu_family = 'aarch64'", "cpu = 'aarch64'", "endian = 'little'",
        '[built-in options]', 'c_args = '+repr(['--sysroot='+str(sdk), '-ffile-prefix-map='+str(copied)+'=.']),
        'c_link_args = '+repr(flags), '',
    ]))
    for directory in ('tmp', 'cache'):
        (out/directory).mkdir(parents=True, exist_ok=True)
    runner = [str(args.proot.resolve()), '-r', str(args.host_root.resolve()), '-b', str(PROJECT), '-b', str(out),
              '-b', str(sdk), '-b', '/dev', '-b', '/proc', '-w', str(out),
              '/usr/bin/env', 'PATH=/usr/bin:/bin', 'TMPDIR='+str(out/'tmp'),
              'XDG_CACHE_HOME='+str(out/'cache'), '/usr/bin/python3', '/usr/bin/meson']
    build = out/'vaapi-build'
    if build.exists():
        shutil.rmtree(build)
    subprocess.run([*runner, 'setup', str(build), str(copied), '--cross-file', str(cross),
                    '--prefix=/usr', '--buildtype=release', '-Dstrip=true',
                    '-Ddriverdir=/usr/lib/aarch64-linux-gnu/dri',
                    '-Dcodec_h264=enabled', '-Dcodec_hevc=enabled',
                    *['-Dcodec_'+codec+'=disabled' for codec in ('mpeg2', 'vp8', 'vp9', 'av1')]], check=True)
    subprocess.run([*runner, 'compile', '-C', str(build), '-j', '4'], check=True)
    subprocess.run([*runner, 'install', '-C', str(build), '--destdir', str(out/'rootfs'), '--no-rebuild'], check=True)
    (out/'vaapi.json').write_text(json.dumps({'source_commit': PIN, 'codecs': ['h264', 'hevc-main8'],
                                            'interface': 'V4L2 Media Request / VA-API'}, indent=2)+'\n')


if __name__ == '__main__':
    main()
