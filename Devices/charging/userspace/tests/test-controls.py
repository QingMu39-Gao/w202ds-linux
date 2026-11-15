import importlib.machinery
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

source = Path(__file__).resolve().parents[1] / 'tests/reference_power.py'
loader = importlib.machinery.SourceFileLoader('power_control', str(source))
spec = importlib.util.spec_from_loader(loader.name, loader)
module = importlib.util.module_from_spec(spec)
loader.exec_module(module)


class PowerControlsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.sysfs = self.root / 'sys'
        self.calls = []
        self.control = module.Controller(self.sysfs, self.calls.append)
        self.supplies = self.sysfs / 'class/power_supply'
        self.port = self.sysfs / 'devices/platform/pd/typec/port0'
        self.put(self.port / 'port_type', '[dual] source sink')
        self.put(self.port / 'data_role', '[device] host')
        self.put(self.port / 'power_role', '[sink] source')
        self.put(self.port / 'select_usb_power_delivery', '[pd7] pd8')
        self.link(self.sysfs / 'class/typec/port0', self.port)
        for name, attrs in {
            'w202-battery': {'capacity': '15', 'current_now': '-283804', 'status': 'Discharging'},
            'w202-usb-c': {'online': '1', 'usb_type': 'C PD [PD_PPS]', 'voltage_now': '9700000'},
            'w202-bq-main': {'charge_behaviour': '[auto] inhibit-charge',
                             'device/fast_charge': '1', 'device/charge_enabled': '1'},
            'tcpm-source-psy-pd': {'online': '2', 'current_now': '3000000', 'voltage_now': '9800000'},
        }.items():
            for attr, text in attrs.items():
                self.put(self.supplies / name / attr, text)
        for number, maximum in [(7, 9800), (8, 5000)]:
            path = self.sysfs / f'devices/platform/pd/usb_power_delivery/pd{number}'
            self.put(path / 'sink-capabilities/1:fixed_supply/voltage', '5000mV')
            if maximum > 5000:
                self.put(path / 'sink-capabilities/2:fixed_supply/voltage', '9000mV')
                self.put(path / 'sink-capabilities/3:programmable_supply/maximum_voltage', f'{maximum}mV')
            self.link(self.sysfs / f'class/usb_power_delivery/pd{number}', path)

    @staticmethod
    def put(path, text):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text + '\n')

    @staticmethod
    def link(path, target):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.symlink_to(target)

    def snapshot(self):
        return {str(p): p.read_bytes() for p in self.sysfs.rglob('*') if p.is_file()}

    def test_status_is_read_only_and_distinguishes_measured_current(self):
        before = self.snapshot()
        status = self.control.status()
        self.assertEqual(status['battery_current_ua'], -283804)
        self.assertEqual(status['contract_current_ua'], 3000000)
        self.assertIsNone(status['input_current_ua'])
        self.assertEqual(status['protocol'], 'PD PPS')
        self.assertEqual(status['available'], dict(charging=True, fast_charge=True, usb_role=True))
        self.assertEqual(before, self.snapshot())
        self.assertEqual(self.calls, [])

    def test_pps_capability_is_not_reported_as_an_active_pps_contract(self):
        self.put(self.supplies / 'tcpm-source-psy-pd/online', '1')
        self.assertEqual(self.control.status()['protocol'], 'PD fixed')

    def test_sysfs_device_metadata_does_not_hide_pd_profiles(self):
        for number in (7, 8):
            path = self.sysfs / f'devices/platform/pd/usb_power_delivery/pd{number}/sink-capabilities'
            self.put(path / 'uevent', 'DEVTYPE=usb_power_delivery_capabilities')
            self.put(path / 'power/runtime_status', 'unsupported')
            self.link(path / 'subsystem', self.sysfs / 'class/usb_power_delivery')
        self.assertTrue(self.control.status()['available']['fast_charge'])

    def test_fast_disable_gates_pps_then_selects_only_five_volts(self):
        self.control.apply('fast-off')
        self.assertEqual(self.control.read(self.supplies / 'w202-bq-main/device/fast_charge'), '0')
        self.assertEqual(self.control.read(self.port / 'select_usb_power_delivery'), 'pd8')
        self.assertFalse(self.control.status()['fast_charge'])

    def test_fast_enable_selects_fast_profile_and_restores_pps_permission(self):
        self.control.apply('fast-off')
        self.control.apply('fast-on')
        self.assertTrue(self.control.status()['fast_charge'])

    def test_unavailable_profile_never_writes_permission(self):
        (self.sysfs / 'class/usb_power_delivery/pd8').unlink()
        before = self.snapshot()
        with self.assertRaises(ValueError):
            self.control.apply('fast-off')
        self.assertFalse(self.control.status()['available']['fast_charge'])
        self.assertEqual(before, self.snapshot())

    def test_unknown_action_cannot_become_a_command(self):
        with self.assertRaises(ValueError):
            self.control.apply('usb-host; reboot')
        self.assertEqual(self.calls, [])

    def test_roles_go_through_the_tcpm_board_tool(self):
        self.control.apply('usb-host')
        self.assertEqual(self.calls, [['/usr/libexec/w202-usb-role', 'host']])

    def test_old_single_role_controller_disables_role_button(self):
        self.put(self.port / 'port_type', '[sink]')
        self.assertFalse(self.control.status()['available']['usb_role'])

    def test_missing_hardware_remains_readable(self):
        status = module.Controller(self.root / 'empty').status()
        self.assertFalse(any(status['available'].values()))
        self.assertIsNone(status['battery_current_ua'])

    def test_multi_port_ambiguity_rejects_fast_write(self):
        self.put(self.sysfs / 'class/typec/port2/port_type', '[dual] source sink')
        with self.assertRaisesRegex(ValueError, 'Ambiguous'):
            self.control.apply('fast-on')

    def test_pd_fixed_and_pps_permissions_are_distinct(self):
        self.control.apply('mode-pd9v')
        self.assertEqual(self.control.status()['charging_mode'], 'pd9v')
        self.assertTrue(self.control.status()['fast_charge'])
        self.assertFalse(self.control.status()['pps_allowed'])
        self.control.apply('mode-pps')
        self.assertEqual(self.control.status()['charging_mode'], 'pps')
        self.assertTrue(self.control.status()['pps_allowed'])
        self.control.apply('mode-pd5v')
        self.assertEqual(self.control.status()['charging_mode'], 'pd5v')

    def test_auto_reopens_maximum_profile_after_fixed_five_volts(self):
        self.control.apply('mode-pd5v')
        self.control.apply('mode-auto')
        status = self.control.status()
        self.assertEqual(status['pd_profile'], 'pd7')
        self.assertTrue(status['pps_allowed'])

    def test_absent_voltage_sensor_is_not_fabricated_from_pd_contract(self):
        self.assertIsNone(self.control.status()['battery_voltage_uv'])
        self.put(self.supplies / 'w202-battery/voltage_now', '3980000')
        self.assertEqual(self.control.status()['battery_voltage_uv'], 3980000)

    def test_measured_input_current_uses_usb_sensor_only_while_sinking(self):
        self.put(self.supplies / 'w202-usb-c/current_now', '845000')
        self.assertEqual(self.control.status()['input_current_ua'], 845000)
        self.put(self.port / 'power_role', '[source] sink')
        self.assertIsNone(self.control.status()['input_current_ua'])

    def test_unplugged_source_role_does_not_mean_output_power(self):
        self.put(self.port / 'power_role', '[source] sink')
        self.put(self.supplies / 'w202-usb-c/online', '0')
        self.put(self.supplies / 'w202-usb-c/voltage_now', '159000')
        status = self.control.status()
        self.assertFalse(status['typec_connected'])
        self.assertFalse(status['usb_output_active'])
        self.assertFalse(status['input_online'])
        self.put(self.supplies / 'w202-usb-c/voltage_now', '5000000')
        self.assertFalse(self.control.status()['usb_output_active'])
        self.port.with_name('port0-partner').mkdir()
        self.link(self.sysfs / 'class/typec/port0-partner', self.port.with_name('port0-partner'))
        self.assertTrue(self.control.status()['usb_output_active'])



if __name__ == '__main__':
    unittest.main()
