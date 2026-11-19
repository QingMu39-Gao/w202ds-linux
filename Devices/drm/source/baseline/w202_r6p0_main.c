// SPDX-License-Identifier: GPL-2.0-only
/* W202DS r6p0 fixed-panel DRM/KMS with Linux DSI/DPHY initialization.
 * DRM fbdev owns the console framebuffer. Modeset disable enters panel
 * standby, and enable wakes it without resetting shared Himax touch firmware.
 * No unload: preserve platform and display clock ownership until reboot.
 */
#include <linux/backlight.h>
#include <linux/aperture.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/dma-buf.h>
#include <linux/interrupt.h>
#include <linux/ktime.h>
#include <linux/log2.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_blend.h>
#include <drm/drm_connector.h>
#include <drm/clients/drm_client_setup.h>
#include <drm/drm_fbdev_dma.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_prime.h>
#include <drm/drm_rect.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_vblank.h>
#include "w202_r6p0.h"
#include "w202_layers.h"

#define CTRL 0x08
#define LAYER_ENABLE 0x2c
#define INT_EN 0x250
#define INT_CLR 0x254
#define INT_STS 0x258
#define VSYNC BIT(4)
#define LAY_DONE BIT(18)
#define W202_LATCH BIT(4)
#define W202_ERROR_IRQS (BIT(2) | BIT(6) | BIT(7) | BIT(8))
#define W202_CURSOR_SIZE 64
static bool flip_after_latch = true;
static bool defer_modeset = true;
static bool adopt_display = true;
static bool aux_planes;
module_param(aux_planes, bool, 0444);
MODULE_PARM_DESC(aux_planes, "Expose experimental r6p0 overlay and cursor KMS planes (default false)");
module_param(adopt_display, bool, 0444);
MODULE_PARM_DESC(adopt_display, "Check live inherited display before resetting DPU/DSI/panel (default true)");
/* Board installation correction, clockwise in input coordinates. Logical
 * KMS coordinates are already landscape; userspace rotation starts at zero.
 * The native scanout rotation is the inverse of this input correction.
 */
static unsigned int mount_rotation = 270;
module_param(mount_rotation, uint, 0444);
MODULE_PARM_DESC(mount_rotation, "Board input mounting correction (90 or 270; default 270)");
module_param(defer_modeset, bool, 0444);
MODULE_PARM_DESC(defer_modeset, "Register KMS resources now, activate panel/fbdev only after display_enable=1");
module_param(flip_after_latch, bool, 0644);
MODULE_PARM_DESC(flip_after_latch, "Complete pageflip after layer-update IRQ (false retains M13 extra vblank)");
static struct w202_dpu *to_w202(struct drm_device *drm)
{ return container_of(drm, struct w202_dpu, drm); }
static irqreturn_t w202_irq(int irq, void *data);

static ssize_t dsi_control_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct w202_dpu *d = dev_get_drvdata(dev);

	ssize_t ret;

	mutex_lock(&d->hw_lock);
	ret = w202_dsi_status(d, buf);
	mutex_unlock(&d->hw_lock);
	return ret;
}

static ssize_t dsi_control_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct w202_dpu *d = dev_get_drvdata(dev);
	int ret;

	if (!sysfs_streq(buf, "start"))
		return -EINVAL;
	mutex_lock(&d->hw_lock);
	ret = d->display_ready ? w202_dsi_start(d) : -EBUSY;
	mutex_unlock(&d->hw_lock);
	return ret ? ret : count;
}
static DEVICE_ATTR_RW(dsi_control);

static ssize_t scanout_status_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct w202_dpu *d = dev_get_drvdata(dev);
	ssize_t ret;

	mutex_lock(&d->hw_lock);
	ret = sysfs_emit(buf, "initialized=%u dma=%08x layer=%08x pitch=%u enabled=%08x commits=%llu updates=%llu vsyncs=%llu errors=%llu last_error=%08x latch_last_us=%llu latch_max_us=%llu latch_timeouts=%llu flip_after_latch=%u\n",
		d->hw_initialized, readl(d->regs + 0x30), readl(d->regs + 0x40),
		readl(d->regs + 0x4c), readl(d->regs + LAYER_ENABLE),
		d->commits, d->updates, d->vsyncs, d->errors, d->last_error_status,
		d->latch_last_us, d->latch_max_us, d->latch_timeouts, flip_after_latch);
	mutex_unlock(&d->hw_lock);
	return ret;
}
static DEVICE_ATTR_RO(scanout_status);

