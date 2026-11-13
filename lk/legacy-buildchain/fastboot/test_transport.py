"""Run the native bulk replacements as ARM64, including synchronous callbacks."""
import json
import os
from pathlib import Path
import struct
import unittest
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn import arm64_const as r

STATE, BUFFER, EP, REQ, OPS = 0xb52aa828, 0x86000000, 0xb5c00000, 0xb5c01000, 0xb5c02000
QUEUE, CANCEL, END = 0xb6001000, 0xb6002000, 0xb6003000

class TransportTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build=Path(os.environ['LK_FLASH_BUILD'])
        cls.image=(build/'lk-debuger.img').read_bytes()
        cls.symbols=json.loads((build/'manifest.json').read_text())['symbols']

    def run_transfer(self, *, tx=False, size=0x500000, fault=None):
        u=Uc(UC_ARCH_ARM64,UC_MODE_ARM)
        u.mem_map(0xb5000000,0x1100000);u.mem_map(BUFFER,0x600000)
        u.mem_write(0xb5000000,self.image[512:0x113a60])
        def put(a,v,fmt='<Q'):u.mem_write(a,struct.pack(fmt,v))
        def get(a,fmt='<Q'):return struct.unpack(fmt,u.mem_read(a,struct.calcsize(fmt)))[0]
        put(STATE+0x20,1,'<B');put(STATE+(0x28 if tx else 0x30),EP)
        put(STATE+(0x48 if tx else 0x50),REQ);put(EP+0x10,OPS);put(EP+0x28,512,'<H')
        put(OPS+0x20,QUEUE);put(OPS+0x28,CANCEL)
        state=dict(chunks=[],cancel=0,clock=0,polls=0)
        def ret(v=0):
            u.reg_write(r.UC_ARM64_REG_X0,v&0xffffffffffffffff)
            u.reg_write(r.UC_ARM64_REG_PC,u.reg_read(r.UC_ARM64_REG_X30))
        def hook(uc,pc,n,unused):
            ins=get(pc,'<I')
            # Model the architectural counter, without depending on Unicorn's wall clock.
            if ins&~31 in (0xd53be000,0xd53be020):
                value=1000
                if ins&~31==0xd53be020:
                    state['clock']+=10000;value=state['clock']
                uc.reg_write(getattr(r,'UC_ARM64_REG_X'+str(ins&31)),value)
                uc.reg_write(r.UC_ARM64_REG_PC,pc+4);return
            if pc==QUEUE:
                self.assertEqual(get(STATE+4,'<I'),0,'completion must be cleared before queue')
                count=get(REQ+8,'<I');address=get(REQ)
                self.assertEqual(address,BUFFER+sum(state['chunks']))
                self.assertLessEqual(count,65536)
                state['chunks'].append(count)
                if fault=='queue':ret(-1);return
                # Completion may happen inside queue(), before it returns.
                if fault!='timeout':
                    put(REQ+0x44,count if fault!='short' else 17,'<I')
                    put(STATE+4,1,'<I')
                    if fault=='status':put(STATE,0xffffffff,'<I')
                    if fault=='oversized':put(REQ+0x44,count+1,'<I')
                ret()
            elif pc==0xb50857b4:
                state['polls']+=1
                if fault=='disconnect':put(STATE+0x20,0,'<B')
                if fault=='freed':
                    put(STATE+(0x48 if tx else 0x50),0)
                    uc.mem_write(REQ,bytes(128))
                ret()
            elif pc==CANCEL:
                state['cancel']+=1;ret()
        u.hook_add(UC_HOOK_CODE,hook)
        saved={getattr(r,f'UC_ARM64_REG_X{i}'):0x9000+i for i in range(18,30)}
        for reg,v in saved.items():u.reg_write(reg,v)
        u.reg_write(r.UC_ARM64_REG_X0,BUFFER);u.reg_write(r.UC_ARM64_REG_X1,size)
        u.reg_write(r.UC_ARM64_REG_X30,END);u.reg_write(r.UC_ARM64_REG_SP,0xb5e00000)
        entry='lk_debug_usb_write' if tx else 'lk_debug_usb_read'
        u.emu_start(self.symbols[entry],END,count=1000000)
        self.assertEqual(u.reg_read(r.UC_ARM64_REG_PC),END)
        self.assertEqual(u.reg_read(r.UC_ARM64_REG_SP),0xb5e00000)
        for reg,v in saved.items():self.assertEqual(u.reg_read(reg),v)
        result=u.reg_read(r.UC_ARM64_REG_X0)&0xffffffff
        state['result']=result if result<0x80000000 else result-0x100000000
        return state

    def test_five_mib_and_immediate_completion(self):
        for tx in (False,True):
            s=self.run_transfer(tx=tx)
            self.assertEqual(s['result'],0x500000)
            self.assertEqual(s['chunks'],[65536]*80)

    def test_timeout_cancels_dma(self):
        for tx in (False,True):
            s=self.run_transfer(tx=tx,fault='timeout')
            self.assertEqual(s['result'],-110);self.assertEqual(s['cancel'],1)
            self.assertLess(s['polls'],10)

    def test_disconnect_reset_and_io_errors_return_for_native_rebind(self):
        for fault in ('disconnect','freed','queue','status','oversized'):
            for tx in (False,True):
                with self.subTest(fault=fault,tx=tx):
                    s=self.run_transfer(tx=tx,fault=fault)
                    self.assertEqual(s['result'],-110)
                    self.assertEqual(len(s['chunks']),1)

    def test_short_transfer_reports_actual(self):
        self.assertEqual(self.run_transfer(size=512,fault='short')['result'],17)

    def test_disconnect_event_enabled_and_patches_validated(self):
        import runpy
        api=runpy.run_path(str(Path(__file__).parents[1]/'debug/image.py'))
        self.assertTrue(api['validate'](self.image)['usb_transport_recovery'])
        b=bytearray(self.image);b[api['USB_EVENTS_OFFSET']]^=1
        with self.assertRaises(ValueError):api['validate'](b)

    def test_unbind_releases_preconfiguration_requests_once(self):
        for bulk_already_freed in (False,True):
            u=Uc(UC_ARCH_ARM64,UC_MODE_ARM);u.mem_map(0xb5000000,0x1100000)
            u.mem_write(0xb5000000,self.image[512:0x113a60])
            gadget,dev=0xb5c04000,0xb5c05000
            def put(a,v):u.mem_write(a,struct.pack('<Q',v))
            put(gadget+0x58,dev);put(gadget+8,EP);put(EP+0x10,OPS);put(OPS+0x18,CANCEL)
            put(dev+8,REQ);put(dev+32,EP);put(dev+40,EP)
            if not bulk_already_freed:put(dev+64,REQ+128);put(dev+72,REQ+256)
            freed=[]
            def hook(uc,pc,n,unused):
                if pc==CANCEL:
                    freed.append(uc.reg_read(r.UC_ARM64_REG_X1))
                    uc.reg_write(r.UC_ARM64_REG_PC,uc.reg_read(r.UC_ARM64_REG_X30))
            u.hook_add(UC_HOOK_CODE,hook)
            for _ in range(2):
                u.reg_write(r.UC_ARM64_REG_X0,gadget);u.reg_write(r.UC_ARM64_REG_X30,END)
                u.reg_write(r.UC_ARM64_REG_SP,0xb5e00000)
                u.emu_start(self.symbols['lk_debug_usb_unbind'],END,count=10000)
                self.assertEqual(u.reg_read(r.UC_ARM64_REG_PC),END)
            self.assertEqual(freed,[REQ] if bulk_already_freed else [REQ,REQ+128,REQ+256])
            self.assertEqual(bytes(u.mem_read(gadget+0x58,8)),bytes(8))

if __name__=='__main__':unittest.main()
