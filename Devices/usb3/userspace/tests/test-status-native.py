"""Run a W202_TESTING native USB binary against isolated sysfs fixtures."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class AttachmentStatus(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.put('class/typec/port0/port_type', '[dual] source sink')
        self.put('class/typec/port0/data_role', 'host [device]')
        self.put('class/typec/port0/power_role', 'source [sink]')
        self.put('kernel/config/usb_gadget/test/UDC', 'test-udc')
        self.put('class/udc/test-udc/state', 'not attached')

    def put(self, name, value):
        p = self.root / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(value + '\n')

    def status(self):
        result = subprocess.check_output(
            [os.environ['W202_USB_STATUS_TEST_BINARY'], 'status'],
            env=dict(os.environ, W202_SYSFS_ROOT=str(self.root)), text=True)
        return json.loads(result)

    def test_unplugged_keeps_role_and_binding_but_not_connection(self):
        s = self.status()
        self.assertEqual(s['data_role'], 'device')
        self.assertTrue(s['bound'])
        self.assertFalse(s['connected'])
        self.assertFalse(s['configured'])
        self.assertEqual(s['link_state'], 'not attached')

    def test_attached_not_yet_configured(self):
        (self.root / 'class/typec/port0-partner').mkdir()
        self.put('class/udc/test-udc/state', 'powered')
        s = self.status()
        self.assertTrue(s['connected'])
        self.assertFalse(s['configured'])

    def test_attached_and_configured(self):
        (self.root / 'class/typec/port0-partner').mkdir()
        self.put('class/udc/test-udc/state', 'configured')
        s = self.status()
        self.assertTrue(s['connected'])
        self.assertTrue(s['configured'])

    def test_missing_typec_is_unknown(self):
        (self.root / 'class/typec/port0/port_type').unlink()
        self.assertIsNone(self.status()['connected'])


if __name__ == '__main__':
    unittest.main()
