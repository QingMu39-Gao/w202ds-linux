// SPDX-License-Identifier: GPL-2.0-only
/* Run production register/OTG sequences, including every I/O failure point. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define W202_BQ_CHARGER_H
#define BIT(n) (1U << (n))
#define GENMASK(h, l) ((~0U >> (31 - (h))) & (~0U << (l)))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define CHARGE_ENABLE BIT(4)
#define OTG_ENABLE BIT(5)
#define WATCHDOG_STROBE BIT(6)
typedef uint8_t u8;
struct i2c_client { u8 regs[256]; bool main; };
struct w202_bq {
    struct i2c_client *client;
    bool otg_active, otg_saved_valid, stop_requested;
    u8 otg_saved[5];
};
static struct i2c_client chip, second;
static struct w202_bq main_bq, slave_bq;
static unsigned int calls, fail_at;
static bool fail_off, enable_seen;
static int i2c_smbus_read_byte_data(struct i2c_client *c, int reg)
{
    if (++calls == fail_at) return -EIO;
    return c->regs[reg];
}
static int i2c_smbus_write_byte_data(struct i2c_client *c, int reg, int val)
{
    if (++calls == fail_at) return -EIO;
    if (c->main && reg == 1 && (val & OTG_ENABLE)) {
        assert(!(val & CHARGE_ENABLE));
        assert(!(second.regs[1] & CHARGE_ENABLE));
        assert(second.regs[0] & 0x80);
        assert(!(chip.regs[2] & 0x80));
        assert((chip.regs[6] & 0x30) == 0x10);
        enable_seen = true;
        if (fail_off) {
            c->regs[reg] = val;
            return -EIO;
        }
    }
    if (fail_off && enable_seen && c->main && reg == 1 && !(val & OTG_ENABLE))
        return -EIO;
    c->regs[reg] = val;
    return 0;
}
static int w202_bq_ovp_bits(const struct w202_bq *bq) { (void)bq; return 0xc0; }
static int w202_bq_update_locked(struct w202_bq *, int, int, int);
#include "../registers.c"
#include "../otg-hardware.c"

static void reset(void)
{
    memset(&chip, 0, sizeof(chip));
    memset(&second, 0, sizeof(second));
    chip.main = true;
    chip.regs[0] = 0x42; chip.regs[1] = 0x10; chip.regs[2] = 0xaa;
    chip.regs[5] = 0x8c; chip.regs[6] = 0xe8;
    second.regs[0] = 0x04; second.regs[1] = 0x10;
    main_bq = (struct w202_bq){ .client = &chip, .stop_requested = true };
    slave_bq = (struct w202_bq){ .client = &second, .stop_requested = true };
    calls = fail_at = 0; fail_off = enable_seen = false;
}
int main(void)
{
    unsigned int count, i;
    reset();
    assert(!w202_bq_otg_enter_locked(&main_bq, &slave_bq));
    count = calls;
    assert(main_bq.otg_active && enable_seen);
    assert(!w202_bq_otg_exit_locked(&main_bq, &slave_bq));
    assert(!main_bq.otg_active && !main_bq.otg_saved_valid);
    assert(chip.regs[0] == 0x42 && chip.regs[2] == 0xaa && chip.regs[6] == 0xe8);
    assert(second.regs[0] == 0x04);
    assert(!(chip.regs[1] & 0x30) && !(second.regs[1] & 0x30));
    assert(main_bq.stop_requested && slave_bq.stop_requested);
    puts("PASS: both chargers off before 5 V/500 mA boost, isolation restored, user inhibit preserved");
    for (i = 1; i <= count; ++i) {
        reset(); fail_at = i;
        assert(w202_bq_otg_enter_locked(&main_bq, &slave_bq) < 0);
        assert(!(chip.regs[1] & OTG_ENABLE) && !(second.regs[0] & 0x80));
        assert(!main_bq.otg_active);
    }
    printf("PASS: all %u enable I/O failure points leave boost off\n", count);
    reset(); fail_off = true;
    assert(w202_bq_otg_enter_locked(&main_bq, &slave_bq) < 0);
    assert(main_bq.otg_active && main_bq.otg_saved_valid && (second.regs[0] & 0x80));
    fail_off = false;
    assert(!w202_bq_otg_exit_locked(&main_bq, &slave_bq));
    assert(!main_bq.otg_active && !(second.regs[0] & 0x80));
    puts("PASS: unconfirmed boost-off retains slave isolation until recovery succeeds");
    reset(); second.regs[0] |= 0x80;
    assert(!w202_bq_otg_enter_locked(&main_bq, &slave_bq));
    assert(!w202_bq_otg_exit_locked(&main_bq, &slave_bq));
    assert(second.regs[0] & 0x80);
    puts("PASS: an already-isolated slave retains its original HIZ state");
    return 0;
}
