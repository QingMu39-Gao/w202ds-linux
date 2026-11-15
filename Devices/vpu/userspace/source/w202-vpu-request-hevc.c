// SPDX-License-Identifier: GPL-2.0-only
/* Real Media Request API acceptance client; no private codec ioctl/MMIO. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/dma-buf.h>
#include <linux/media.h>
#include <linux/videodev2.h>
#include <drm/panfrost_drm.h>
#include "hevc-idr-fixture.h"

static int decoder = -1;
static void check(bool ok, const char *message)
{
	if (ok)
		return;
	fprintf(stderr, "Request HEVC error: %s (errno=%d: %s)\n", message, errno, strerror(errno));
	if (decoder >= 0)
		close(decoder);
	exit(1);
}

struct request_buffer {
	enum v4l2_buf_type type;
	enum v4l2_memory memory;
	void *data;
	size_t length;
	int fd;
};

static void cpu_sync(struct request_buffer *buffer, uint64_t flags)
{
	if (buffer->memory == V4L2_MEMORY_DMABUF) {
		struct dma_buf_sync sync = {.flags = flags};

		check(!ioctl(buffer->fd, DMA_BUF_IOCTL_SYNC, &sync), "DMA-BUF CPU sync");
	}
}

static struct request_buffer allocate(enum v4l2_buf_type type, bool dmabuf, size_t size)
{
	struct request_buffer buffer = {
		.type = type, .memory = dmabuf ? V4L2_MEMORY_DMABUF : V4L2_MEMORY_MMAP, .fd = -1,
	};
	struct v4l2_requestbuffers request = {.type = type, .memory = buffer.memory, .count = 1};

	check(!ioctl(decoder, VIDIOC_REQBUFS, &request) && request.count, "REQBUFS");
	if (dmabuf) {
		int render = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
		struct drm_panfrost_create_bo bo = {.size = (size + 4095) & ~(size_t)4095};
		struct drm_prime_handle prime;

		check(render >= 0, "Panfrost render device");
		check(!ioctl(render, DRM_IOCTL_PANFROST_CREATE_BO, &bo), "Panfrost BO");
		prime = (struct drm_prime_handle){.handle = bo.handle, .flags = DRM_CLOEXEC | DRM_RDWR};
		check(!ioctl(render, DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime), "export DMA-BUF");
		close(render);
		buffer.fd = prime.fd;
		buffer.length = bo.size;
		buffer.data = mmap(NULL, buffer.length, PROT_READ | PROT_WRITE,
				   MAP_SHARED, buffer.fd, 0);
	} else {
		struct v4l2_buffer info = {.type = type, .memory = buffer.memory};

		check(!ioctl(decoder, VIDIOC_QUERYBUF, &info), "QUERYBUF");
		buffer.length = info.length;
		buffer.data = mmap(NULL, buffer.length, PROT_READ | PROT_WRITE,
				   MAP_SHARED, decoder, info.m.offset);
	}
	check(buffer.data != MAP_FAILED && buffer.length >= size, "buffer mapping and capacity");
	return buffer;
}

static void enqueue(struct request_buffer *buffer, size_t bytes, int request_fd)
{
	struct v4l2_buffer entry = {
		.type = buffer->type, .memory = buffer->memory, .bytesused = bytes,
		.length = buffer->length, .field = V4L2_FIELD_NONE,
		.timestamp = {.tv_sec = 1},
	};

	if (buffer->memory == V4L2_MEMORY_DMABUF)
		entry.m.fd = buffer->fd;
	if (request_fd >= 0) {
		entry.flags = V4L2_BUF_FLAG_REQUEST_FD;
		entry.request_fd = request_fd;
	}
	check(!ioctl(decoder, VIDIOC_QBUF, &entry), "QBUF");
}

static struct v4l2_buffer dequeue(struct request_buffer *buffer)
{
	struct v4l2_buffer entry = {.type = buffer->type, .memory = buffer->memory};

	check(!ioctl(decoder, VIDIOC_DQBUF, &entry), "DQBUF");
	return entry;
}

static void write_bytes(int fd, const void *data, size_t bytes)
{
	const char *p = data;

	while (bytes) {
		ssize_t n = write(fd, p, bytes);

		if (n < 0 && errno == EINTR)
			continue;
		check(n > 0, "pixel output");
		p += n;
		bytes -= n;
	}
}

int main(int argc, char **argv)
{
	check(argc >= 5 && argc <= 7,
	      "usage: w202-vpu-request-hevc INPUT.hevc OUTPUT.nv12 VIDEO MEDIA [mmap|dmabuf] [COUNT]");
	bool dmabuf = argc >= 6 && !strcmp(argv[5], "dmabuf");
	unsigned int count = argc == 7 ? strtoul(argv[6], NULL, 10) : 4;
	check(count && count <= 10000, "frame count");
	FILE *source = fopen(argv[1], "rb");
	check(source && !fseek(source, 0, SEEK_END), "open input");
	long bytes = ftell(source);
	check(bytes > 0 && bytes <= 8 * 1024 * 1024 && !fseek(source, 0, SEEK_SET), "input size");
	unsigned char *stream = malloc(bytes);
	check(stream && fread(stream, 1, bytes, source) == (size_t)bytes, "read input");
	fclose(source);
	struct hevc_fixture fixture;
	check(!hevc_parse_idr(stream, bytes, 0, &fixture), "parse first IDR");
	unsigned int log_ctu = fixture.sps.log2_min_luma_coding_block_size_minus3 + 3 +
		fixture.sps.log2_diff_max_min_luma_coding_block_size;
	check(log_ctu >= 4 && log_ctu <= 6, "CTU size");
	unsigned int width = (fixture.sps.pic_width_in_luma_samples + (1U << log_ctu) - 1) & ~((1U << log_ctu) - 1);
	unsigned int height = (fixture.sps.pic_height_in_luma_samples + (1U << log_ctu) - 1) & ~((1U << log_ctu) - 1);
	decoder = open(argv[3], O_RDWR | O_NONBLOCK | O_CLOEXEC);
	check(decoder >= 0, "V4L2 device");
	int media = open(argv[4], O_RDWR | O_CLOEXEC), request_fd = -1;
	check(media >= 0 && !ioctl(media, MEDIA_IOC_REQUEST_ALLOC, &request_fd), "allocate request");
	/* Supply SPS before format negotiation, so CTU stride is known. */
	struct v4l2_ext_control sps = {.id = V4L2_CID_STATELESS_HEVC_SPS, .size = sizeof(fixture.sps), .ptr = &fixture.sps};
	struct v4l2_ext_controls setup = {.count = 1, .controls = &sps};
	check(!ioctl(decoder, VIDIOC_S_EXT_CTRLS, &setup), "SPS for negotiation");
	struct v4l2_format format = {.type = V4L2_BUF_TYPE_VIDEO_OUTPUT};
	format.fmt.pix = (struct v4l2_pix_format){
		.width = fixture.sps.pic_width_in_luma_samples, .height = fixture.sps.pic_height_in_luma_samples,
		.pixelformat = V4L2_PIX_FMT_HEVC_SLICE, .sizeimage = bytes + 1024,
	};
	check(!ioctl(decoder, VIDIOC_S_FMT, &format) && format.fmt.pix.pixelformat == V4L2_PIX_FMT_HEVC_SLICE, "OUTPUT HEVC format");
	struct request_buffer input = allocate(format.type, dmabuf, format.fmt.pix.sizeimage);
	format = (struct v4l2_format){.type = V4L2_BUF_TYPE_VIDEO_CAPTURE};
	check(!ioctl(decoder, VIDIOC_G_FMT, &format) && format.fmt.pix.bytesperline == width &&
	      format.fmt.pix.height == height && format.fmt.pix.pixelformat == V4L2_PIX_FMT_NV12, "CAPTURE stride");
	struct request_buffer frame = allocate(format.type, dmabuf, format.fmt.pix.sizeimage);
	struct v4l2_ext_control controls[] = {
		{.id = V4L2_CID_STATELESS_HEVC_SPS, .size = sizeof(fixture.sps), .ptr = &fixture.sps},
		{.id = V4L2_CID_STATELESS_HEVC_PPS, .size = sizeof(fixture.pps), .ptr = &fixture.pps},
		{.id = V4L2_CID_STATELESS_HEVC_SLICE_PARAMS, .size = sizeof(fixture.slice), .ptr = &fixture.slice},
		{.id = V4L2_CID_STATELESS_HEVC_DECODE_PARAMS, .size = sizeof(fixture.decode), .ptr = &fixture.decode},
	};
	struct v4l2_ext_controls parameters = {.which = V4L2_CTRL_WHICH_REQUEST_VAL,
		.count = 4, .request_fd = request_fd, .controls = controls};
	int output = open(argv[2], O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	check(output >= 0, "new pixel output");
	for (unsigned int i = 0; i < count; i++) {
		check(!hevc_parse_idr(stream, bytes, i, &fixture), "parse IDR");
		cpu_sync(&input, DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE);
		memset(input.data, 0, input.length);
		memcpy(input.data, fixture.nal, fixture.nal_size);
		cpu_sync(&input, DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE);
		const char *bad = getenv("W202_BAD_HEVC");
		if (bad && !strcmp(bad, "depth")) fixture.sps.bit_depth_luma_minus8 = 2;
		if (bad && !strcmp(bad, "wpp")) fixture.pps.flags |= V4L2_HEVC_PPS_FLAG_ENTROPY_CODING_SYNC_ENABLED;
		if (bad && !strcmp(bad, "offset")) fixture.slice.data_byte_offset = fixture.nal_size;
		check(!ioctl(decoder, VIDIOC_S_EXT_CTRLS, &parameters), "request controls");
		enqueue(&frame, 0, -1); enqueue(&input, fixture.nal_size, request_fd);
		check(!ioctl(request_fd, MEDIA_REQUEST_IOC_QUEUE), "queue request");
		if (!i) {
			check(!ioctl(decoder, VIDIOC_STREAMON, &frame.type), "CAPTURE STREAMON");
			check(!ioctl(decoder, VIDIOC_STREAMON, &input.type), "OUTPUT STREAMON");
		}
		struct pollfd done = {.fd = request_fd, .events = POLLPRI};
		check(poll(&done, 1, 3000) == 1 && (done.revents & POLLPRI), "request completion");
		struct v4l2_buffer captured = dequeue(&frame), consumed = dequeue(&input);
		bool failed = (captured.flags | consumed.flags) & V4L2_BUF_FLAG_ERROR;
		check(failed == !!bad, "buffer completion status");
		check(captured.sequence == i && consumed.sequence == i &&
		      captured.timestamp.tv_sec == 1 && captured.bytesused == width * height * 3 / 2, "capture metadata");
		if (!failed) {
			cpu_sync(&frame, DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ);
			unsigned char *y = (unsigned char *)frame.data + fixture.crop_top * width + fixture.crop_left;
			unsigned char *uv = (unsigned char *)frame.data + width * height + (fixture.crop_top / 2) * width + fixture.crop_left;
			for (unsigned int row = 0; row < fixture.height; row++) write_bytes(output, y + row * width, fixture.width);
			for (unsigned int row = 0; row < fixture.height / 2; row++) write_bytes(output, uv + row * width, fixture.width);
			cpu_sync(&frame, DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ);
		}
		if (i + 1 != count) check(!ioctl(request_fd, MEDIA_REQUEST_IOC_REINIT), "reinitialize request");
	}
	check(!ioctl(decoder, VIDIOC_STREAMOFF, &input.type), "OUTPUT STREAMOFF");
	check(!ioctl(decoder, VIDIOC_STREAMOFF, &frame.type), "CAPTURE STREAMOFF");
	check(!close(output), "close pixel output");
	fprintf(stderr, "V4L2 HEVC IDR: stride=%ux%u visible=%ux%u CTU=%u mode=%s requests=%u\n",
		width, height, fixture.width, fixture.height, 1U << log_ctu, dmabuf ? "DMABUF" : "MMAP", count);
	close(request_fd); close(media); close(decoder); decoder = -1;
	munmap(input.data, input.length); munmap(frame.data, frame.length);
	if (input.fd >= 0) close(input.fd);
	if (frame.fd >= 0) close(frame.fd);
	free(stream);
	return 0;
}
