/*
 * Copyright © 2011 Benjamin Franzke
 * Copyright © 2011 Intel Corporation
 * Copyright © 2021 Collabora, Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <time.h>
#include <limits.h>

#include <wayland-client.h>
#define ARRAY_LENGTH(a) (sizeof(a) / sizeof((a)[0]))
static void *xzalloc(size_t size)
{
	void *p = calloc(1, size);
	if (!p) abort();
	return p;
}
static int os_create_anonymous_file(off_t size)
{
	int fd = memfd_create("w202-touch-test", MFD_CLOEXEC);
	if (fd >= 0 && ftruncate(fd, size) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

#include "xdg-shell-client-protocol.h"

struct seat {
	struct seat *next;
	uint32_t name;
	struct touch *touch;
	struct wl_seat *seat;
	struct wl_touch *wl_touch;
};

struct buffer {
	struct wl_buffer *buffer;
	void *data;
	struct touch *touch;
	struct buffer *next;
	int width, height;
	size_t size;
	bool busy;
	int x1, y1, x2, y2;
};

struct touch {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct xdg_wm_base *wm_base;
	struct wl_shm *shm;
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;
	struct buffer *buffers;
	struct seat *seats;
	uint32_t *canvas;
	struct wl_callback *frame_callback;
	int x1, y1, x2, y2;
	unsigned int commits, releases, motions, peak, active;
	uint32_t last_stats, max_lag_ms;
	struct { bool down; int x, y; } contacts[32];
	int width, height;
	int pending_width, pending_height;
	int init_width, init_height;
	bool running;
	bool wait_for_configure;
	bool needs_buffer_update;
	bool has_argb;
	bool maximized;
	bool fullscreen;
};

static void submit_pending(struct touch *touch);

static void buffer_destroy(struct buffer *buffer)
{
	wl_buffer_destroy(buffer->buffer);
	munmap(buffer->data, buffer->size);
	free(buffer);
}

static void buffer_release(void *data, struct wl_buffer *wl_buffer)
{
	struct buffer *buffer = data;
	buffer->busy = false;
	buffer->touch->releases++;
	submit_pending(buffer->touch);
}
static const struct wl_buffer_listener buffer_listener = { buffer_release };

static struct buffer *
create_shm_buffer(struct touch *touch)
{
	struct wl_shm_pool *pool;
	int fd, size, stride;
	void *data;
	struct buffer *buffer;

	if (touch->width <= 0 || touch->height <= 0 ||
	    touch->width > INT_MAX / 4 ||
	    touch->height > INT_MAX / (touch->width * 4))
		return NULL;
	buffer = xzalloc(sizeof(*buffer));
	stride = touch->width * 4;
	size = stride * touch->height;

	fd = os_create_anonymous_file(size);
	if (fd < 0) {
		fprintf(stderr, "creating a buffer file for %d B failed: %s\n",
			size, strerror(errno));
		exit(1);
	}

	data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		fprintf(stderr, "mmap failed: %s\n", strerror(errno));
		close(fd);
		free(buffer);
		return NULL;
	}

	pool = wl_shm_create_pool(touch->shm, fd, size);
	buffer->buffer =
		wl_shm_pool_create_buffer(pool, 0,
					  touch->width, touch->height, stride,
					  WL_SHM_FORMAT_ARGB8888);

	wl_shm_pool_destroy(pool);

	buffer->data = data;
	buffer->touch = touch;
	buffer->width = touch->width;
	buffer->height = touch->height;
	buffer->size = size;
	buffer->x2 = touch->width;
	buffer->y2 = touch->height;
	wl_buffer_add_listener(buffer->buffer, &buffer_listener, buffer);
	buffer->next = touch->buffers;
	touch->buffers = buffer;
	close(fd);

	return buffer;
}

static void mark_damage(struct touch *touch, int x1, int y1, int x2, int y2)
{
	if (x1 < 0) x1 = 0;
	if (y1 < 0) y1 = 0;
	if (x2 > touch->width) x2 = touch->width;
	if (y2 > touch->height) y2 = touch->height;
	if (x1 >= x2 || y1 >= y2) return;
	if (touch->x1 > x1) touch->x1 = x1;
	if (touch->y1 > y1) touch->y1 = y1;
	if (touch->x2 < x2) touch->x2 = x2;
	if (touch->y2 < y2) touch->y2 = y2;
	for (struct buffer *b = touch->buffers; b; b = b->next) {
		/* Damage coordinates belong to this canvas size. Carrying them
		 * across resize generations can copy beyond a reused SHM buffer.
		 */
		if (b->width != touch->width || b->height != touch->height)
			continue;
		if (b->x1 > x1) b->x1 = x1;
		if (b->y1 > y1) b->y1 = y1;
		if (b->x2 < x2) b->x2 = x2;
		if (b->y2 < y2) b->y2 = y2;
	}
}

