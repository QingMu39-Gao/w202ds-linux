// SPDX-License-Identifier: GPL-2.0-only
/* Native DRM boot window. fbcon remains the single Linux text renderer. */
#include <linux/io.h>
#include <linux/iosys-map.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <drm/drm_client.h>
#include <drm/drm_connector.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_print.h>
#include "w202_r6p0.h"
#include "w202_boot_text.h"
#include "common/linux/pj4edk2_fbcon.h"

struct w202_boot_console {
	struct drm_client_dev client;
	struct drm_client_buffer *buffer;
	struct iosys_map map;
	struct fb_info *fb;
	u32 cells[20];
	void *inherited;
	bool captured, committed;
};

static void w202_boot_stop(struct w202_boot_console *b)
{
	pj4edk2_fbcon_unregister(b->fb);
	b->fb = NULL;
}

static void w202_boot_unregister(struct drm_client_dev *client)
{
	struct w202_boot_console *b = container_of(client, struct w202_boot_console, client);

	w202_boot_stop(b);
	if (b->inherited)
		memunmap(b->inherited);
	drm_client_buffer_vunmap(b->buffer);
	drm_client_framebuffer_delete(b->buffer);
	drm_client_release(client);
	kvfree(b);
}

static const struct drm_client_funcs w202_boot_client_funcs = {
	.owner = THIS_MODULE,
	.unregister = w202_boot_unregister,
};

int w202_boot_console_prepare(struct w202_dpu *d)
{
	struct device_node *chosen;
	struct w202_boot_console *b;
	struct pj4edk2_fbcon_viewport view;
	u32 v[20];
	int ret;

	if (d->boot_console)
		return 0;
	chosen = of_find_node_by_path("/chosen");
	if (!chosen)
		return 0;
	if (!of_find_property(chosen, PJ4EDK2_DISPLAY_PROPERTY, NULL)) {
		of_node_put(chosen);
		return 0;
	}
	ret = of_property_count_u32_elems(chosen, PJ4EDK2_DISPLAY_PROPERTY);
	if (ret == ARRAY_SIZE(v))
		ret = of_property_read_u32_array(chosen, PJ4EDK2_DISPLAY_PROPERTY, v, ARRAY_SIZE(v));
	else
		ret = -EINVAL;
	of_node_put(chosen);
	if (ret || !w202_boot_viewport_select(&view, v, ARRAY_SIZE(v))) {
		drm_warn(&d->drm, "invalid optional boot-display layout; using normal fbdev\n");
		return 0;
	}
	b = kvzalloc(sizeof(*b), GFP_KERNEL);
	if (!b)
		return -ENOMEM;
	memcpy(b->cells, v, sizeof(v));
	b->inherited = memremap(0xb0000000, 9216000, MEMREMAP_WB);
	if (!b->inherited) {
		ret = -ENOMEM;
		goto free;
	}
	ret = drm_client_init(&d->drm, &b->client, "w202-boot-fbcon", &w202_boot_client_funcs);
	if (ret)
		goto unmap;
	b->buffer = drm_client_framebuffer_create(&b->client, 1920, 1200, DRM_FORMAT_XRGB8888);
	if (IS_ERR(b->buffer)) {
		ret = PTR_ERR(b->buffer);
		goto release;
	}
	ret = drm_client_buffer_vmap(b->buffer, &b->map);
	if (ret)
		goto delete;
	if (b->buffer->pitch < 7680 || b->map.is_iomem) {
		ret = -EINVAL;
		drm_client_buffer_vunmap(b->buffer);
		goto delete;
	}
	/* This is a new XRGB8888 allocation, regardless of the inherited GOP
	 * channel order. The board's accepted geometry remains unchanged. */
	b->cells[8] = b->buffer->pitch;
	b->cells[4] = b->cells[5] = b->buffer->pitch * 1200;
	b->cells[9] = 2;
	d->boot_console = b;
	drm_client_register(&b->client);
	return 0;
delete:
	drm_client_framebuffer_delete(b->buffer);
release:
	drm_client_release(&b->client);
unmap:
	memunmap(b->inherited);
free:
	kvfree(b);
	return ret;
}

void w202_boot_console_capture(struct w202_dpu *d)
{
	struct w202_boot_console *b = d->boot_console;
	unsigned int y;

	/* Aperture removal has detached old fbcon and drained its DPU worker.
	 * Preserve the whole inherited frame before native scanout starts. */
	if (!b || b->captured)
		return;
	for (y = 0; y < 1200; y++)
		iosys_map_memcpy_to(&b->map, y * b->buffer->pitch,
				   b->inherited + y * 7680, 7680);
	wmb();
	memunmap(b->inherited);
	b->inherited = NULL;
	b->captured = true;
}

int w202_boot_console_start(struct w202_dpu *d)
{
	struct w202_boot_console *b = d->boot_console;
	u64 timeouts = d->latch_timeouts, updates = d->updates;
	struct drm_mode_set *set;
	int ret;

	if (b->committed && b->fb)
		return 0;
	if (!b->captured)
		return -EINVAL;
	if (!b->committed) {
		ret = drm_client_modeset_probe(&b->client, 1920, 1200);
		if (ret)
			return ret;
		mutex_lock(&b->client.modeset_mutex);
		drm_client_for_each_modeset(set, &b->client) {
			if (set->num_connectors)
				set->fb = b->buffer->fb;
		}
		mutex_unlock(&b->client.modeset_mutex);
		ret = drm_client_modeset_commit(&b->client);
		if (ret)
			return ret;
		if (d->latch_timeouts != timeouts || d->updates == updates)
			return -ETIMEDOUT;
		b->committed = true;
	}
	b->fb = pj4edk2_fbcon_register(d->drm.dev, THIS_MODULE, b->cells,
		b->map.vaddr, b->buffer->pitch * 1200, NULL, NULL);
	if (IS_ERR(b->fb)) {
		ret = PTR_ERR(b->fb);
		b->fb = NULL;
		return ret;
	}
	pr_info("w202-drm: native fbcon boot window %ux%u; full screen awaits login\n",
		b->fb->var.xres, b->fb->var.yres);
	return 0;
}

void w202_boot_console_stop(struct w202_dpu *d)
{
	if (d->boot_console)
		w202_boot_stop(d->boot_console);
}

bool w202_boot_console_active(struct w202_dpu *d)
{
	return d->boot_console && READ_ONCE(d->boot_console->fb);
}

ssize_t w202_boot_console_status(struct w202_dpu *d, char *buf)
{
	struct w202_boot_console *b = d->boot_console;

	if (!b)
		return sysfs_emit(buf, "available=0 active=0\n");
	return sysfs_emit(buf, "available=1 active=%u captured=%u committed=%u renderer=fbcon pitch=%u\n",
		!!READ_ONCE(b->fb), b->captured, b->committed, b->buffer->pitch);
}
