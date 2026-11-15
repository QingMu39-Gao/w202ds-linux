#!/usr/bin/env python3
"""Build native shader tools, the target ICD and its hardware runtime tree."""
import argparse
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=HERE.parent/'mesa/panvk-mali-jm')
    parser.add_argument('--cross-file', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=8)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    if not args.cross_file.is_file() or not (args.source/'.git').exists():
        parser.error('Prepare the target SDK and pinned source checkout first')
    patch = HERE/'patches/0001-restore-panfrost-drm-jm.patch'
    applied = subprocess.run(['git', '-C', str(args.source), 'apply', '--reverse',
                              '--check', str(patch)], capture_output=True).returncode == 0
    base = [sys.executable, str(HERE/'build.py'), '--source', str(args.source),
            '--jobs', str(args.jobs)]
    subprocess.run([*base, '--host-tools', *([] if applied else ['--apply'])], check=True)
    subprocess.run([*base, '--cross-file', str(args.cross_file)], check=True)
    subprocess.run([sys.executable, str(HERE/'install.py'), '--library',
                    str(PROJECT/'output/userspace/gpu/arm64/src/panfrost/vulkan/libvulkan_panfrost.so'),
                    '--destdir', str(PROJECT/'output/userspace/gpu/rootfs')], check=True)


if __name__ == '__main__':
    main()