/* Generic deferred display activation. This code has no boot-handoff API. */
static int w202_prepare_display(struct w202_dpu *d)
{
	int ret;

	lockdep_assert_held(&d->hw_lock);
	if (d->display_ready)
		return 0;
	/* No clocks or display registers are changed by deferred probe. */
	while (d->clocks_enabled < ARRAY_SIZE(d->clks)) {
		ret = clk_prepare_enable(d->clks[d->clocks_enabled]);
		if (ret)
			return ret;
		d->clocks_enabled++;
	}
	ret = w202_dsi_claim_clocks(d);
	if (ret)
		return ret;
	/* Hold every scanout clock before the generic firmware owner drops its
	 * references. This also drains its writer before we touch display MMIO.
	 * No handoff-specific header, symbol or build dependency is required.
	 */
	ret = aperture_remove_conflicting_devices(0xb0000000, 0x9e4000, "w202-r6p0");
	if (ret)
		return ret;
	w202_boot_console_capture(d);
	if (!d->irq_requested) {
		writel(0, d->regs + INT_EN);
		writel(~0u, d->regs + INT_CLR);
		ret = devm_request_irq(d->dev, d->irq, w202_irq, 0,
				       "w202-r6p0", d);
		if (ret)
			return ret;
		d->irq_requested = true;
		writel(d->irq_mask, d->regs + INT_EN);
	}
	/* Keep the live panel and its last frame when the complete hardware
	 * state is compatible. A failed check has not changed DSI or the panel.
	 */
	ret = adopt_display ? w202_dsi_adopt(d) : -EOPNOTSUPP;
	if (!ret) {
		WRITE_ONCE(d->display_ready, true);
		return 0;
	}
	drm_info(&d->drm, "display handoff: full initialization required (%d)\n", ret);
	ret = backlight_disable(d->backlight);
	if (!ret)
		ret = w202_dsi_start(d);
	if (!ret)
		WRITE_ONCE(d->display_ready, true);
	return ret;
}

static int w202_activate_display(struct w202_dpu *d)
{
	bool start = false;
	int ret;

	/* Serialize setup too: its KMS callbacks acquire hw_lock themselves. */
	mutex_lock(&d->activation_lock);
	ret = w202_boot_console_prepare(d);
	if (ret)
		goto out;
	mutex_lock(&d->hw_lock);
	WRITE_ONCE(d->scanout_allowed, true);
	ret = w202_prepare_display(d);
	if (!ret && !d->boot_console && !d->fbdev_started) {
		d->fbdev_started = true;
		start = true;
	}
	mutex_unlock(&d->hw_lock);
	if (!ret && d->boot_console && !d->fbdev_started)
		ret = w202_boot_console_start(d);
	if (ret)
		drm_err(&d->drm, "deferred panel initialization failed: %d\n", ret);
	if (start)
		drm_client_setup_with_color_mode(&d->drm, 16);
out:
	mutex_unlock(&d->activation_lock);
	return ret;
}

static ssize_t boot_console_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", w202_boot_console_active(dev_get_drvdata(dev)));
}

static ssize_t boot_console_store(struct device *dev,
				 struct device_attribute *attr, const char *buf, size_t count)
{
	struct w202_dpu *d = dev_get_drvdata(dev);
	bool value = false;
	/* Stop the boot writer without starting fbdev before a greeter paints. */
	bool preserve = sysfs_streq(buf, "preserve");
	int ret = preserve ? 0 : kstrtobool(buf, &value);

	if (ret || value)
		return ret ? ret : -EINVAL;
	mutex_lock(&d->activation_lock);
	if (!d->display_ready) {
		ret = -EAGAIN;
		goto out;
	}
	w202_boot_console_stop(d);
	if (!preserve && !d->fbdev_started) {
		d->fbdev_started = true;
		drm_client_setup_with_color_mode(&d->drm, 16);
	}
out:
	mutex_unlock(&d->activation_lock);
	return ret ? ret : count;
}
static DEVICE_ATTR_RW(boot_console);

static ssize_t boot_console_status_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return w202_boot_console_status(dev_get_drvdata(dev), buf);
}
static DEVICE_ATTR_RO(boot_console_status);

static ssize_t display_enable_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct w202_dpu *d = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", READ_ONCE(d->scanout_allowed));
}

static ssize_t display_enable_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct w202_dpu *d = dev_get_drvdata(dev);
	bool allow;
	int ret = kstrtobool(buf, &allow);

	if (ret)
		return ret;
	mutex_lock(&d->hw_lock);
	if (!allow && d->scanout_allowed) {
		mutex_unlock(&d->hw_lock);
		return -EPERM;
	}
	mutex_unlock(&d->hw_lock);
	ret = allow ? w202_activate_display(d) : 0;
	return ret ? ret : count;
}
static DEVICE_ATTR_RW(display_enable);

static ssize_t display_ready_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct w202_dpu *d = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", READ_ONCE(d->display_ready));
}
static DEVICE_ATTR_RO(display_ready);

static struct attribute *w202_attrs[] = {
	&dev_attr_boot_console.attr,
	&dev_attr_boot_console_status.attr,
	&dev_attr_display_enable.attr,
	&dev_attr_display_ready.attr,
	&dev_attr_dsi_control.attr,
	&dev_attr_scanout_status.attr,
	NULL,
};
static const struct attribute_group w202_group = { .attrs = w202_attrs };

