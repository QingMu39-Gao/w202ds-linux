#!/usr/bin/env python3
"""Run the queue synchronization unit test using a native Mesa build's flags."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, required=True)
args = parser.parse_args()
build = args.build_dir.resolve()
entries = json.loads((build / 'compile_commands.json').read_text())
entry = next(e for e in entries if e['file'].endswith('/jm/panvk_vX_gpu_queue.c')
             and '-DPAN_ARCH=9' in e.get('command', ' '.join(e.get('arguments', []))))
flags = entry.get('arguments') or shlex.split(entry['command'])
command = []
skip = False
for flag in flags:
    if skip:
        skip = False
        continue
    if flag in ('-o', '-MQ', '-MF'):
        skip = True
    elif flag not in ('-c', '-MD', '-DNDEBUG', entry['file']):
        command.append(flag)
source = (Path(entry['directory']) / entry['file']).resolve()
test = Path(__file__).resolve().with_name('queue-sync.c')
output = build / 'panvk-queue-sync-test'
command += [str(test), f'-DPANVK_JM_QUEUE_SOURCE="{source}"',
            '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
            '-o', str(output)]
subprocess.run(command, cwd=entry['directory'], check=True)
subprocess.run([str(output)], check=True)