static void frame_done(void *data, struct wl_callback *callback, uint32_t time)
{
	struct touch *touch = data;
	wl_callback_destroy(callback);
	touch->frame_callback = NULL;
	if (time - touch->last_stats >= 1000) {
		fprintf(stderr, "TOUCH_STATS time=%u commits=%u releases=%u motions=%u active=%u peak=%u max_input_lag_ms=%u\n",
			time, touch->commits, touch->releases, touch->motions,
			touch->active, touch->peak, touch->max_lag_ms);
		touch->last_stats = time;
	}
	submit_pending(touch);
}
static const struct wl_callback_listener frame_listener = { frame_done };

static void submit_pending(struct touch *touch)
{
	if (!touch->canvas || touch->frame_callback ||
	    touch->x1 >= touch->x2 || touch->y1 >= touch->y2)
		return;
	struct buffer *available = NULL;
	unsigned int count = 0;
	struct buffer **link = &touch->buffers;
	while (*link) {
		struct buffer *b = *link;
		if (b->width != touch->width || b->height != touch->height) {
			if (!b->busy) {
				*link = b->next;
				buffer_destroy(b);
				continue;
			}
		} else {
			count++;
			if (!b->busy) available = b;
		}
		link = &b->next;
	}
	if (!available && count < 3)
		available = create_shm_buffer(touch);
	if (!available) return; /* release callback will retry */
	struct buffer *b = available;
	for (int y = b->y1; y < b->y2; y++)
		memcpy((uint32_t *)b->data + y * touch->width + b->x1,
		       touch->canvas + y * touch->width + b->x1,
		       (b->x2 - b->x1) * sizeof(uint32_t));
	b->x1 = b->width; b->y1 = b->height; b->x2 = b->y2 = 0;
	b->busy = true;
	wl_surface_attach(touch->surface, b->buffer, 0, 0);
	wl_surface_damage(touch->surface, touch->x1, touch->y1,
			  touch->x2 - touch->x1, touch->y2 - touch->y1);
	touch->x1 = touch->width; touch->y1 = touch->height;
	touch->x2 = touch->y2 = 0;
	touch->frame_callback = wl_surface_frame(touch->surface);
	wl_callback_add_listener(touch->frame_callback, &frame_listener, touch);
	wl_surface_commit(touch->surface);
	touch->commits++;
}