static irqreturn_t w202_irq(int irq, void *data)
{
	struct w202_dpu *d = data;
	u32 status = readl(d->regs + INT_STS);
	if (!status) return IRQ_NONE;
	writel(status, d->regs + INT_CLR);
	if (status & W202_ERROR_IRQS) {
		d->errors++;
		d->last_error_status = status;
		drm_err_ratelimited(&d->drm, "scanout error IRQ=%08x\n", status);
	}
	if (status & BIT(0)) complete(&d->stopped);
	if (status & BIT(16)) complete(&d->configured);
	if (status & VSYNC) { d->vsyncs++; drm_crtc_handle_vblank(&d->pipe.crtc); }
	/* Update DRM's sequence before waking the commit when both status
	 * bits arrive together, so the event describes the latched frame. */
	if (status & LAY_DONE) { d->updates++; complete(&d->updated); }
	return IRQ_HANDLED;
}
static bool w202_latch(struct w202_dpu *d)
{
	u64 start = ktime_get_ns();
	bool done;
	/* The layer shadow update is CTRL[4], with IRQ[18], NOT CTRL[3]. */
	reinit_completion(&d->updated);
	writel(LAY_DONE, d->regs + INT_CLR);
	writel(readl(d->regs + CTRL) | W202_LATCH, d->regs + CTRL);
	done = wait_for_completion_timeout(&d->updated, msecs_to_jiffies(150));
	d->latch_last_us = div_u64(ktime_get_ns() - start, 1000);
	d->latch_max_us = max(d->latch_max_us, d->latch_last_us);
	if (!done) {
		d->latch_timeouts++;
		drm_err(&d->drm, "layer latch timeout: raw=%08x ctrl=%08x\n",
			readl(d->regs + 0x25c), readl(d->regs + CTRL));
	}
	return done;
}
/* Expose the installed panel rectangle without changing electrical timings. */
static void w202_logical_mode(struct drm_display_mode *dst,
                             const struct drm_display_mode *src)
{
    drm_mode_copy(dst, src);
    swap(dst->hdisplay, dst->vdisplay);
    swap(dst->hsync_start, dst->vsync_start);
    swap(dst->hsync_end, dst->vsync_end);
    swap(dst->htotal, dst->vtotal);
    drm_mode_set_name(dst);
}
static enum drm_mode_status w202_mode_valid(struct drm_simple_display_pipe *pipe,
                                          const struct drm_display_mode *mode)
{
    struct w202_dpu *d = to_w202(pipe->crtc.dev);
    struct drm_display_mode logical;
    unsigned int i;
    for (i = 0; i < ARRAY_SIZE(d->modes); i++) {
        w202_logical_mode(&logical, &d->modes[i]);
        if (drm_mode_equal(mode, &logical)) return MODE_OK;
    }
    return MODE_BAD;
}
static int w202_check(struct drm_simple_display_pipe *pipe,
		     struct drm_plane_state *plane, struct drm_crtc_state *crtc)
{
	struct drm_framebuffer *fb = plane->fb;
	struct w202_dpu *d = to_w202(pipe->crtc.dev);
	bool quarter_turn = plane->rotation & (DRM_MODE_ROTATE_90 | DRM_MODE_ROTATE_270);
	u32 width = quarter_turn ? 1200 : 1920;
	u32 height = quarter_turn ? 1920 : 1200;

	if (!READ_ONCE(d->scanout_allowed))
		return -EBUSY;
	if (fb && !READ_ONCE(d->display_ready))
		return -EAGAIN;
	if (!fb) return 0;
	if (!is_power_of_2(plane->rotation) ||
	    !(plane->rotation & DRM_MODE_ROTATE_MASK))
		return -EINVAL;
	if (plane->src_x || plane->src_y || fb->offsets[0] ||
	    fb->width != width || fb->height != height ||
	    plane->src_w != width << 16 || plane->src_h != height << 16 ||
	    plane->crtc_x || plane->crtc_y ||
	    plane->crtc_w != 1920 || plane->crtc_h != 1200 ||
	    fb->pitches[0] % fb->format->cpp[0] ||
	    fb->pitches[0] / fb->format->cpp[0] > 0x7fff)
		return -EINVAL;
	return 0;
}
static int w202_atomic_check(struct drm_device *drm,
			     struct drm_atomic_state *state)
{
	struct w202_dpu *d = to_w202(drm);

	/* Also reject disable-only commits which do not call plane .check. */
	if (!READ_ONCE(d->scanout_allowed) || !READ_ONCE(d->display_ready))
		return -EBUSY;
	if (aux_planes) {
		struct drm_plane *planes[] = { &d->pipe.plane, &d->overlay, &d->cursor };
		struct drm_crtc_state *crtc_state;
		struct drm_plane_state *plane_state;
		unsigned int i;

		/* One hardware latch covers all layers. Duplicate even untouched
		 * layer states so the commit tail never reads a later commit's state.
		 */
		crtc_state = drm_atomic_get_crtc_state(state, &d->pipe.crtc);
		if (IS_ERR(crtc_state))
			return PTR_ERR(crtc_state);
		for (i = 0; i < ARRAY_SIZE(planes); i++) {
			plane_state = drm_atomic_get_plane_state(state, planes[i]);
			if (IS_ERR(plane_state))
				return PTR_ERR(plane_state);
		}
	}
	return drm_atomic_helper_check(drm, state);
}

