"""Role requests use TCPM and cannot silently bypass its power policy."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

PROGRAM = Path(__file__).resolve().parents[1] / 'rootfs/usr/libexec/w202-usb-role'


class RoleTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.port = self.root / 'sys/class/typec/port0'
        self.port.mkdir(parents=True)
        for attr, value in {'port_type': 'source sink [dual]', 'data_role': '[device] host',
                            'power_role': '[sink] source'}.items():
            (self.port / attr).write_text(value + '\n')
        self.dwc = self.root / 'sys/class/usb_role/dwc3/role'
        self.dwc.parent.mkdir(parents=True)
        self.dwc.write_text('device\n')
        commands = self.root / 'bin'
        commands.mkdir()
        modprobe = commands / 'modprobe'
        modprobe.write_text('#!/bin/sh\necho "$1" >> "$MODULE_LOG"\n')
        modprobe.chmod(0o755)
        self.env = dict(os.environ, PATH=str(commands) + os.pathsep + os.environ['PATH'],
                        W202_SYSFS_ROOT=str(self.root / 'sys'), MODULE_LOG=str(self.root / 'modules'))

    def run_role(self, *args):
        return subprocess.run(['/bin/sh', str(PROGRAM), *args], env=self.env,
                              capture_output=True, text=True, timeout=5)

    def test_status_has_no_side_effects(self):
        before = {str(p): p.read_bytes() for p in self.root.rglob('*') if p.is_file()}
        result = self.run_role('status')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('port0.power_role=[sink] source', result.stdout)
        self.assertIn('usb_role.dwc3=device', result.stdout)
        self.assertEqual(before, {str(p): p.read_bytes() for p in self.root.rglob('*') if p.is_file()})

    def test_invalid_request_does_not_load_drivers(self):
        self.assertEqual(self.run_role('power', 'host').returncode, 2)
        self.assertFalse((self.root / 'modules').exists())
        self.assertEqual(self.dwc.read_text(), 'device\n')

    def test_port_and_data_requests_do_not_write_dwc_directly(self):
        self.assertEqual(self.run_role('host').returncode, 0)
        self.assertEqual((self.port / 'port_type').read_text(), 'source\n')
        self.assertEqual(self.run_role('data', 'device').returncode, 0)
        self.assertEqual(self.dwc.read_text(), 'device\n')
        self.assertEqual(self.run_role('auto').returncode, 0)
        self.assertEqual((self.port / 'port_type').read_text(), 'dual\n')

    def test_ambiguous_port_does_not_change_either_controller(self):
        second = self.port.parent / 'port1'
        second.mkdir()
        (second / 'port_type').write_text('sink\n')
        self.assertNotEqual(self.run_role('host').returncode, 0)
        self.assertEqual((self.port / 'port_type').read_text(), 'source sink [dual]\n')
        self.assertEqual((second / 'port_type').read_text(), 'sink\n')

    def test_rejected_tcpm_request_does_not_fall_back_to_dwc(self):
        target = self.port / 'power_role'
        target.unlink()
        target.symlink_to('/dev/full')
        self.assertNotEqual(self.run_role('power', 'source').returncode, 0)
        self.assertEqual(self.dwc.read_text(), 'device\n')

    def test_host_does_not_fail_or_wait_for_a_gadget_controller(self):
        self.dwc.write_text('host\n')
        gadget = PROGRAM.with_name('w202-usb-gadget')
        result = subprocess.run(['/bin/sh', str(gadget), 'start'], env=self.env,
                                capture_output=True, text=True, timeout=3)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.dwc.read_text(), 'host\n')
        self.assertFalse((self.root / 'modules').exists())


if __name__ == '__main__':
    unittest.main()