static void redraw(void *data)
{
	struct touch *touch = data;
	free(touch->canvas);
	touch->canvas = xzalloc((size_t)touch->width * touch->height * 4);
	uint32_t *pixels = touch->canvas;
	touch->x1 = touch->width;
	touch->y1 = touch->height;
	touch->x2 = touch->y2 = 0;
	for (struct buffer *b = touch->buffers; b; b = b->next) {
		if (b->width == touch->width && b->height == touch->height) {
			b->x1 = b->y1 = 0;
			b->x2 = b->width;
			b->y2 = b->height;
		}
	}
	for (int y = 0; y < touch->height; y++)
		for (int x = 0; x < touch->width; x++)
			pixels[y * touch->width + x] =
				(x % 120 == 0 || y % 120 == 0) ? 0xff385068 : 0xff182838;
	const uint32_t colors[] = {0xffff6060, 0xff60ff80, 0xff6080ff, 0xffffff60, 0xffffffff};
	int cx[] = {touch->width / 8, touch->width * 7 / 8, touch->width / 8,
		    touch->width * 7 / 8, touch->width / 2};
	int cy[] = {touch->height / 8, touch->height / 8, touch->height * 7 / 8,
		    touch->height * 7 / 8, touch->height / 2};
	for (int i = 0; i < 5; i++)
		for (int dy = -45; dy <= 45; dy++)
			for (int dx = -45; dx <= 45; dx++) {
				int r2 = dx * dx + dy * dy;
				int x = cx[i] + dx, y = cy[i] + dy;
				if (x >= 0 && y >= 0 && x < touch->width && y < touch->height &&
				    ((r2 >= 38 * 38 && r2 <= 45 * 45) || dx == 0 || dy == 0))
					pixels[y * touch->width + x] = colors[i];
			}
	mark_damage(touch, 0, 0, touch->width, touch->height);
	fprintf(stderr, "TOUCH_TEST_READY %dx%d fullscreen; release-safe 3 buffers, frame-paced; targets TL=red TR=green BL=blue BR=yellow CENTER=white\n",
		touch->width, touch->height);
	submit_pending(touch);
}

static void
shm_format(void *data, struct wl_shm *wl_shm, uint32_t format)
{
	struct touch *touch = data;

	if (format == WL_SHM_FORMAT_ARGB8888)
		touch->has_argb = true;
}

struct wl_shm_listener shm_listener = {
	shm_format
};


static void
touch_paint(struct touch *touch, int32_t x, int32_t y, int32_t id)
{
	uint32_t c;
	static const uint32_t colors[] = {
		0xffff0000,
		0xffffff00,
		0xff0000ff,
		0xffff00ff,
		0xff00ff00,
		0xff00ffff,
	};

	if (id >= 0 && id < (int32_t) ARRAY_LENGTH(colors))
		c = colors[id];
	else
		c = 0xffffffff;

	if (!touch->canvas) return;
	uint32_t *pixels = touch->canvas;
	for (int dy = -16; dy <= 16; dy++)
		for (int dx = -16; dx <= 16; dx++) {
			int px = x + dx, py = y + dy;
			if (dx * dx + dy * dy <= 256 && px >= 0 && py >= 0 &&
			    px < touch->width && py < touch->height)
				pixels[py * touch->width + px] = c;
		}

	mark_damage(touch, x - 16, y - 16, x + 17, y + 17);
}

static void
touch_handle_down(void *data, struct wl_touch *wl_touch,
		  uint32_t serial, uint32_t time, struct wl_surface *surface,
		  int32_t id, wl_fixed_t x_w, wl_fixed_t y_w)
{
	struct touch *touch = data;
	float x = wl_fixed_to_double(x_w);
	float y = wl_fixed_to_double(y_w);

	fprintf(stderr, "TOUCH_DOWN id=%d x=%.1f y=%.1f time=%u\n", id, x, y, time);
	if (id >= 0 && id < 32) {
		if (!touch->contacts[id].down) touch->active++;
		touch->contacts[id].down = true;
		touch->contacts[id].x = x;
		touch->contacts[id].y = y;
		if (touch->peak < touch->active) touch->peak = touch->active;
	}
	touch_paint(touch, x, y, id);
}

static void
touch_handle_up(void *data, struct wl_touch *wl_touch,
		uint32_t serial, uint32_t time, int32_t id)
{
	struct touch *touch = data;
	if (id >= 0 && id < 32 && touch->contacts[id].down) {
		touch->contacts[id].down = false;
		touch->active--;
	}
	fprintf(stderr, "TOUCH_UP id=%d time=%u\n", id, time);
}

