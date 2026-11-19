#!/usr/bin/env python3
"""Export a verified kernel and its runtime modules, without distro policy."""
import argparse
import json
from pathlib import Path
import re
import shutil
import tempfile

from kernel_state import ROOT, sha256, verify_output


def export(build, output, board):
    verified = verify_output(build.resolve(), check_freshness=True)
    build = verified['kdir']
    release = verified['kernelrelease']
    output = output.resolve()
    output_root = (ROOT / 'output').resolve()
    if not output.is_relative_to(output_root) or output == output_root:
        raise ValueError('Runtime output must be a subdirectory of project output/')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.runtime-', dir=output.parent) as tmp:
        staging = Path(tmp)
        modules = staging / 'modules' / release
        modules.mkdir(parents=True)
        standard = 0
        for relative in (build / 'modules.order').read_text().splitlines():
            path = Path(relative).with_suffix('.ko')
            if path.is_absolute() or '..' in path.parts:
                raise ValueError('Unsafe modules.order path')
            target = modules / 'kernel' / path
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(build / path, target)
            standard += 1
        receipts = sorted((verified['root'] / 'modules').glob('*/build.json'))
        if not receipts:
            raise ValueError('No board module build receipts')
        names = set()
        for receipt in receipts:
            record = json.loads(receipt.read_text())
            if not record.get('ok') or record['kernel_release'] != release:
                raise ValueError(f'Mismatched module receipt: {receipt}')
            module = receipt.parent / record['module']['path']
            if sha256(module) != record['module']['sha256']:
                raise ValueError(f'Module hash mismatch: {module}')
            for name, expected in record['kernel_baseline'].items():
                if sha256(build / name) != expected:
                    raise ValueError(f'Module built against a different kernel: {module}')
            for name, expected in record['source_files'].items():
                if sha256(Path(record['source']) / name) != expected:
                    raise ValueError(f'Module source changed: {module}')
            for shared, files in record.get('shared_inputs', {}).items():
                for name, expected in files.items():
                    if sha256(Path(shared)/name) != expected:
                        raise ValueError(f'Module shared input changed: {module}')
            if module.name in names:
                raise ValueError(f'Duplicate board module: {module.name}')
            names.add(module.name)
            target = modules / 'updates' / board / module.name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(module, target)
        board_modules = modules/'updates'/board
        (board_modules/'kernel-release').write_text(release+'\n')
        (board_modules/'SHA256SUMS').write_text(''.join(
            sha256(path)+'  '+path.name+'\n' for path in sorted(board_modules.glob('*.ko'))))
        for module in modules.rglob('*.ko'):
            match = re.search(rb'vermagic=([^\0]+)', module.read_bytes())
            if not match or match[1].decode().split()[0] != release:
                raise ValueError(f'Module ABI mismatch: {module}')
        for name in ('modules.builtin', 'modules.builtin.modinfo'):
            shutil.copyfile(build / name, modules / name)
        (modules / 'modules.order').write_text(''.join('kernel/'+str(Path(line).with_suffix('.ko'))+'\n'
                                                    for line in (build/'modules.order').read_text().splitlines()))
        shutil.copyfile(build / 'arch/arm64/boot/Image', staging / 'Image')
        shutil.copyfile(build / '.config', staging / 'config')
        metadata = {'schema': 1, 'board': board, 'release': release,
                    'standard_modules': standard, 'board_modules': len(receipts),
                    'image_sha256': sha256(staging / 'Image'),
                    'config_sha256': sha256(staging / 'config'),
                    'module_hashes': {str(p.relative_to(modules)): sha256(p)
                                      for p in sorted(modules.rglob('*.ko'))}}
        (staging / 'identity.json').write_text(json.dumps(metadata, indent=2) + '\n')
        if output.exists():
            shutil.rmtree(output)
        shutil.move(staging, output)
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', default='desktop')
    args = parser.parse_args()
    if not args.profile.isidentifier():
        parser.error('Invalid profile')
    board = json.loads((ROOT / 'board-userspace.json').read_text())['board']
    result = export(ROOT / 'output' / ('kernel-' + args.profile),
                    ROOT / 'output' / ('runtime-' + args.profile), board)
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
