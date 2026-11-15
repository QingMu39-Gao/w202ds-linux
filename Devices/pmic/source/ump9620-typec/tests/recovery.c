// SPDX-License-Identifier: GPL-2.0
/* Exercise production reset budgeting and rearm against failing regmap I/O. */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t u32;
#define W202_UMP9620_TYPEC_H
#define HZ 100UL
#define BIT(n) (1U << (n))
#define SC27XX_STATE_MASK 31
#define time_after_eq(a, b) ((long)((a) - (b)) >= 0)
#define lockdep_assert_held(lock) assert(*(lock))
#define SC27XX_TYPEC_PD_CFG 8
#define SC27XX_TYPEC_PD_CONSTRACT 64
#define SC27XX_INT_CLR 44
#define SC27XX_INT_EN 48
#define SC27XX_INT_CLR_MASK 0x3fff
#define SC27XX_INT_EN_MASK 0x85ff
struct w202_typec {
    int lock, regmap;
    unsigned int base, typec_base, nr_source_caps, reset_attempts, reset_window_attempts;
    unsigned long contract_since, reset_window_start;
    bool faulted, contract_valid, tx_pending, reset_window_valid;
    const char *fault_reason;
    uint64_t reset_count, rearm_count;
};
static unsigned long jiffies;
static int calls, fail_at, stop_error;
static int io(void) { return ++calls == fail_at ? -EIO : 0; }
static int regmap_update_bits(int map, unsigned int reg, int mask, int val)
{ (void)map; (void)reg; (void)mask; (void)val; return io(); }
static int regmap_write(int map, unsigned int reg, int value)
{ (void)map; (void)reg; (void)value; return io(); }
static int w202_pd_flush(struct w202_typec *pd, bool ids)
{ assert(pd->lock && ids); return io(); }
static void w202_pd_latch_fault(struct w202_typec *pd, const char *reason)
{ assert(pd->lock); pd->faulted = true; pd->fault_reason = reason; }
static int w202_typec_stop_source(struct w202_typec *pd)
{ assert(pd->lock); return stop_error; }
#include "../recovery.c"
static struct w202_typec fresh(void)
{
    calls = fail_at = stop_error = 0;
    jiffies = 100;
    return (struct w202_typec){ .lock = 1 };
}
int main(void)
{
    struct w202_typec pd = fresh();
    int i, previous;

    assert(!w202_pd_begin_reset(&pd));
    w202_pd_contract_established(&pd);
    jiffies += 59 * HZ;
    assert(!w202_pd_begin_reset(&pd));
    w202_pd_contract_established(&pd);
    assert(w202_pd_begin_reset(&pd) == -ESHUTDOWN);
    assert(pd.faulted && pd.reset_count == 2);
    previous = calls;
    for (i = 0; i < 100; i++)
        assert(w202_pd_begin_reset(&pd) == -ESHUTDOWN);
    assert(calls == previous);
    assert(w202_pd_detached(&pd) == 1);
    assert(!pd.faulted && !pd.fault_reason && !pd.reset_attempts);
    assert(pd.rearm_count == 1 && w202_pd_begin_reset(&pd) == -ESHUTDOWN);
    jiffies += 60 * HZ;
    assert(w202_pd_detached(&pd) == 1 && !w202_pd_begin_reset(&pd));
    puts("PASS: two consecutive resets, detach rearms, independent storm ceiling survives unplug");

    pd = fresh();
    assert(!w202_pd_begin_reset(&pd));
    assert(!w202_pd_begin_reset(&pd));
    w202_pd_contract_established(&pd);
    for (i = 0; i < 15; i++) {
        jiffies += 4 * HZ;
        w202_pd_contract_established(&pd);
    }
    assert(!w202_pd_begin_reset(&pd) && pd.reset_attempts == 1);
    puts("PASS: stable contract refills budget; PPS renewals preserve interval");

    pd = fresh();
    pd.reset_attempts = 2;
    jiffies = ULONG_MAX - 20 * HZ;
    w202_pd_contract_established(&pd);
    jiffies += 60 * HZ;
    assert(!w202_pd_begin_reset(&pd) && pd.reset_attempts == 1);
    puts("PASS: stable-contract interval handles jiffies wrap");

    for (i = 1; i <= 2; i++) {
        pd = fresh(); fail_at = i;
        assert(w202_pd_begin_reset(&pd) == -EIO && pd.faulted);
    }
    for (i = 1; i <= 4; i++) {
        pd = fresh(); pd.faulted = true; pd.fault_reason = "test";
        fail_at = i;
        assert(w202_pd_detached(&pd) == -EIO && pd.faulted);
        assert(pd.rearm_count == 0 && pd.fault_reason);
    }
    puts("PASS: reset/rearm I/O failures retain fault latch");

    pd = fresh(); pd.tx_pending = true; pd.nr_source_caps = 4;
    assert(!w202_pd_detached(&pd) && calls == 2);
    assert(!pd.tx_pending && !pd.nr_source_caps && !pd.contract_valid);
    puts("PASS: normal detach resets stock FIFOs/IDs/contract, not only faulted detach");

    pd = fresh(); pd.faulted = true; pd.fault_reason = "test";
    for (i = 1; i < 32; i++) {
        if (i == 3) continue;
        assert(w202_pd_cc_event(&pd, BIT(1), i) == 0 && pd.faulted);
    }
    assert(w202_pd_cc_event(&pd, BIT(0), 0) == 0 && pd.faulted);
    assert(w202_pd_cc_event(&pd, 0, 0) == 0 && pd.faulted);
    assert(w202_pd_cc_event(&pd, BIT(1), 0x6000) == 1 && !pd.faulted);
    assert(!pd.reset_attempts && pd.rearm_count == 1);
    pd.faulted = true;
    assert(w202_pd_cc_event(&pd, BIT(1), 3) == 1 && !pd.faulted);
    puts("PASS: sink/source detach unlocks; attach/VBUS/other states do not");

    pd = fresh(); pd.faulted = true; stop_error = -EIO;
    assert(w202_pd_cc_event(&pd, BIT(1), 3) == -EIO && pd.faulted);
    assert(!pd.rearm_count && !calls);
    stop_error = 0;
    assert(w202_pd_cc_event(&pd, BIT(1), 3) == 1 && !pd.faulted);
    puts("PASS: detach cannot rearm until source shutdown succeeds");
    return 0;
}
