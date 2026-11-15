/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "../panfrost_ums9620_dvfs.h"

struct hardware {
	int uv, slot_uv, index, voltage_error, clock_error, read_error;
	char operations[8];
	unsigned int count;
};

static int voltage_get(void *data)
{
	struct hardware *h = data;

	return h->read_error ? -EIO : h->uv;
}

static int voltage_set(void *data, int uv)
{
	struct hardware *h = data;

	h->operations[h->count++] = 'V';
	h->slot_uv = uv; /* A failed PMIC write can still change the HW slot. */
	if (h->voltage_error)
		return -EIO;
	h->uv = uv;
	return 0;
}

static int index_set(void *data, int index)
{
	struct hardware *h = data;
	int needed = index == 6 ? 750000 : index == 5 ? 700000 : 650000;

	h->operations[h->count++] = 'F';
	/* Check both supply and slot at the instant the frequency changes. */
	assert(h->uv >= needed && h->slot_uv >= needed);
	if (h->clock_error)
		return -ETIMEDOUT;
	h->index = index;
	return 0;
}

static const struct ums_dvfs_ops ops = {
	.get_voltage = voltage_get,
	.set_voltage = voltage_set,
	.set_index = index_set,
};

int main(void)
{
	struct hardware h;
	unsigned int i, index;
	int uv;

	/* Repeat each direction, including direct lowest/highest transitions. */
	h = (struct hardware){ .uv = 650000, .slot_uv = 650000, .index = 2 };
	for (index = 5; index <= 6; index++) {
		uv = index == 5 ? 700000 : 750000;
		for (i = 0; i < 100; i++) {
			h.count = 0;
			memset(h.operations, 0, sizeof(h.operations));
			assert(!ums_dvfs_transition(&ops, &h, index, uv));
			assert(!strcmp(h.operations, "VF"));
			assert(h.uv == uv && h.index == (int)index);
			h.count = 0;
			memset(h.operations, 0, sizeof(h.operations));
			assert(!ums_dvfs_transition(&ops, &h, 2, 650000));
			assert(!strcmp(h.operations, "FV"));
			assert(h.uv == 650000 && h.index == 2);
		}
	}
	/* Raising voltage fails: never attempt the higher clock. */
	h.voltage_error = 1;
	h.count = 0;
	assert(ums_dvfs_transition(&ops, &h, 5, 700000) == -EIO);
	assert(h.count == 1 && h.index == 2 && h.uv == 650000);
	/* Clock write is uncertain: retain the raised voltage. */
	h.voltage_error = 0;
	h.clock_error = 1;
	h.count = 0;
	assert(ums_dvfs_transition(&ops, &h, 5, 700000) == -ETIMEDOUT);
	assert(h.count == 2 && h.uv == 700000 && h.slot_uv == 700000);
	/* A failed downclock must not attempt to reduce the voltage. */
	h.index = 5;
	h.count = 0;
	assert(ums_dvfs_transition(&ops, &h, 2, 650000) == -ETIMEDOUT);
	assert(h.count == 1 && h.uv == 700000);
	/* A failed voltage reduction keeps a confirmed safe lower clock. */
	h.clock_error = 0;
	h.voltage_error = 1;
	h.count = 0;
	assert(ums_dvfs_transition(&ops, &h, 2, 650000) == 1);
	assert(h.index == 2 && h.uv == 700000 && h.slot_uv == 650000);
	/* Retry high frequency at unchanged actual voltage: fix the slot first. */
	h.voltage_error = 0;
	h.count = 0;
	assert(!ums_dvfs_transition(&ops, &h, 5, 700000));
	assert(h.count == 2 && h.operations[0] == 'V' && h.operations[1] == 'F');
	h.read_error = 1;
	h.count = 0;
	assert(ums_dvfs_transition(&ops, &h, 2, 650000) == -EIO);
	assert(!h.count);
	puts("GPU DVFS: 400 transitions and 6 failure/retry cases passed");
	return 0;
}