static int w202_aux_check(struct drm_plane *plane, struct drm_atomic_state *state)
{
	struct drm_plane_state *ps = drm_atomic_get_new_plane_state(state, plane);
	struct drm_framebuffer *fb = ps->fb;
	struct drm_crtc_state *crtc_state;
	struct drm_gem_dma_object *obj;
	unsigned int cpp;
	int ret;

	if (!ps->crtc || !fb) {
		ps->visible = false;
		return 0;
	}
	crtc_state = drm_atomic_get_new_crtc_state(state, ps->crtc);
	ret = drm_atomic_helper_check_plane_state(ps, crtc_state,
			DRM_PLANE_NO_SCALING, DRM_PLANE_NO_SCALING, true, false);
	if (ret || !ps->visible)
		return ret;

	cpp = fb->format->cpp[0];
	obj = drm_fb_dma_get_gem_obj(fb, 0);
	/* r6p0 takes a 16-byte aligned 32-bit DMA base and integer crop.
	 * Keep the existing display-owned BO restriction for all layers.
	 */
	if (fb->modifier != DRM_FORMAT_MOD_LINEAR || fb->offsets[0] ||
	    !cpp || fb->pitches[0] % cpp || fb->pitches[0] / cpp > 0x7fff ||
	    ((ps->src.x1 | ps->src.y1 | ps->src.x2 | ps->src.y2) & 0xffff) ||
	    !obj || !IS_ALIGNED(obj->dma_addr, 16) || upper_32_bits(obj->dma_addr) ||
	    (u64)obj->dma_addr + (u64)(fb->height - 1) * fb->pitches[0] +
	    (u64)fb->width * cpp > BIT_ULL(32))
		return -EINVAL;
	if (plane->type == DRM_PLANE_TYPE_CURSOR &&
	    (fb->width > W202_CURSOR_SIZE || fb->height > W202_CURSOR_SIZE))
		return -EINVAL;
	return 0;
}

static void w202_aux_update(struct drm_plane *plane, struct drm_atomic_state *state)
{
	/* Required by the DRM helper. The shared commit tail writes and
	 * latches all active layers together, after the modeset is enabled.
	 */
}

static const struct drm_plane_helper_funcs w202_aux_plane_helpers = {
	.prepare_fb = drm_gem_plane_helper_prepare_fb,
	.atomic_check = w202_aux_check,
	.atomic_update = w202_aux_update,
};

static const struct drm_plane_funcs w202_aux_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	.reset = drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
};

static int w202_init_aux_plane(struct w202_dpu *d, struct drm_plane *plane,
			       enum drm_plane_type type, unsigned int layer)
{
	static const u32 rgb[] = { DRM_FORMAT_ARGB8888, DRM_FORMAT_XRGB8888 };
	const unsigned int count = type == DRM_PLANE_TYPE_CURSOR ? 1 : ARRAY_SIZE(rgb);
	int ret;

	ret = drm_universal_plane_init(&d->drm, plane, drm_crtc_mask(&d->pipe.crtc),
			&w202_aux_plane_funcs, rgb, count, NULL, type, NULL);
	if (ret)
		return ret;
	drm_plane_helper_add(plane, &w202_aux_plane_helpers);
	ret = drm_plane_create_zpos_immutable_property(plane, layer);
	if (ret)
		return ret;
	return drm_plane_create_blend_mode_property(plane,
			BIT(DRM_MODE_BLEND_PREMULTI) | BIT(DRM_MODE_BLEND_PIXEL_NONE));
}

static void w202_write_layer(struct w202_dpu *d, unsigned int layer,
			     u32 dma, u32 ctrl, u32 size, u32 pitch, u32 pos, u32 crop)
{
	void __iomem *regs = d->regs + layer * W202_LAY_STRIDE;

	writel(dma, regs + W202_LAY_ADDR);
	writel(ctrl, regs + W202_LAY_CTRL);
	/* The no-scale destination dimensions are expressed before rotation. */
	writel(size, regs + W202_LAY_DST_SIZE);
	writel(size, regs + W202_LAY_SRC_SIZE);
	writel(pitch, regs + W202_LAY_PITCH);
	writel(pos, regs + W202_LAY_POS);
	writel(0xff, regs + W202_LAY_ALPHA);
	writel(crop, regs + W202_LAY_CROP);
}

