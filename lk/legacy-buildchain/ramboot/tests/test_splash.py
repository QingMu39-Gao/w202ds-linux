"""Run the patched LK bitmap paths and compare actual scanout pixels."""
import os
from pathlib import Path
import struct
import unittest

from unicorn import Uc, UC_ARCH_ARM64, UC_HOOK_CODE, UC_MODE_ARM
from unicorn.arm64_const import (UC_ARM64_REG_LR, UC_ARM64_REG_PC, UC_ARM64_REG_SP,
                                UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2)

LK, FB, BMP, DPU = 0xb5000000, 0xb0000000, 0x9d000000, 0x31000000
WIDTH, HEIGHT = 1200, 1920
FRAME_BYTES = WIDTH * HEIGHT * 2
END, STACK = 0xb6001000, 0xb5e00000
MODE_LOG, FASTBOOT_LOG = 0xb500665c, 0xb5012aac
BITMAP, CACHED_BITMAP = 0xb5088a6c, 0xb50885a0
ASSET, CACHE, PRINT, LOG = 0xb50889fc, 0xb50043d0, 0xb5035e94, 0xb5035f60
PARTITION, FILENAME = 0xb50b8f68, 0xb50e1b50
COLOUR = 0x39e7


def cstring(uc, address):
    return bytes(uc.mem_read(address, 64)).split(b'\0', 1)[0]


