// SPDX-License-Identifier: GPL-2.0-only
/* Execute the production PHY-to-TCPM bridge without target hardware. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define W202_UMP9620_TYPEC_H
#define READ_ONCE(x) (x)
#define SC27XX_TYPEC_STATUS 0x1c
#define SC27XX_TYPEC_DBG1 0x60
#define SC27XX_STATE_MASK 0x1f
#define SC27XX_TYPEC_VBUS_OK 0x100
typedef uint32_t u32;
enum { USB_CHARGER_ABSENT, USB_CHARGER_PRESENT };
enum { UNKNOWN_TYPE, SDP_TYPE, DCP_TYPE, CDP_TYPE };
enum { POWER_SUPPLY_USB_TYPE_UNKNOWN, POWER_SUPPLY_USB_TYPE_SDP,
       POWER_SUPPLY_USB_TYPE_DCP, POWER_SUPPLY_USB_TYPE_CDP };
struct usb_phy { int chg_state, chg_type; };
struct w202_typec { struct usb_phy *usb_phy; u32 *regmap, typec_base; };
struct tcpc_dev { struct w202_typec *pd; };
static struct w202_typec *tcpc_to_w202(struct tcpc_dev *t) { return t->pd; }
static int fail_read;
static int regmap_read(u32 *map, u32 reg, u32 *value)
{ if (fail_read) return -1; *value = map[reg]; return 0; }
static void usb_phy_set_charger_state(struct usb_phy *phy, int state)
{ phy->chg_state = state; if (state == USB_CHARGER_ABSENT) phy->chg_type = UNKNOWN_TYPE; }
#include "../charger.c"

int main(void)
{
    u32 regs[0x100] = { [0x1c] = 2, [0x60] = 0x100 };
    struct usb_phy phy = { USB_CHARGER_PRESENT, DCP_TYPE };
    struct w202_typec pd = { .usb_phy = &phy, .regmap = regs };
    struct tcpc_dev tcpc = { &pd };

    assert(w202_typec_charger_current(&tcpc) == 1500);
    phy.chg_type = CDP_TYPE;
    assert(w202_typec_charger_current(&tcpc) == 1500);
    phy.chg_type = SDP_TYPE;
    assert(w202_typec_charger_current(&tcpc) == 500);
    phy.chg_type = UNKNOWN_TYPE;
    assert(w202_typec_charger_current(&tcpc) == 500);
    phy.chg_type = DCP_TYPE;
    w202_typec_charger_state(&pd, false);
    assert(phy.chg_type == UNKNOWN_TYPE);
    assert(w202_typec_charger_current(&tcpc) == 500);
    w202_typec_charger_state(&pd, true);
    assert(w202_typec_charger_current(&tcpc) == 500);
    phy.chg_type = DCP_TYPE;
    regs[0x60] = 0;
    assert(w202_typec_charger_current(&tcpc) == 0);
    regs[0x60] = 0x100;
    regs[0x1c] = 5;
    assert(w202_typec_charger_current(&tcpc) == 0);
    regs[0x1c] = 2;
    fail_read = 1;
    assert(w202_typec_charger_current(&tcpc) == 0);
    fail_read = 0;
    pd.usb_phy = NULL;
    assert(w202_typec_charger_current(&tcpc) == 500);
    puts("PASS: fresh DCP/CDP budgets, conservative fallback, detach/source and I/O failure");
    return 0;
}