static bool w202_write_aux(struct w202_dpu *d, struct drm_plane_state *ps,
			   const struct drm_display_mode *mode, unsigned int layer)
{
	struct drm_framebuffer *fb;
	struct drm_gem_dma_object *obj;
	struct drm_rect dst;
	u32 ctrl, rotation, crop, size;

	if (!ps || !ps->visible) {
		writel(0, d->regs + layer * W202_LAY_STRIDE + W202_LAY_CTRL);
		return false;
	}
	fb = ps->fb;
	obj = drm_fb_dma_get_gem_obj(fb, 0);
	rotation = w202_rotation_code(0, mount_rotation);
	ctrl = (rotation << W202_LAY_ROTATION_SHIFT) | W202_LAY_ARGB8888;
	if (fb->format->has_alpha && ps->pixel_blend_mode == DRM_MODE_BLEND_PREMULTI)
		ctrl |= W202_LAY_COMBO_ALPHA | W202_LAY_PREMULT;
	else
		ctrl |= W202_LAY_CONST_ALPHA;
	/* Use the same rotation for the native destination and buffer.
	 * The source crop stays in framebuffer coordinates.
	 */
	dst = ps->dst;
	drm_rect_rotate(&dst, mode->hdisplay, mode->vdisplay,
			DRM_MODE_ROTATE_0 << rotation);
	crop = w202_pair(ps->src.x1 >> 16, ps->src.y1 >> 16);
	size = w202_pair(drm_rect_width(&ps->src) >> 16, drm_rect_height(&ps->src) >> 16);
	w202_write_layer(d, layer, lower_32_bits(obj->dma_addr), ctrl, size,
			 fb->pitches[0] / fb->format->cpp[0],
			 w202_pair(dst.x1, dst.y1), crop);
	return true;
}

static void w202_finish_flip(struct drm_crtc_state *crtc_state, bool latched,
			     bool vblank_ref)
{
	struct drm_crtc *crtc = crtc_state->crtc;
	struct drm_pending_vblank_event *event;
	unsigned long flags;

	spin_lock_irqsave(&crtc->dev->event_lock, flags);
	event = crtc_state->event;
	if (event) {
		crtc_state->event = NULL;
		/* IRQ18 reports the layer shadow update. Report this frame
		 * instead of adding a frame of latency by arming another vblank.
		 */
		if (!vblank_ref || (latched && READ_ONCE(flip_after_latch))) {
			drm_crtc_send_vblank_event(crtc, event);
		} else {
			drm_crtc_arm_vblank_event(crtc, event);
			vblank_ref = false; /* arm transfers the reference */
		}
	}
	spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
	if (vblank_ref)
		drm_crtc_vblank_put(crtc);
}

static void w202_commit_layers(struct w202_dpu *d, struct drm_crtc_state *crtc_state,
			       struct drm_plane_state *state, struct drm_plane_state *overlay,
			       struct drm_plane_state *cursor)
{
	struct drm_crtc *crtc = &d->pipe.crtc;
	struct drm_framebuffer *fb = state ? state->fb : NULL;
	struct drm_gem_dma_object *obj;
	u32 ctrl, rotation, mask = BIT(W202_LAY_PRIMARY);
	bool latched, vblank_ref;
	int ret;
	/* DPMS can retain a framebuffer on an inactive CRTC. Updating that
	 * plane must not wake the panel after the disable callback. */
	if (!READ_ONCE(d->scanout_allowed) || !READ_ONCE(d->display_ready) ||
	    !fb || !state->crtc || !crtc_state->active)
		goto skip;
	obj = drm_fb_dma_get_gem_obj(fb, 0);
	if (WARN_ON_ONCE(!obj || upper_32_bits(obj->dma_addr)))
		goto skip;
	/* Keep vblank accounting active before the latch IRQ. Without this
	 * reference DRM may disable accounting while the desktop is idle,
	 * leaving an immediate event with an old presentation timestamp. */
	vblank_ref = !drm_crtc_vblank_get(crtc);
	mutex_lock(&d->hw_lock);
	ret = w202_dsi_set_power(d, true);
	if (ret) {
		drm_err(&d->drm, "cannot wake panel for commit: %d\n", ret);
		mutex_unlock(&d->hw_lock);
		if (vblank_ref)
			drm_crtc_vblank_put(crtc);
		goto skip;
	}
	/* Compose the KMS request with the same mounting correction. */
	rotation = w202_rotation_code(__ffs(state->rotation & DRM_MODE_ROTATE_MASK),
				      mount_rotation);
	ctrl = (rotation << W202_LAY_ROTATION_SHIFT) | W202_LAY_CONST_ALPHA;
	ctrl |= fb->format->format == DRM_FORMAT_RGB565 ? W202_LAY_RGB565 : W202_LAY_ARGB8888;
	w202_write_layer(d, W202_LAY_PRIMARY, lower_32_bits(obj->dma_addr), ctrl,
			 w202_pair(fb->width, fb->height), fb->pitches[0] / fb->format->cpp[0], 0, 0);
	if (aux_planes) {
		if (w202_write_aux(d, overlay, &crtc_state->mode, W202_LAY_OVERLAY))
			mask |= BIT(W202_LAY_OVERLAY);
		if (w202_write_aux(d, cursor, &crtc_state->mode, W202_LAY_CURSOR))
			mask |= BIT(W202_LAY_CURSOR);
	}
	writel(mask, d->regs + LAYER_ENABLE);
	latched = w202_latch(d);
	if (latched && !d->first_frame_visible) {
		ret = backlight_enable(d->backlight);
		if (!ret)
			d->first_frame_visible = true;
		else
			drm_err(&d->drm, "first framebuffer backlight enable failed: %d\n", ret);
	}
	d->commits++;
	mutex_unlock(&d->hw_lock);
	drm_dbg(&d->drm, "commit=%llu dma=%pad hwaddr=%08x updates=%llu vsyncs=%llu\n",
		d->commits, &obj->dma_addr, readl(d->regs + 0x30), d->updates, d->vsyncs);
	w202_finish_flip(crtc_state, latched, vblank_ref);
	return;

skip:
	if (aux_planes)
		w202_finish_flip(crtc_state, false, false);
}