static void
touch_handle_motion(void *data, struct wl_touch *wl_touch,
		    uint32_t time, int32_t id, wl_fixed_t x_w, wl_fixed_t y_w)
{
	struct touch *touch = data;
	int x = wl_fixed_to_int(x_w), y = wl_fixed_to_int(y_w);
	touch->motions++;
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	uint32_t ms = (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
	int32_t lag = ms - time;
	if (lag >= 0 && (uint32_t)lag > touch->max_lag_ms)
		touch->max_lag_ms = lag;
	if (id >= 0 && id < 32 && touch->contacts[id].down) {
		int dx = x - touch->contacts[id].x;
		int dy = y - touch->contacts[id].y;
		int span = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
		int steps = span / 8 + 1;
		for (int i = 1; i < steps; i++)
			touch_paint(touch, touch->contacts[id].x + dx * i / steps,
				    touch->contacts[id].y + dy * i / steps, id);
		touch->contacts[id].x = x;
		touch->contacts[id].y = y;
	}
	touch_paint(touch, x, y, id);
}

static void
touch_handle_frame(void *data, struct wl_touch *wl_touch)
{
	submit_pending(data);
}

static void
touch_handle_cancel(void *data, struct wl_touch *wl_touch)
{
	struct touch *touch = data;
	memset(touch->contacts, 0, sizeof(touch->contacts));
	touch->active = 0;
}

static const struct wl_touch_listener touch_listener = {
	touch_handle_down,
	touch_handle_up,
	touch_handle_motion,
	touch_handle_frame,
	touch_handle_cancel,
};

static void
seat_handle_capabilities(void *data, struct wl_seat *wl_seat,
			 enum wl_seat_capability caps)
{
	struct seat *seat = data;
	struct touch *touch = seat->touch;

	if ((caps & WL_SEAT_CAPABILITY_TOUCH) && !seat->wl_touch) {
		seat->wl_touch = wl_seat_get_touch(wl_seat);
		wl_touch_set_user_data(seat->wl_touch, touch);
		wl_touch_add_listener(seat->wl_touch, &touch_listener, touch);
	} else if (!(caps & WL_SEAT_CAPABILITY_TOUCH) && seat->wl_touch) {
		wl_touch_destroy(seat->wl_touch);
		seat->wl_touch = NULL;
	}
}

static const struct wl_seat_listener seat_listener = {
	seat_handle_capabilities,
};

static void
add_seat(struct touch *touch, uint32_t name, uint32_t version)
{
	struct seat *seat;

	seat = malloc(sizeof *seat);
	assert(seat);

	seat->touch = touch;
	seat->name = name;
	seat->next = touch->seats;
	touch->seats = seat;
	seat->wl_touch = NULL;
	seat->seat = wl_registry_bind(touch->registry, name,
				      &wl_seat_interface, 1);
	wl_seat_add_listener(seat->seat, &seat_listener, seat);
}

static void
handle_xdg_surface_configure(void *data, struct xdg_surface *surface,
			     uint32_t serial)
{
	struct touch *touch = data;

	xdg_surface_ack_configure(surface, serial);

	if (touch->wait_for_configure || touch->needs_buffer_update) {
		/* Apply dimensions and replace the canvas in one callback. Touch
		 * events between toplevel.configure and surface.configure must
		 * continue to use the previous canvas dimensions.
		 */
		touch->width = touch->pending_width;
		touch->height = touch->pending_height;
		if (touch->width <= 0 || touch->height <= 0 ||
		    touch->width > INT_MAX / 4 ||
		    touch->height > INT_MAX / (touch->width * 4)) {
			fprintf(stderr, "invalid surface dimensions %dx%d\n",
				touch->width, touch->height);
			touch->running = false;
			return;
		}
		redraw(touch);
		touch->wait_for_configure = false;
		touch->needs_buffer_update = false;
	}
}

static const struct xdg_surface_listener xdg_surface_listener = {
	handle_xdg_surface_configure,
};

static void
xdg_wm_base_ping(void *data, struct xdg_wm_base *shell, uint32_t serial)
{
	xdg_wm_base_pong(shell, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
	xdg_wm_base_ping,
};

static void
handle_global(void *data, struct wl_registry *registry,
	      uint32_t name, const char *interface, uint32_t version)
{
	struct touch *touch = data;

	if (strcmp(interface, "wl_compositor") == 0) {
		touch->compositor =
			wl_registry_bind(registry, name,
					 &wl_compositor_interface, 1);
	} else if (strcmp(interface, "xdg_wm_base") == 0) {
		touch->wm_base =
			wl_registry_bind(registry, name,
					 &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(touch->wm_base,
					 &wm_base_listener, touch);
	} else if (strcmp(interface, "wl_shm") == 0) {
		touch->shm = wl_registry_bind(registry, name,
					      &wl_shm_interface, 1);
		wl_shm_add_listener(touch->shm, &shm_listener, touch);
	} else if (strcmp(interface, "wl_seat") == 0) {
		add_seat(touch, name, version);
	}
}

static void
handle_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
	struct touch *touch = data;
	for (struct seat **link = &touch->seats; *link;) {
		struct seat *seat = *link;
		if (seat->name != name) {
			link = &seat->next;
			continue;
		}
		*link = seat->next;
		if (seat->wl_touch) wl_touch_destroy(seat->wl_touch);
		wl_seat_destroy(seat->seat);
		free(seat);
		break;
	}
}

static const struct wl_registry_listener registry_listener = {
	handle_global,
	handle_global_remove
};

static void
handle_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel,
		          int32_t width, int32_t height, struct wl_array *states)
{
	struct touch *touch = data;
	uint32_t *p;

	touch->fullscreen = false;
	touch->maximized = false;

	wl_array_for_each(p, states) {
		uint32_t state = *p;
		switch (state) {
		case XDG_TOPLEVEL_STATE_FULLSCREEN:
			touch->fullscreen = true;
			break;
		case XDG_TOPLEVEL_STATE_MAXIMIZED:
			touch->maximized = true;
			break;
		}
	}

	if (width > 0 && height > 0) {
		if (!touch->fullscreen && !touch->maximized) {
			touch->init_width = width;
			touch->init_height = height;
		}
		touch->pending_width = width;
		touch->pending_height = height;
	} else if (!touch->fullscreen && !touch->maximized) {
		touch->pending_width = touch->init_width;
		touch->pending_height = touch->init_height;

	}

	touch->needs_buffer_update = touch->pending_width != touch->width ||
				    touch->pending_height != touch->height;
}

static void
handle_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel)
{
	struct touch *touch = data;
	touch->running = false;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	handle_toplevel_configure,
	handle_toplevel_close,
};

