"""Exercise policy startup failures against a fake power_supply tree."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

PROGRAM = Path(__file__).resolve().parents[1] / 'rootfs/usr/libexec/w202-charging'


class ChargingPolicyTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.supplies = self.root / 'supplies'
        for name in ('w202-bq-main', 'w202-bq-slave'):
            path = self.supplies / name / 'charge_behaviour'
            path.parent.mkdir(parents=True)
            path.write_text('inhibit-charge\n')
        battery = self.supplies / 'w202-battery/voltage_now'
        battery.parent.mkdir()
        battery.write_text('3800000\n')
        commands = self.root / 'bin'
        commands.mkdir()
        for name, content in {
            'modprobe': '#!/bin/sh\necho "$1" >> "$MODULE_LOG"\n'
                        'test "$1" != "${MISSING_MODULE:-none}"\n',
            'sleep': '#!/bin/sh\nexit 0\n',
        }.items():
            path = commands / name
            path.write_text(content)
            path.chmod(0o755)
        self.env = dict(os.environ, PATH=str(commands) + os.pathsep + os.environ['PATH'],
                        W202_SYSFS_ROOT=str(self.root / 'sys'),
                        W202_POWER_SUPPLY_ROOT=str(self.supplies),
                        MODULE_LOG=str(self.root / 'modules.log'))

    def run_mode(self, mode):
        return subprocess.run(['/bin/sh', str(PROGRAM), mode], env=self.env,
                              capture_output=True, text=True, timeout=5)

    def mode(self, name):
        return (self.supplies / name / 'charge_behaviour').read_text().strip()

    def test_auto_enables_only_main(self):
        result = self.run_mode('auto')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.mode('w202-bq-main'), 'auto')
        self.assertEqual(self.mode('w202-bq-slave'), 'inhibit-charge')

    def test_invalid_mode_has_no_driver_or_charger_side_effects(self):
        self.assertEqual(self.run_mode('force-fast').returncode, 2)
        self.assertFalse((self.root / 'modules.log').exists())
        self.assertEqual(self.mode('w202-bq-main'), 'inhibit-charge')

    def test_status_reads_policy_actual_current_and_separate_usb_roles_without_writes(self):
        current = self.supplies / 'w202-battery/current_now'
        current.write_text('-120000\n')
        device = self.supplies / 'w202-bq-main/device'
        device.mkdir()
        (device / 'charge_enabled').write_text('0\n')
        sysfs = self.root / 'sys'
        for name, value in {
            'class/typec/port0/data_role': '[device] host\n',
            'class/typec/port0/power_role': '[sink] source\n',
            'class/usb_role/dwc3/role': 'device\n',
        }.items():
            path = sysfs / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(value)
        before = {str(p): p.read_bytes() for p in self.root.rglob('*') if p.is_file()}
        result = self.run_mode('status')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('w202-battery.current_now=-120000', result.stdout)
        self.assertIn('w202-bq-main.charge_enabled=0', result.stdout)
        self.assertIn('w202-bq-main.charge_behaviour=inhibit-charge', result.stdout)
        self.assertIn('port0.data_role=[device] host', result.stdout)
        self.assertIn('port0.power_role=[sink] source', result.stdout)
        self.assertIn('usb_role.dwc3=device', result.stdout)
        self.assertFalse((self.root / 'modules.log').exists())
        after = {str(p): p.read_bytes() for p in self.root.rglob('*') if p.is_file()}
        self.assertEqual(before, after)

    def test_cannot_enable_main_when_slave_stop_write_fails(self):
        slave = self.supplies / 'w202-bq-slave/charge_behaviour'
        slave.unlink()
        slave.symlink_to('/dev/full')
        self.assertNotEqual(self.run_mode('auto').returncode, 0)
        self.assertEqual(self.mode('w202-bq-main'), 'inhibit-charge')

    def test_missing_telemetry_module_prevents_charge_request(self):
        self.env['MISSING_MODULE'] = 'w202_ump9620_power'
        self.assertNotEqual(self.run_mode('auto').returncode, 0)
        self.assertEqual(self.mode('w202-bq-main'), 'inhibit-charge')

    def test_no_pd_controller_keeps_guarded_ordinary_charge_available(self):
        self.env['MISSING_MODULE'] = 'w202_ump9620_typec'
        result = self.run_mode('auto')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('ordinary USB limits', result.stderr)
        self.assertEqual(self.mode('w202-bq-main'), 'auto')

    def test_missing_battery_telemetry_prevents_charge_request(self):
        (self.supplies / 'w202-battery/voltage_now').unlink()
        result = self.run_mode('auto')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('did not become ready', result.stderr)
        self.assertEqual(self.mode('w202-bq-main'), 'inhibit-charge')


if __name__ == '__main__':
    unittest.main()