static void w202_update(struct drm_simple_display_pipe *pipe,
			struct drm_plane_state *old)
{
	if (!aux_planes)
		w202_commit_layers(to_w202(pipe->crtc.dev), pipe->crtc.state,
				   pipe->plane.state, NULL, NULL);
}
static void w202_enable(struct drm_simple_display_pipe *pipe,
		       struct drm_crtc_state *crtc, struct drm_plane_state *plane)
{
	struct w202_dpu *d = to_w202(pipe->crtc.dev);
	int ret;

	if (!READ_ONCE(d->scanout_allowed) || !READ_ONCE(d->display_ready))
		return;
	mutex_lock(&d->hw_lock);
	{
        struct drm_display_mode native;
        w202_logical_mode(&native, &crtc->adjusted_mode);
        ret = w202_dsi_set_mode(d, &native);
    }
	if (!ret)
		ret = w202_dsi_set_power(d, true);
	mutex_unlock(&d->hw_lock);
	if (ret)
		drm_err(&d->drm, "DRM enable failed to wake panel: %d\n", ret);
	drm_crtc_vblank_on(&pipe->crtc);
}
static void w202_disable(struct drm_simple_display_pipe *pipe)
{
	struct w202_dpu *d = to_w202(pipe->crtc.dev);
	struct drm_crtc_state *crtc_state = pipe->crtc.state;
	int ret;

	if (!READ_ONCE(d->scanout_allowed) || !READ_ONCE(d->display_ready))
		return;
	mutex_lock(&d->hw_lock);
	/* An active new CRTC state means a modeset, not DPMS blank.
	 * Preserve the panel and touch firmware; enable updates the timings. */
	ret = crtc_state->active ? 0 : w202_dsi_set_power(d, false);
	if (ret)
		drm_err(&d->drm, "DRM disable failed to put panel in standby: %d\n", ret);
	/* No LK framebuffer restoration: the DRM fbdev client restores its
	 * own DMA allocation when userspace drops DRM master.
	 */
	mutex_unlock(&d->hw_lock);
	drm_crtc_vblank_off(&pipe->crtc);
	/* A disable commit has no plane update to consume its synthetic event.
	 * An active modeset with aux planes completes at the layer latch.
	 */
	if (!aux_planes || !crtc_state->active)
		w202_finish_flip(crtc_state, false, false);
	drm_info(&d->drm, "DRM CRTC disabled: commits=%llu updates=%llu vsyncs=%llu\n",
		d->commits, d->updates, d->vsyncs);
}
static int w202_enable_vblank(struct drm_simple_display_pipe *pipe) { return 0; }
static void w202_disable_vblank(struct drm_simple_display_pipe *pipe) { }
static const struct drm_simple_display_pipe_funcs pipe_funcs = {
	.mode_valid = w202_mode_valid, .check = w202_check,
	.update = w202_update, .enable = w202_enable, .disable = w202_disable,
	.enable_vblank = w202_enable_vblank, .disable_vblank = w202_disable_vblank,
};
static int w202_get_modes(struct drm_connector *connector)
{
 struct w202_dpu *d = to_w202(connector->dev);
 struct drm_display_mode *mode;
 int i;

 for (i = 0; i < ARRAY_SIZE(d->modes); i++) {
  mode = drm_mode_duplicate(connector->dev, &d->modes[i]);
  if (mode) w202_logical_mode(mode, &d->modes[i]);
  if (!mode)
   return -ENOMEM;
  mode->type = DRM_MODE_TYPE_DRIVER;
  if (i == d->preferred_mode)
   mode->type |= DRM_MODE_TYPE_PREFERRED;
  drm_mode_probed_add(connector, mode);
 }
 return ARRAY_SIZE(d->modes);
}
static const struct drm_connector_helper_funcs connector_helpers = { .get_modes = w202_get_modes };
static const struct drm_connector_funcs connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};
static const struct drm_mode_config_funcs config_funcs = {
	.fb_create = drm_gem_fb_create,
	.atomic_check = w202_atomic_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static void w202_commit_aux_planes(struct drm_atomic_state *state)
{
	struct w202_dpu *d = to_w202(state->dev);
	struct drm_crtc_state *crtc_state;

	crtc_state = drm_atomic_get_new_crtc_state(state, &d->pipe.crtc);
	if (!crtc_state)
		return;
	w202_commit_layers(d, crtc_state,
			drm_atomic_get_new_plane_state(state, &d->pipe.plane),
			drm_atomic_get_new_plane_state(state, &d->overlay),
			drm_atomic_get_new_plane_state(state, &d->cursor));
}

static void w202_atomic_commit_tail(struct drm_atomic_state *state)
{
	struct drm_device *dev = state->dev;

	drm_atomic_helper_commit_modeset_disables(dev, state);
	drm_atomic_helper_commit_planes(dev, state, 0);
	drm_atomic_helper_commit_modeset_enables(dev, state);
	if (aux_planes)
		w202_commit_aux_planes(state);
	drm_atomic_helper_fake_vblank(state);
	drm_atomic_helper_commit_hw_done(state);
	/* The layer-update IRQ has already retired the old framebuffer when
	 * update sends its event. A following DPMS commit may stop scanout as
	 * soon as hw_done and flip_done are signalled. Waiting for a fresh
	 * vblank here can therefore time out after the panel has gone to sleep.
	 * Wait for this commit's event instead; this also covers the optional
	 * arm-vblank path and the synthetic event sent by disable.
	 */
	drm_atomic_helper_wait_for_flip_done(dev, state);
	drm_atomic_helper_cleanup_planes(dev, state);
}

static const struct drm_mode_config_helper_funcs config_helpers = {
	.atomic_commit_tail = w202_atomic_commit_tail,
};

static struct drm_gem_object *w202_prime_import(struct drm_device *drm,
					      struct dma_buf *buffer)
{
	/* The VAU currently runs in bypass: scanout requires a contiguous
	 * allocation below 4GiB. kmsro renders into display-allocated BOs,
	 * which may be exported to Panfrost and reimported here unchanged.
	 * Reject foreign buffers before attaching/mapping: attempting to
	 * DMA-map Panfrost's scattered/high pages causes SWIOTLB bounce
	 * allocations even though that memory cannot be scanned out.
	 * Foreign direct scanout needs real VAU/IOMMU support first.
	 */
	if (!drm_gem_is_prime_exported_dma_buf(drm, buffer))
		return ERR_PTR(-EOPNOTSUPP);
	return drm_gem_prime_import(drm, buffer);
}
DEFINE_DRM_GEM_DMA_FOPS(w202_fops);
static const struct drm_driver w202_driver = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	DRM_GEM_DMA_DRIVER_OPS,
	.gem_prime_import = w202_prime_import,
	DRM_FBDEV_DMA_DRIVER_OPS,
	.fops = &w202_fops, .name = "w202-r6p0", .desc = "W202DS M14 latch-complete pageflip",
	.major = 0, .minor = 1,
};
static void w202_put_backlight(void *data)
{
	struct backlight_device *bd = data;

	put_device(&bd->dev);
}

