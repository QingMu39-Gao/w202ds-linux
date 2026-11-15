#!/usr/bin/env python3
"""Stage the board ARM64 PanVK ICD and an explicit diagnostic launcher."""
import argparse
import json
from pathlib import Path
import shutil


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path, required=True)
    parser.add_argument('--destdir', type=Path, required=True)
    args = parser.parse_args()
    root = args.destdir.resolve()
    if root == Path('/'):
        parser.error('Use a staging directory, not the running system root')
    header = args.library.read_bytes()[:20]
    if (header[:6] != b'\x7fELF\x02\x01' or
            int.from_bytes(header[18:20], 'little') != 183):
        parser.error('Expected an ELF64 little-endian AArch64 library')
    library = root / 'usr/lib/w202-panvk/libvulkan_panfrost.so'
    library.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(args.library, library)
    library.chmod(0o644)
    config = root / 'usr/share/w202-panvk/icd.json'
    config.parent.mkdir(parents=True, exist_ok=True)
    config.write_text(json.dumps({'file_format_version': '1.0.0', 'ICD': {
        'library_path': '/usr/lib/w202-panvk/libvulkan_panfrost.so',
        'api_version': '1.3.0'}}, indent=2) + '\n')
    public = root / 'usr/share/vulkan/icd.d/w202_panfrost_icd.json'
    public.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(config, public)
    launcher = root / 'usr/bin/w202-panvk-run'
    launcher.parent.mkdir(parents=True, exist_ok=True)
    launcher.write_text("""#!/bin/sh
set -eu
if [ "$#" -eq 0 ]; then
    echo 'Usage: w202-panvk-run <Vulkan command> [arguments...]' >&2
    exit 2
fi
export PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1
export VK_DRIVER_FILES=/usr/share/w202-panvk/icd.json
export VK_ICD_FILENAMES="$VK_DRIVER_FILES"
exec "$@"
""")
    launcher.chmod(0o755)


if __name__ == '__main__':
    main()
