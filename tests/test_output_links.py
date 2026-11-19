"""Runtime export follows the project output link without allowing escapes."""
import hashlib
import json
from pathlib import Path
import runpy
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'scripts'))
exporter = runpy.run_path(str(Path(__file__).resolve().parents[1]/'scripts/runtime-export.py'))['export']


class LinkedRuntimeOutput(unittest.TestCase):
    def test_runtime_is_published_under_external_output_and_escape_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            source = base/'source'
            source.mkdir()
            (base/'build-linux').mkdir()
            (source/'output').symlink_to(base/'build-linux', target_is_directory=True)
            build = source/'output/kernel-desktop'
            (build/'arch/arm64/boot').mkdir(parents=True)
            for name in ('modules.order', 'modules.builtin', 'modules.builtin.modinfo', '.config'):
                (build/name).write_text('')
            (build/'arch/arm64/boot/Image').write_bytes(b'kernel')
            module = build/'modules/driver/driver.ko'
            module.parent.mkdir(parents=True)
            module.write_bytes(b'vermagic=test-release\0')
            sha = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
            (module.parent/'build.json').write_text(json.dumps({
                'ok': True, 'kernel_release': 'test-release',
                'module': {'path': module.name, 'sha256': sha(module)},
                'kernel_baseline': {}, 'source_files': {}, 'source': str(source)}))
            verified = {'kdir': build.resolve(), 'root': build.resolve(), 'kernelrelease': 'test-release'}
            with patch.dict(exporter.__globals__, ROOT=source, sha256=sha,
                            verify_output=lambda *a, **kw: verified):
                output = source/'output/runtime-desktop'
                result = exporter(build, output, 'test-board')
                self.assertEqual(result['board_modules'], 1)
                self.assertEqual((base/'build-linux/runtime-desktop/Image').read_bytes(), b'kernel')
                for unsafe in (source/'output', base/'unrelated'):
                    with self.assertRaisesRegex(ValueError, 'subdirectory'):
                        exporter(build, unsafe, 'test-board')


if __name__ == '__main__':
    unittest.main()