class LogoPanel:
    def __init__(self, uc):
        self.u = uc
        self.missing = False
        self.assets = []
        self.logs = []
        self.cleaned = []
        # A four-pixel, 16-bit BMP, backed by the native logo memory.
        header = bytearray(54)
        struct.pack_into('<2sI4xIIiiHH', header, 0, b'BM', 62, 54, 40, 4, 1, 1, 16)
        self.bmp = bytes(header) + struct.pack('<4H', *([COLOUR]*4))
        uc.mem_map(FB, FRAME_BYTES)
        uc.mem_map(BMP, 0x1000)
        uc.mem_write(0xb56bb088, struct.pack('<QQQ', FB, 0x9e4000, BMP))
        uc.mem_write(0xb56bae08, b'\x01')
        uc.mem_write(0xb56bae10, struct.pack('<Q', FB))
        uc.mem_write(0xb56bae20, struct.pack('<IB', WIDTH*2, 1))
        uc.mem_write(0xb50ffc98, struct.pack('<HHHB', WIDTH, HEIGHT, WIDTH, 8))
        for start in (ASSET, CACHE, PRINT, LOG, 0xb5035c78):
            uc.hook_add(UC_HOOK_CODE, self.hardware, begin=start, end=start)

    def hardware(self, uc, pc, size, _):
        x0, x1, x2 = (uc.reg_read(reg) for reg in (UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2))
        if pc == ASSET:
            self.assets.append((cstring(uc, x0), cstring(uc, x1)))
            if not self.missing:
                uc.mem_write(x2, self.bmp)
            uc.reg_write(UC_ARM64_REG_X0, 0xffffffff if self.missing else 0)
        elif pc == CACHE:
            self.cleaned.append((x0, x1, self.screen_label()))
        elif pc == LOG and uc.reg_read(UC_ARM64_REG_LR) == MODE_LOG + 4:
            self.logs.append((cstring(uc, x0), x1, cstring(uc, x2)))
        uc.reg_write(UC_ARM64_REG_PC, uc.reg_read(UC_ARM64_REG_LR))

    def run(self, addr, *args, end=END):
        for reg, value in zip((UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2), args):
            self.u.reg_write(reg, value)
        self.u.reg_write(UC_ARM64_REG_LR, end)
        self.u.emu_start(addr, end, count=10_000_000)
        if self.u.reg_read(UC_ARM64_REG_PC) != end:
            raise AssertionError(f'LK did not return from {addr:#x}')
        return self.u.reg_read(UC_ARM64_REG_X0) & 0xffffffff

    def select(self, mode, name):
        name_address = struct.unpack('<Q', self.u.mem_read(0xb50b7c20 + 8*mode, 8))[0]
        self.run(MODE_LOG, 0xb50b9238, mode, name_address, end=MODE_LOG+4)
        if self.logs[-1] != (cstring(self.u, 0xb50b9238), mode, name.encode()):
            raise AssertionError(f'LK mode log changed: {self.logs[-1]}')

    def screen_label(self):
        # Read the physical scanout region as a 512x32 landscape raster.
        # DPU code 2 rotates the source 180 degrees; the mounted panel
        # maps its portrait coordinates to the landscape screen at 270.
        rect = [self.u.mem_read(FB + (HEIGHT-1-x)*WIDTH*2 + 24*2, 64)
                for x in range(24, 24+512)]
        return bytes(col[y*2] for y in range(32) for col in rect)

    def expected_label(self, mode):
        cols = b''.join(self.u.mem_read(0xb50de848 + 16*c, 16)
                        for c in ('LK MODE: ' + mode).ljust(32).encode())
        return bytes(255 if cell & (0x80 >> (x//2)) else 0
                     for y in range(16) for _ in range(2)
                     for cell in cols[y::16] for x in range(16))


class SplashTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image = Path(os.environ['RESIDENT_IMAGE']).read_bytes()

    def setUp(self):
        self.u = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        self.u.mem_map(LK, 0x1000000)
        self.u.mem_write(LK, self.image[512:0x113a60])
        self.u.mem_map(DPU, 0x1000)
        self.u.mem_map(END, 0x1000)
        self.u.reg_write(UC_ARM64_REG_SP, STACK)
        self.panel = LogoPanel(self.u)

    def assert_splash(self, mode):
        self.assertEqual(self.panel.screen_label(), self.panel.expected_label(mode))
        self.assertEqual(self.panel.cleaned[-1], (FB, FB+FRAME_BYTES, self.panel.expected_label(mode)))
        self.assertEqual(self.u.mem_read(FB, 8), struct.pack('<4H', *([COLOUR]*4)))

    def test_final_mode_on_the_shared_bitmap_exit(self):
        for mode, label in ((2, 'NORMAL_MODE'), (16, 'EXT_RSTN_REBOOT_MODE'),
                            (4, 'RECOVERY_MODE'), (5, 'FASTBOOT_MODE'),
                            (7, 'CHARGE_MODE'), (9, 'WATCHDOG_REBOOT')):
            with self.subTest(mode=mode):
                self.panel.select(mode, label)
                self.assertEqual(self.panel.run(BITMAP, PARTITION, FILENAME), 0)
                self.assert_splash(label)

    def test_early_battery_calls_keep_context_even_when_using_fallback_asset(self):
        for site, filename, label in (
            (0xb5006084, 0xb50b8f70, 'LOW BATTERY'),
            (0xb5006098, 0xb50b8fb8, 'LOW BATTERY'),
            (0xb506fcbc, 0xb50b8fb8, 'CHARGE_MODE'),
            (0xb506f4a0, 0xb50da0b0, 'NO BATTERY'),
        ):
            with self.subTest(site=hex(site)):
                self.assertEqual(self.panel.run(site, PARTITION, filename, end=site+4), 0)
                self.assertEqual(self.panel.assets[-1][1], cstring(self.u, filename))
                self.assert_splash(label)

    def test_preload_is_relabelled_and_cached_fastboot_redraw_keeps_its_mode(self):
        self.assertEqual(self.panel.run(BITMAP, PARTITION, FILENAME), 0)
        self.assert_splash('DETECTING')
        for offset, value in ((0x2c, 1), (0x30, FB), (0x40, 0x00200041), (0x4c, WIDTH)):
            self.u.mem_write(DPU+offset, struct.pack('<I', value))
        self.panel.select(4, 'RECOVERY_MODE')
        self.assert_splash('RECOVERY_MODE')
        self.panel.run(FASTBOOT_LOG, 0xb50be708, end=FASTBOOT_LOG+4)
        self.assert_splash('FASTBOOT_MODE')
        self.u.mem_write(0xb56bc0a8, struct.pack('<I', 1))  # native cache is populated
        self.u.mem_write(FB, bytes(FRAME_BYTES))
        self.assertEqual(self.panel.run(CACHED_BITMAP, PARTITION), 0)
        self.assert_splash('FASTBOOT_MODE')

    def test_no_label_after_missing_or_invalid_bitmap(self):
        self.panel.select(2, 'NORMAL_MODE')
        self.panel.missing = True
        self.assertEqual(self.panel.run(BITMAP, PARTITION, FILENAME), 0xffffffff)
        self.assertEqual(self.panel.screen_label(), bytes(512*32))
        self.panel.missing = False
        self.panel.bmp = b'NO' + self.panel.bmp[2:]
        self.assertEqual(self.panel.run(BITMAP, PARTITION, FILENAME), 1)
        self.assertEqual(self.panel.screen_label(), bytes(512*32))

    def test_unsupported_scanout_is_not_written_by_the_label(self):
        for register, value in ((0xb56bae24, b'\0'), (0xb56bae20, struct.pack('<H', 2392))):
            with self.subTest(register=hex(register)):
                saved = bytes(self.u.mem_read(register, len(value)))
                self.u.mem_write(register, value)
                self.assertEqual(self.panel.run(BITMAP, PARTITION, FILENAME), 0)
                self.assertEqual(self.panel.screen_label(), bytes(512*32))
                self.u.mem_write(register, saved)


if __name__ == '__main__':
    unittest.main()
