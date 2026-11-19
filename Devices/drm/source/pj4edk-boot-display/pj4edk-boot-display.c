// SPDX-License-Identifier: GPL-2.0-only
/* W202DS retained scanout adapter. fbcon owns text, fonts, scroll and logos;
 * only cache maintenance and the inherited DPU present live on this board. */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ktime.h>
#include <linux/libfdt.h>
#include <linux/memblock.h>
#include <linux/module.h>
#include <linux/of_fdt.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <asm/cacheflush.h>
#include "pj4edk-boot-display-core.h"
#include "common/linux/pj4edk2_fbcon.h"

#define PRESENT_RETRY_MS 1
static struct fb_info *boot_fb;
static u32 display_cells[20];
static DEFINE_SPINLOCK(pj4edk_lock);
static bool owned;
static void __iomem *inherited_dpu;
static bool present_inflight, single_run, dirty;
static u32 present_done, dirty_first, dirty_end;
static unsigned long cache_ranges, present_count, present_busy, queue_max_ns;
static u64 queued_ns;
module_param(cache_ranges, ulong, 0444);
module_param(present_count, ulong, 0444);
module_param(present_busy, ulong, 0444);
module_param(queue_max_ns, ulong, 0444);
static void pj4edk_flush_work(struct work_struct *work);
static DECLARE_DELAYED_WORK(pj4edk_work, pj4edk_flush_work);

/* Atomic-safe notification from standard fbcon raster operations. */
static void pj4edk_damage(void *context, unsigned int y, unsigned int height)
{
	unsigned long flags;

	spin_lock_irqsave(&pj4edk_lock, flags);
	if (owned) {
		dirty_first = dirty ? min(dirty_first, y) : y;
		dirty_end = dirty ? max(dirty_end, y + height) : y + height;
		dirty = true;
		if (!queued_ns)
			queued_ns = ktime_get_mono_fast_ns();
		schedule_delayed_work(&pj4edk_work, 0);
	}
	spin_unlock_irqrestore(&pj4edk_lock, flags);
}

static void pj4edk_flush_work(struct work_struct *work)
{
	unsigned long flags, address;
	u32 first, end, ctrl;

	spin_lock_irqsave(&pj4edk_lock, flags);
	if (!owned || !dirty) {
		spin_unlock_irqrestore(&pj4edk_lock, flags);
		return;
	}
	if (present_inflight && !(readl(inherited_dpu + 0x25c) & present_done)) {
		present_busy++;
		schedule_delayed_work(&pj4edk_work, msecs_to_jiffies(PRESENT_RETRY_MS));
		spin_unlock_irqrestore(&pj4edk_lock, flags);
		return;
	}
	first = dirty_first;
	end = dirty_end;
	dirty = false;
	if (queued_ns) {
		queue_max_ns = max_t(unsigned long, queue_max_ns,
			ktime_get_mono_fast_ns() - queued_ns);
		queued_ns = 0;
	}
	spin_unlock_irqrestore(&pj4edk_lock, flags);

	/* Native landscape rows. Scanout rotation stays in the DPU. Cache/MMIO
	 * operations never run inside the fbcon/printk drawing callbacks. */
	address = (unsigned long)phys_to_virt(0xb0000000) + first * 7680;
	dcache_clean_poc(address, address + (end - first) * 7680);
	cache_ranges++;
	writel(present_done | BIT(16) | BIT(18) | BIT(2), inherited_dpu + 0x254);
	ctrl = readl(inherited_dpu + 0x08) & ~BIT(1);
	wmb();
	writel(ctrl | BIT(2) | BIT(4), inherited_dpu + 0x08);
	if (single_run || !(ctrl & BIT(0)))
		writel(readl(inherited_dpu + 0x08) | BIT(0), inherited_dpu + 0x08);
	present_inflight = true;
	present_count++;
}

static bool pj4edk_boot_display_console_active(void)
{
	return READ_ONCE(owned);
}

static void pj4edk_boot_display_console_release(void)
{
	unsigned long flags;

	/* Detach fbcon before revoking the framebuffer and DPU ownership. */
	pj4edk2_fbcon_unregister(boot_fb);
	boot_fb = NULL;
	spin_lock_irqsave(&pj4edk_lock, flags);
	owned = false;
	spin_unlock_irqrestore(&pj4edk_lock, flags);
	cancel_delayed_work_sync(&pj4edk_work);
}

static int pj4edk_boot_display_fb_register(struct device *dev)
{
	boot_fb = pj4edk2_fbcon_register(dev, THIS_MODULE, display_cells,
		phys_to_virt(0xb0000000), 0x9e4000, pj4edk_damage, NULL);
	if (IS_ERR(boot_fb)) {
		int ret = PTR_ERR(boot_fb);

		boot_fb = NULL;
		return ret;
	}
	dev_info(dev, "fbcon boot window %ux%u, standard Linux fonts and logo\n",
		boot_fb->var.xres, boot_fb->var.yres);
	return 0;
}

static int __init pj4edk_console_init(void)
{
	const void *property;
	const __be32 *reg;
	struct memblock_region *r;
	u32 *v = display_cells;
	int chosen, logo, length;
	bool reserved = false;
	void __iomem *dpu;

	if (!initial_boot_params)
		return 0;
	chosen = fdt_path_offset(initial_boot_params, "/chosen");
	if (chosen < 0)
		return 0;
	property = fdt_getprop(initial_boot_params, chosen, PJ4EDK2_DISPLAY_PROPERTY, &length);
	if (!property || !pj4edk2_display_decode_be(property, length, v) ||
	    !pj4edk_boot_display_layout(v))
		return 0;
	logo = fdt_path_offset(initial_boot_params, "/reserved-memory/logobuffer@b0000000");
	if (logo < 0 || fdt_getprop(initial_boot_params, logo, "no-map", NULL) ||
	    fdt_getprop(initial_boot_params, logo, "reusable", NULL))
		return 0;
	reg = fdt_getprop(initial_boot_params, logo, "reg", &length);
	if (!reg || length != 16 || be32_to_cpu(reg[0]) ||
	    be32_to_cpu(reg[1]) != 0xb0000000 || be32_to_cpu(reg[2]) ||
	    be32_to_cpu(reg[3]) != 0x9e4000 ||
	    !memblock_is_region_memory(0xb0000000, 0x9e4000) ||
	    !memblock_is_map_memory(0xb0000000))
		return 0;
	for_each_reserved_mem_region(r)
		if (r->base <= 0xb0000000 && r->size >= 0xb09e4000 - r->base)
			reserved = true;
	if (!reserved)
		return 0;
	dpu = ioremap(0x31000000, 0x1000);
	if (!dpu)
		return -ENOMEM;
	if (readl(dpu) != 0x600 || readl(dpu + 0x18) != 0x078004b0 ||
	    readl(dpu + 0x2c) != 1 || readl(dpu + 0x30) != 0xb0000000 ||
	    (readl(dpu + 0x40) & 0x007000f0) != 0x00100030 || readl(dpu + 0x4c) != 1920) {
		iounmap(dpu);
		return 0;
	}
	inherited_dpu = dpu;
	single_run = (readl(dpu + 0x0c) & (BIT(0) | BIT(1))) != 0;
	present_done = single_run ? BIT(0) : BIT(18);
	owned = true;
	return 0;
}
subsys_initcall(pj4edk_console_init);

#include "pj4edk-boot-display-clocks.h"