static struct touch *
touch_create(int width, int height)
{
	struct touch *touch;

	touch = calloc(1, sizeof *touch);
	if (touch == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	touch->display = wl_display_connect(NULL);
	assert(touch->display);

	touch->has_argb = false;
	touch->registry = wl_display_get_registry(touch->display);
	wl_registry_add_listener(touch->registry, &registry_listener, touch);
	wl_display_dispatch(touch->display);
	wl_display_roundtrip(touch->display);

	if (!touch->has_argb) {
		fprintf(stderr, "WL_SHM_FORMAT_ARGB32 not available\n");
		exit(1);
	}

	if (!touch->wm_base) {
		fprintf(stderr, "xdg-shell required!\n");
		exit(1);
	}

	touch->init_width = width;
	touch->init_height = height;
	touch->pending_width = width;
	touch->pending_height = height;
	touch->surface = wl_compositor_create_surface(touch->compositor);

	touch->xdg_surface =
		xdg_wm_base_get_xdg_surface(touch->wm_base, touch->surface);
	assert(touch->xdg_surface);

	xdg_surface_add_listener(touch->xdg_surface, &xdg_surface_listener, touch);

	touch->xdg_toplevel = xdg_surface_get_toplevel(touch->xdg_surface);
	assert(touch->xdg_toplevel);
	xdg_toplevel_add_listener(touch->xdg_toplevel,
				  &xdg_toplevel_listener, touch);
	xdg_toplevel_set_title(touch->xdg_toplevel, "W202 Touch Test");
	xdg_toplevel_set_app_id(touch->xdg_toplevel, "w202-touch-test");
	xdg_toplevel_set_fullscreen(touch->xdg_toplevel, NULL);
	touch->wait_for_configure = true;
	touch->needs_buffer_update = false;
	wl_surface_commit(touch->surface);

	touch->running = true;

	return touch;
}

static void destroy_touch(struct touch *touch)
{
	if (touch->frame_callback) wl_callback_destroy(touch->frame_callback);
	for (struct seat *seat = touch->seats; seat;) {
		struct seat *next = seat->next;
		if (seat->wl_touch) wl_touch_destroy(seat->wl_touch);
		wl_seat_destroy(seat->seat);
		free(seat);
		seat = next;
	}
	for (struct buffer *b = touch->buffers; b;) {
		struct buffer *next = b->next;
		buffer_destroy(b);
		b = next;
	}
	free(touch->canvas);
	xdg_toplevel_destroy(touch->xdg_toplevel);
	xdg_surface_destroy(touch->xdg_surface);
	wl_surface_destroy(touch->surface);
	xdg_wm_base_destroy(touch->wm_base);
	wl_shm_destroy(touch->shm);
	wl_compositor_destroy(touch->compositor);
	wl_registry_destroy(touch->registry);
	wl_display_disconnect(touch->display);
	free(touch);
}

int
main(int argc, char **argv)
{
	struct touch *touch;
	int ret = 0;
	int seconds = 0, contacts = 0;
	bool resize = false;
	if (argc > 1) {
		if (argc < 4 || argc > 5 || strcmp(argv[1], "--stress")) {
			fprintf(stderr, "usage: %s [--stress seconds contacts [resize]]\n", argv[0]);
			return 2;
		}
		char *end;
		long duration = strtol(argv[2], &end, 10);
		if (*end || duration < 1 || duration > 600) return 2;
		seconds = duration;
		long count = strtol(argv[3], &end, 10);
		if (*end || count < 1 || count > 10) return 2;
		contacts = count;
		resize = argc == 5;
		if (resize && strcmp(argv[4], "resize")) return 2;
	}

	touch = touch_create(600, 500);

	if (!seconds) {
		while (ret != -1 && touch->running)
			ret = wl_display_dispatch(touch->display);
	} else {
		while (ret != -1 && touch->wait_for_configure && touch->running)
			ret = wl_display_dispatch(touch->display);
		struct timespec start, now;
		clock_gettime(CLOCK_MONOTONIC, &start);
		unsigned int ticks = 0;
		while (ret != -1 && touch->running) {
			clock_gettime(CLOCK_MONOTONIC, &now);
			int64_t elapsed = (int64_t)(now.tv_sec - start.tv_sec) * 1000000000 +
					  now.tv_nsec - start.tv_nsec;
			if (elapsed >= (int64_t)seconds * 1000000000) break;
			if (touch->canvas && touch->width > 80 && touch->height > 80) {
				for (int i = 0; i < contacts; i++) {
					int x = 40 + ((ticks * 13 + i * 331) % (touch->width - 80));
					int y = 40 + ((ticks * 7 + i * 257) % (touch->height - 80));
					touch_paint(touch, x, y, i);
				}
				submit_pending(touch);
			}
			ticks++;
			if (resize && ticks % 120 == 0) {
				if ((ticks / 120) % 2)
					xdg_toplevel_unset_fullscreen(touch->xdg_toplevel);
				else
					xdg_toplevel_set_fullscreen(touch->xdg_toplevel, NULL);
				wl_surface_commit(touch->surface);
			}
			ret = wl_display_roundtrip(touch->display);
			usleep(11111);
		}
		fprintf(stderr, "SYNTHETIC_DRAW_END ticks=%u contacts=%d seconds=%d resize=%u commits=%u releases=%u ret=%d\n",
			ticks, contacts, seconds, resize, touch->commits, touch->releases, ret);
	}

	destroy_touch(touch);
	return ret == -1 ? 1 : 0;
}
