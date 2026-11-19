"""Build and verify the independent W202DS LK splash extension."""
import hashlib
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parent
LK = 0xb5000000
START, CODE, END = LK + 0x1a00, LK + 0x1a60, LK + 0x2000
MAGIC = b'LKSPL001'

# Hooks replace complete BL instructions in the verified baseline. Only the
# first hook paints ordinary bitmaps; the callers below it supply the mode.
HOOKS = {
    'lk_logo_present': [(0xb5087340, 0x97fffe70)],
    'lk_logo_clear': [(0xb5088aa0, 0x97fff898), (0xb5088c98, 0x97fff81a)],
    'lk_logo_text': [(0xb5088cc0, 0x97fffdcf)],
    'lk_logo_selected': [(0xb500665c, 0x9400be41)],
    'lk_logo_fastboot_splash': [(0xb500de3c, 0x9401eb66)],
    'lk_logo_fastboot_enter': [(0xb5012aac, 0x94008c73)],
    'lk_logo_low_bitmap': [(0xb5006084, 0x94020a7a), (0xb5006098, 0x94020a75),
                           (0xb50062e4, 0x940209e2), (0xb5006368, 0x940209c1),
                           (0xb5006394, 0x940209b6), (0xb50063cc, 0x940209a8)],
    'lk_logo_no_battery_bitmap': [(0xb506f4a0, 0x94006573)],
    'lk_logo_charge_bitmap': [(0xb506fcbc, 0x9400636c)],
    'lk_logo_charge_splash': [(0xb506fd68, 0x9400639b)],
}
CONFIG = struct.Struct(f'<8sI{len(HOOKS)}I32s')
assert CONFIG.size <= CODE - START


def offset(address):
    return address - LK + 512


def digest(data):
    return hashlib.sha256(data).digest()


def patches(code, entries):
    if not code or len(code) > END - CODE:
        raise ValueError('LK splash extension does not fit in the boot padding')
    if any(address % 4 or not CODE <= address < CODE + len(code) for address in entries):
        raise ValueError('LK splash hook points outside its code')
    for name, address in zip(HOOKS, entries, strict=True):
        for site, original in HOOKS[name]:
            yield offset(site), original, 0x94000000 | (((address - site) >> 2) & 0x3ffffff)


def remove(image):
    """Check and strip splash changes before verifying the underlying LK."""
    start, code_at, end = map(offset, (START, CODE, END))
    if image[start:start+8] != MAGIC:
        return image, False
    _, size, *rest = CONFIG.unpack_from(image, start)
    *entries, checksum = rest
    code = image[code_at:code_at+size]
    if (not 0 < size <= END - CODE or digest(code) != checksum or
            any(image[start+CONFIG.size:code_at]) or any(image[code_at+size:end])):
        raise ValueError('invalid LK splash code or padding')
    base = bytearray(image)
    base[start:end] = bytes(end-start)
    for at, original, patched in patches(code, entries):
        if struct.unpack_from('<I', image, at)[0] != patched:
            raise ValueError('LK splash hook does not match its manifest')
        struct.pack_into('<I', base, at, original)
    return bytes(base), True


def install(image, code, entries):
    start, code_at, end = map(offset, (START, CODE, END))
    if any(image[start:end]):
        raise ValueError('LK splash padding is occupied')
    result = bytearray(image)
    for at, original, patched in patches(code, entries):
        if struct.unpack_from('<I', image, at)[0] != original:
            raise ValueError(f'unexpected LK splash instruction at offset {at:#x}')
        struct.pack_into('<I', result, at, patched)
    CONFIG.pack_into(result, start, MAGIC, len(code), *entries, digest(code))
    result[code_at:code_at+len(code)] = code
    if remove(result) != (image, True):
        raise ValueError('LK splash extension changed bytes outside its layout')
    return bytes(result)


def build(out, cross_compile, run):
    out.mkdir()
    run(cross_compile+'gcc', '-march=armv8-a', '-c', ROOT/'hooks.S', '-o', out/'hooks.o')
    run(cross_compile+'gcc', '-march=armv8-a', '-Os', '-Wall', '-Wextra', '-Werror',
        '-ffreestanding', '-fno-builtin', '-fno-stack-protector', '-fno-pie',
        '-fno-asynchronous-unwind-tables', '-mgeneral-regs-only', '-ffixed-x18',
        '-c', ROOT/'render.c', '-o', out/'render.o')
    run(cross_compile+'ld', '-T', ROOT/'layout.ld', out/'hooks.o', out/'render.o',
        '-o', out/'splash.elf')
    run(cross_compile+'objcopy', '-O', 'binary', '-j', '.text', out/'splash.elf', out/'splash.bin')
    listing = subprocess.check_output([cross_compile+'nm', '-n', str(out/'splash.elf')], text=True)
    symbols = {v[2]:int(v[0], 16) for line in listing.splitlines() if len(v := line.split()) == 3}
    return (out/'splash.bin').read_bytes(), [symbols[name] for name in HOOKS]
