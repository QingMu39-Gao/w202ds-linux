/* SPDX-License-Identifier: MIT
 * Bounded native Fastboot bulk I/O for the pinned DWC3 gadget. Completion
 * state is cleared BEFORE queueing; reset/disconnect may free either request.
 * -110 is this LK's ESHUTDOWN: its command loop tears down and rebinds USB.
 */
#include <stdint.h>
#define U32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define PTR(a) (*(volatile uintptr_t *)(uintptr_t)(a))
#define STATE 0xb52aa828u
static uint64_t ticks(void) {
    uint64_t n;__asm__ volatile("mrs %0, cntpct_el0":"=r"(n));return n;
}
static int transfer(int tx,void *buffer,unsigned bytes) {
    uint64_t frequency;__asm__ volatile("mrs %0, cntfrq_el0":"=r"(frequency));
    unsigned received=0;
    while(bytes) {
        uintptr_t ep=PTR(STATE+(tx?0x28:0x30));
        uintptr_t req=PTR(STATE+(tx?0x48:0x50));
        if(!*(volatile uint8_t *)(uintptr_t)(STATE+0x20) || !ep || !req) return -110;
        unsigned packet=*(volatile uint16_t *)(ep+0x28);
        if(!packet || packet>1024 || (packet&(packet-1))) return -110;
        unsigned count=bytes>0x10000?0x10000:bytes;
        unsigned dma=tx?count:(count+packet-1)&~(packet-1);
        PTR(req)=(uintptr_t)buffer;U32(req+8)=dma;U32(req+0x44)=0;
        U32(STATE)=0;U32(STATE+4)=0;
        int (*queue)(void *,void *,unsigned)=(void *)PTR(PTR(ep+0x10)+0x20);
        if(queue((void *)ep,(void *)req,0)<0) return -110;
        uint64_t started=ticks();
        for(;;) {
            ((void (*)(void))0xb50857b4)();
            /* Check ownership before dereferencing a possibly freed request. */
            if(!*(volatile uint8_t *)(uintptr_t)(STATE+0x20) ||
               req!=PTR(STATE+(tx?0x48:0x50))) return -110;
            if(U32(STATE+4)) break;
            if(ticks()-started>=frequency*30) {
                /* Cancel before returning: no DMA into a reused buffer. */
                int (*dequeue)(void *,void *)=(void *)PTR(PTR(ep+0x10)+0x28);
                dequeue((void *)ep,(void *)req);
                return -110;
            }
        }
        if((int32_t)U32(STATE)<0) return -110;
        unsigned actual=U32(req+0x44);
        if(actual>count) return -110;
        received+=actual;buffer=(uint8_t *)buffer+actual;bytes-=actual;
        if(actual!=count) break;
    }
    return received;
}
int lk_debug_usb_read(void *buffer,unsigned bytes) { return transfer(0,buffer,bytes); }
int lk_debug_usb_write(void *buffer,unsigned bytes) { return transfer(1,buffer,bytes); }
/* UDC removal has already disconnected/cancelled endpoint DMA. The native
 * unbind frees only EP0, leaking the bulk pair if configuration was never
 * selected. Repeated reconnects must release all three requests exactly once. */
void lk_debug_usb_unbind(void *gadget) {
    uintptr_t g=(uintptr_t)gadget,dev=PTR(g+0x58);
    if(!dev) return;
    for(unsigned i=0;i<3;++i) {
        uintptr_t request=dev+(i==0?8:i==1?64:72);
        uintptr_t ep=i==0?PTR(g+8):PTR(dev+(i==1?32:40));
        if(ep && PTR(request)) {
            void (*release)(void *,void *)=(void *)PTR(PTR(ep+0x10)+0x18);
            release((void *)ep,(void *)PTR(request));PTR(request)=0;
        }
    }
    PTR(g+0x58)=0;
}
