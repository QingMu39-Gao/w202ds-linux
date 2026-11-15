// SPDX-License-Identifier: GPL-2.0
/* Exercise production role callbacks, independent power/data roles and I/O failure. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define W202_UMP9620_TYPEC_H
#define BIT(n) (1U << (n))
#define GENMASK(h, l) ((~0U >> (31 - (h))) & (~0U << (l)))
#define READ_ONCE(x) (x)
#define fallthrough __attribute__((fallthrough))
typedef uint32_t u32;
#include "../registers.h"
enum typec_cc_status { TYPEC_CC_OPEN, TYPEC_CC_RA, TYPEC_CC_RD, TYPEC_CC_RP_DEF,
                      TYPEC_CC_RP_1_5, TYPEC_CC_RP_3_0 };
enum typec_role { TYPEC_SINK, TYPEC_SOURCE };
enum typec_port_type { TYPEC_PORT_SRC, TYPEC_PORT_SNK, TYPEC_PORT_DRP };
enum typec_data_role { TYPEC_DEVICE, TYPEC_HOST };
enum typec_cc_polarity { TYPEC_POLARITY_CC1, TYPEC_POLARITY_CC2 };
struct regulator { bool on; };
struct mutex { bool held; };
struct w202_typec {
    struct mutex lock;
    u32 *regmap, typec_base, base;
    bool attached, cc_forced, faulted, ready, vbus_on;
    enum typec_role power_role;
    enum typec_cc_polarity polarity;
    struct regulator *vbus;
    unsigned int nr_source_caps;
    int port;
};
struct tcpc_dev { struct w202_typec *pd; };
static struct w202_typec *tcpc_to_w202(struct tcpc_dev *t) { return t->pd; }
static void mutex_lock(struct mutex *m) { assert(!m->held); m->held = true; }
static void mutex_unlock(struct mutex *m) { assert(m->held); m->held = false; }
static u32 regs[0x1000];
static int calls, fail_at, enables, disables, disable_error;
static int regmap_read(u32 *map, u32 reg, u32 *value)
{
    if (++calls == fail_at) return -EIO;
    *value = map[reg]; return 0;
}
static int regmap_update_bits(u32 *map, u32 reg, u32 mask, u32 value)
{
    if (++calls == fail_at) return -EIO;
    map[reg] = (map[reg] & ~mask) | value; return 0;
}
static int regulator_enable(struct regulator *r) { ++enables; r->on = true; return 0; }
static int regulator_disable(struct regulator *r)
{ ++disables; if (disable_error) return -EIO; r->on = false; return 0; }
static void tcpm_cc_change(int port) { (void)port; }
static void w202_pd_latch_fault(struct w202_typec *pd, const char *reason)
{ assert(pd->lock.held && reason); pd->faulted = true; }
static int charger_events;
static bool charger_present;
static void w202_typec_charger_state(struct w202_typec *pd, bool sink)
{ assert(!pd->lock.held); ++charger_events; charger_present = sink; }
#include "../roles.c"

int main(void)
{
    struct regulator vbus = {};
    struct w202_typec pd = { .regmap = regs, .typec_base = 0x380, .base = 0xe00, .vbus = &vbus };
    struct tcpc_dev tcpc = { .pd = &pd };
    enum typec_cc_status cc1, cc2;
    unsigned int i;

    regs[0x39c] = 5 | BIT(5);
    assert(!w202_typec_get_cc(&tcpc, &cc1, &cc2) && cc1 == TYPEC_CC_RD && cc2 == TYPEC_CC_OPEN);
    regs[0x39c] = 2 | (2 << 6);
    assert(!w202_typec_get_cc(&tcpc, &cc1, &cc2) && cc1 == TYPEC_CC_OPEN && cc2 == TYPEC_CC_RP_3_0);
    regs[0x39c] = 6;
    assert(!w202_typec_get_cc(&tcpc, &cc1, &cc2) && cc1 == TYPEC_CC_RA && cc2 == TYPEC_CC_RD);
    puts("PASS: W205 sink/source/powered-cable CC decoding and both orientations");

    regs[0x3e0] = BIT(8);
    assert(w202_typec_set_vbus(&tcpc, true, false) == -EBUSY && enables == 0);
    regs[0x3e0] = 0;
    assert(!w202_typec_set_vbus(&tcpc, true, false) && enables == 1 && pd.vbus_on);
    assert(!w202_typec_set_vbus(&tcpc, true, false) && enables == 1);
    pd.faulted = true; disable_error = 1;
    assert(w202_typec_set_vbus(&tcpc, false, false) == -EIO && pd.vbus_on);
    disable_error = 0;
    assert(!w202_typec_set_vbus(&tcpc, false, false) && !pd.vbus_on && !vbus.on);
    assert(w202_typec_set_vbus(&tcpc, true, false) == -ESHUTDOWN);
    puts("PASS: no VBUS backfeed, idempotent enable, shutdown works through faults and retries");

    pd.faulted = false;
    assert(!w202_typec_set_roles(&tcpc, true, TYPEC_SINK, TYPEC_HOST));
    assert(charger_present);
    assert((regs[0xe08] & 0x120) == 0x20);
    assert(!w202_typec_set_roles(&tcpc, true, TYPEC_SOURCE, TYPEC_DEVICE));
    assert(!charger_present);
    assert((regs[0xe08] & 0x120) == 0x100);
    assert(!w202_typec_set_cc(&tcpc, TYPEC_CC_RD) && pd.cc_forced);
    assert((regs[0x384] & 0xf) == 4); /* W205 default Rp is encoded as 1, not 0. */
    assert(regs[0x388] & BIT(9));
    assert(!w202_typec_set_roles(&tcpc, true, TYPEC_SINK, TYPEC_DEVICE));
    assert(!(regs[0x388] & BIT(9)) && !(regs[0x3d4] & 0xf000) && !pd.cc_forced);
    assert(!w202_typec_set_cc(&tcpc, TYPEC_CC_RP_DEF));
    assert((regs[0x384] & 0xf) == 5);
    assert(!w202_typec_set_roles(&tcpc, true, TYPEC_SOURCE, TYPEC_HOST));
    puts("PASS: host while sinking, device while sourcing, swap releases forced CC/detach suppression");

    for (i = 1; i <= 2; ++i) {
        pd.attached = false; calls = 0; fail_at = i;
        int old_events = charger_events;
        assert(w202_typec_set_roles(&tcpc, true, TYPEC_SOURCE, TYPEC_HOST) == -EIO);
        assert(!pd.attached && !pd.lock.held);
        assert(charger_events == old_events + 1 && !charger_present);
    }
    fail_at = 0;
    for (i = 1; i <= 4; ++i) {
        pd.faulted = false; pd.cc_forced = false;
        pd.attached = true; pd.power_role = TYPEC_SINK;
        calls = 0; fail_at = i;
        assert(w202_typec_set_cc(&tcpc, TYPEC_CC_RP_DEF) == -EIO);
        assert(pd.faulted && pd.cc_forced && !pd.lock.held);
        assert(!(regs[0x388] & BIT(9)) && (regs[0x3d4] & 0xf000) == 0x5000);
        fail_at = 0;
        assert(w202_typec_set_vbus(&tcpc, true, false) == -ESHUTDOWN);
        assert(!w202_typec_set_roles(&tcpc, false, TYPEC_SINK, TYPEC_DEVICE));
        assert(!(regs[0x3d4] & 0xf000) && !pd.cc_forced);
    }
    puts("PASS: every partial CC swap failure disconnects, clears detach suppression and blocks VBUS");
    pd.faulted = false; pd.attached = false;
    assert(!w202_typec_start_toggling(&tcpc, TYPEC_PORT_DRP, TYPEC_CC_RD));
    assert((regs[0x384] & 0x18f) == (2 | 4 | BIT(8)));
    assert(!(regs[0x3d4] & 0xf000) && !(regs[0x388] & BIT(9)));
    assert(!vbus.on && !pd.vbus_on);
    assert(!w202_typec_start_toggling(&tcpc, TYPEC_PORT_SNK, TYPEC_CC_RD));
    assert((regs[0x384] & 0x18f) == 4);
    assert(!w202_typec_start_toggling(&tcpc, TYPEC_PORT_SRC, TYPEC_CC_RP_DEF));
    assert((regs[0x384] & 0x18f) == 5 && !vbus.on);
    for (i = 1; i <= 3; ++i) {
        calls = 0; fail_at = i; pd.faulted = false;
        assert(w202_typec_start_toggling(&tcpc, TYPEC_PORT_DRP, TYPEC_CC_RD) == -EIO);
        assert(pd.faulted && pd.cc_forced && !pd.lock.held);
        assert((regs[0x3d4] & 0xf000) == 0x5000 && !(regs[0x388] & BIT(9)));
        fail_at = 0;
        assert(w202_typec_set_vbus(&tcpc, true, false) == -ESHUTDOWN);
    }
    puts("PASS: hardware DRP with sink preference, fixed modes and every toggle I/O failure stays disconnected");
    pd.faulted = true;
    assert(!w202_typec_set_cc(&tcpc, TYPEC_CC_OPEN));
    assert((regs[0x3d4] & 0xf000) == 0x5000);
    assert(w202_typec_set_cc(&tcpc, TYPEC_CC_RP_3_0) == -EOPNOTSUPP);
    puts("PASS: failed role writes do not publish attachment; fault recovery can electrically disconnect");
    return 0;
}
