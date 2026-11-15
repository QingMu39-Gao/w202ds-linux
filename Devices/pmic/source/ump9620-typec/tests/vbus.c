// SPDX-License-Identifier: GPL-2.0-only
/* Delayed comparator edges must reach TCPM without a new CC interrupt. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#define W202_UMP9620_TYPEC_H
#define READ_ONCE(x) (x)
#define container_of(p, type, member) ((type *)((char *)(p) - offsetof(type, member)))
#define to_delayed_work(p) container_of(p, struct delayed_work, work)
#define SC27XX_TYPEC_DBG1 0x60
#define SC27XX_TYPEC_VBUS_OK 0x100
#define system_power_efficient_wq 0
#define msecs_to_jiffies(ms) (ms)
#define INIT_DELAYED_WORK(w, cb) ((w)->callback = (cb))
typedef uint32_t u32;
struct work_struct { int unused; };
struct delayed_work { struct work_struct work; void (*callback)(struct work_struct *); };
struct mutex { bool held; };
struct w202_typec {
	struct mutex lock;
	bool ready, faulted;
	struct delayed_work vbus_work;
	u32 *regmap, typec_base;
	int vbus_seen;
	struct w202_typec *port;
};
static int fail_read, reads, events, resets, schedules;
static void mutex_lock(struct mutex *m) { assert(!m->held); m->held = true; }
static void mutex_unlock(struct mutex *m) { assert(m->held); m->held = false; }
static int regmap_read(u32 *map, u32 reg, u32 *value)
{ assert(reg == 0x3e0); ++reads; *value = *map; return fail_read; }
static void w202_pd_latch_fault(struct w202_typec *pd, const char *reason)
{ assert(pd->lock.held && reason); pd->faulted = true; }
static void tcpm_vbus_change(struct w202_typec *pd)
{ assert(!pd->lock.held); ++events; }
static void tcpm_tcpc_reset(struct w202_typec *pd)
{ assert(!pd->lock.held); ++resets; }
static void mod_delayed_work(int queue, struct delayed_work *work, unsigned int delay)
{ (void)queue; assert(work && delay == 100); ++schedules; }
#include "../vbus.c"

int main(void)
{
	u32 debug = 0;
	struct w202_typec pd = { .regmap = &debug, .typec_base = 0x380 };
	pd.port = &pd;
	w202_typec_vbus_init(&pd);
	assert(pd.vbus_seen == -1);
	pd.vbus_work.callback(&pd.vbus_work.work);
	assert(!reads && !events && !schedules);
	pd.ready = true;
	pd.vbus_work.callback(&pd.vbus_work.work);
	assert(reads == 1 && events == 1 && pd.vbus_seen == 0);
	debug = SC27XX_TYPEC_VBUS_OK;
	pd.vbus_work.callback(&pd.vbus_work.work);
	assert(events == 2 && pd.vbus_seen == 1);
	pd.vbus_work.callback(&pd.vbus_work.work);
	assert(events == 2);
	debug = 0;
	pd.vbus_work.callback(&pd.vbus_work.work);
	assert(events == 3 && pd.vbus_seen == 0);
	fail_read = -EIO;
	pd.vbus_work.callback(&pd.vbus_work.work);
	assert(pd.faulted && resets == 1 && events == 3);
	pd.vbus_work.callback(&pd.vbus_work.work);
	assert(resets == 1 && events == 3 && schedules == 6);
	pd.ready = false;
	pd.vbus_work.callback(&pd.vbus_work.work);
	assert(schedules == 6);
	puts("PASS: delayed VBUS rise/fall, no duplicate edges, I/O faults, bounded reset and stopped worker");
	return 0;
}