static int w202_probe(struct platform_device *pdev)
{
	static const char * const names[] = { "clk_dpuvsp_eb", "clk_dpuvsp_disp_eb",
		"clk_master_div6_eb", "clk_dpu_core", "clk_dpu_dpi" };
	/* r6p0 uses the same 32-bit format code for ARGB and XRGB. This
	 * opaque primary layer uses constant alpha (BIT(2), alpha=0xff). */
	static const u32 formats[] = {
		DRM_FORMAT_RGB565, DRM_FORMAT_XRGB8888, DRM_FORMAT_ARGB8888,
	};
	struct device_node *np, *pd;
	struct regmap *pmu;
	struct w202_dpu *d;
	u32 args[2], status;
	int i, ret;
	np = of_find_node_by_path("/soc/dpuvsp/dpu@31000000");
	pd = of_find_node_by_path("/soc/mm/power-domain@0");
	if (!np || !pd) { ret = -ENODEV; goto out_nodes; }
	pmu = syscon_regmap_lookup_by_phandle_args(pd, "pmu-pwr-status-syscon", 2, args);
	if (IS_ERR(pmu)) { ret = PTR_ERR(pmu); goto out_nodes; }
	ret = regmap_read(pmu, args[0], &status);
	if (ret) goto out_nodes;
	if (status & args[1]) { ret = -EHOSTDOWN; goto out_nodes; }
	d = devm_drm_dev_alloc(&pdev->dev, &w202_driver, struct w202_dpu, drm);
	if (IS_ERR(d)) { ret = PTR_ERR(d); goto out_nodes; }
	d->dev = &pdev->dev;
	d->backlight = backlight_device_get_by_name("w202-backlight");
	if (!d->backlight) {
		ret = dev_err_probe(&pdev->dev, -EPROBE_DEFER,
			"waiting for backlight provider\n");
		goto out_nodes;
	}
	ret = devm_add_action_or_reset(d->dev, w202_put_backlight, d->backlight);
	if (ret) goto out_nodes;
	platform_set_drvdata(pdev, d);
	init_completion(&d->updated);
	init_completion(&d->stopped);
	init_completion(&d->configured);
	mutex_init(&d->hw_lock);
	mutex_init(&d->activation_lock);
	for (i = 0; i < ARRAY_SIZE(names); i++) {
		d->clks[i] = of_clk_get_by_name(np, names[i]);
		if (IS_ERR(d->clks[i])) { ret = PTR_ERR(d->clks[i]); goto out_nodes; }
	}
	d->regs = of_iomap(np, 0);
	if (!d->regs) { ret = -ENOMEM; goto out_nodes; }
	if (readl(d->regs) != 0x600) { ret = -ENODEV; goto out_nodes; }
	ret = w202_dpu_read_mode(d);
	if (ret) goto out_nodes;
	ret = dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret) goto out_nodes;
	ret = drmm_mode_config_init(&d->drm); if (ret) goto out_nodes;
	d->drm.mode_config.funcs = &config_funcs;
	d->drm.mode_config.helper_private = &config_helpers;
	/* These limits govern ADDFB2 allocation too, including compositor
	 * cursor/probe buffers. Actual scanout dimensions, pitch and rotation
	 * remain constrained by w202_check(); do not impose panel size here.
	 * Match the r6p0 vendor driver's global framebuffer limits. */
	d->drm.mode_config.min_width = d->drm.mode_config.min_height = 0;
	d->drm.mode_config.max_width = d->drm.mode_config.max_height = 8192;
	ret = drm_connector_init(&d->drm, &d->connector, &connector_funcs, DRM_MODE_CONNECTOR_DSI);
	if (ret) goto out_nodes;
	drm_connector_helper_add(&d->connector, &connector_helpers);
	/* Mounting is normalized in KMS and input; no second desktop correction. */
	ret = drm_connector_set_panel_orientation(&d->connector,
		DRM_MODE_PANEL_ORIENTATION_NORMAL);
	if (ret) goto out_nodes;
	d->connector.display_info.width_mm = 216;
	d->connector.display_info.height_mm = 135;
	ret = drm_simple_display_pipe_init(&d->drm, &d->pipe, &pipe_funcs, formats,
		ARRAY_SIZE(formats), NULL, &d->connector);
	if (ret) goto out_nodes;
	ret = drm_plane_create_rotation_property(&d->pipe.plane,
		DRM_MODE_ROTATE_0, DRM_MODE_ROTATE_MASK);
	if (ret) goto out_nodes;
	if (aux_planes) {
		ret = drm_plane_create_zpos_immutable_property(&d->pipe.plane, W202_LAY_PRIMARY);
		if (ret) goto out_nodes;
		ret = w202_init_aux_plane(d, &d->overlay, DRM_PLANE_TYPE_OVERLAY, W202_LAY_OVERLAY);
		if (ret) goto out_nodes;
		ret = w202_init_aux_plane(d, &d->cursor, DRM_PLANE_TYPE_CURSOR, W202_LAY_CURSOR);
		if (ret) goto out_nodes;
		d->pipe.crtc.cursor = &d->cursor;
		d->drm.mode_config.cursor_width = W202_CURSOR_SIZE;
		d->drm.mode_config.cursor_height = W202_CURSOR_SIZE;
	}
	drm_mode_config_reset(&d->drm);
	ret = drm_vblank_init(&d->drm, 1); if (ret) goto out_nodes;
	d->irq = irq_of_parse_and_map(np, 0);
	if (!d->irq) { ret = -EINVAL; goto out_nodes; }
	d->irq_mask = BIT(0) | BIT(16) | VSYNC | LAY_DONE | W202_ERROR_IRQS;
	ret = drm_dev_register(&d->drm, 0);
	if (ret) goto out_nodes;
	if (!defer_modeset) {
		ret = w202_activate_display(d);
		if (ret) {
			if (d->irq_requested)
				writel(0, d->regs + INT_EN);
			drm_dev_unregister(&d->drm);
			goto out_nodes;
		}
	}
	ret = devm_device_add_group(&pdev->dev, &w202_group);
	if (ret) {
		if (d->irq_requested)
			writel(0, d->regs + INT_EN);
		drm_dev_unregister(&d->drm);
		goto out_nodes;
	}
	drm_info(&d->drm, "r6p0 KMS registered: %dx%d %d kHz; display %s\n",
		d->mode.hdisplay, d->mode.vdisplay, d->mode.clock,
		defer_modeset ? "deferred until display_enable=1" : "active");
	ret = 0;
out_nodes:
	of_node_put(np); of_node_put(pd);
	return ret;
}
static struct platform_driver platform_driver = {
	.probe = w202_probe,
	.driver = { .name = "w202-r6p0-lk-dsi", .suppress_bind_attrs = true },
};
static int __init w202_init(void)
{
	struct platform_device *pdev;
	int ret;

	if (mount_rotation != 90 && mount_rotation != 270)
		return -EINVAL;
	ret = platform_driver_register(&platform_driver);
	if (ret) return ret;
	pdev = platform_device_register_simple("w202-r6p0-lk-dsi", -1, NULL, 0);
	if (IS_ERR(pdev)) { platform_driver_unregister(&platform_driver); return PTR_ERR(pdev); }
	/* Backlight registration can happen after initramfs has loaded us.
	 * Keep the device registered so the driver core can retry deferred probe.
	 */
	if (!pdev->dev.driver)
		dev_info(&pdev->dev, "registered; awaiting display providers\n");
	return 0;
}
module_init(w202_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("W202DS M14 latch-complete pageflip, Linux DSI and DRM fbcon");
