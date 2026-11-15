#!/usr/bin/env python3
"""Build the experimental W202DS PanVK ICD without replacing system Mesa."""
import argparse
import json
from pathlib import Path
import subprocess

DESCRIPTION = Path(__file__).resolve().with_name('sources.json')
PROJECT = Path(__file__).resolve().parents[4]
PATCH = Path(__file__).resolve().parent / 'patches/0001-restore-panfrost-drm-jm.patch'


def run(*args, **kwargs):
    return subprocess.run([str(arg) for arg in args], check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--build-dir', type=Path, help='Default: output/userspace/gpu/{native,arm64}')
    parser.add_argument('--apply', action='store_true', help='Apply the port to a clean pinned checkout')
    parser.add_argument('--cross-file', type=Path)
    parser.add_argument('--native-file', type=Path)
    parser.add_argument('--host-tools', action='store_true', help='Also build the native shader compiler tools')
    parser.add_argument('--jobs', type=int, default=8)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    if args.cross_file and args.host_tools:
        parser.error('--host-tools requires a native build')
    source = args.source.resolve()
    build = (args.build_dir or PROJECT / 'output/userspace/gpu' / ('arm64' if args.cross_file else 'native')).resolve()
    head = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    if head != json.loads(DESCRIPTION.read_text())['revision']:
        parser.error('Source HEAD does not match the pinned community revision')
    if args.apply:
        dirty = subprocess.check_output(['git', '-C', str(source), 'status', '--porcelain'], text=True)
        if dirty:
            parser.error('Apply requires a clean checkout; use a separate source tree')
        run('git', '-C', source, 'apply', '--check', PATCH)
        run('git', '-C', source, 'apply', PATCH)
    # Refuse an unported tree instead of silently producing a kbase ICD.
    run('git', '-C', source, 'apply', '--reverse', '--check', PATCH)
    options = [
        '--prefix=/usr', '--buildtype=release', '--wrap-mode=nofallback',
        '-Dplatforms=wayland', '-Dglx=disabled', '-Degl=disabled',
        '-Dgles1=disabled', '-Dgles2=disabled', '-Dopengl=false',
        '-Dgallium-drivers=[]', '-Dvulkan-drivers=panfrost',
        '-Dpanfrost-kmds=panfrost', '-Dvideo-codecs=[]',
        '-Dbuild-tests=false', '-Dlibunwind=disabled',
    ]
    if args.cross_file:
        options += ['--cross-file', str(args.cross_file.resolve()),
                    '-Dmesa-clc=system', '-Dprecomp-compiler=system', '-Dllvm=disabled']
    if args.native_file:
        options += ['--native-file', str(args.native_file.resolve())]
    if args.host_tools:
        options += ['-Dinstall-mesa-clc=true', '-Dinstall-precomp-compiler=true']
    setup = ['meson', 'setup']
    if (build / 'meson-private/coredata.dat').exists():
        setup += ['--reconfigure']
    run(*setup, build, source, *options)
    targets = ['vulkan_panfrost', 'panfrost_icd', 'panfrost_devenv_icd']
    if args.host_tools:
        targets += ['mesa_clc', 'vtn_bindgen2', 'panfrost_compile']
    run('meson', 'compile', '-C', build, '-j', args.jobs, *targets)


if __name__ == '__main__':
    main()
