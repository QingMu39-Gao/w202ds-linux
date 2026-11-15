// SPDX-License-Identifier: GPL-2.0-only
/* Offline diagnostic: parse fixtures and validate the pure hardware builder. */
#include <errno.h>
#include <stdio.h>
#include "hevc-idr-fixture.h"

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "HEVC check failed at line %d\n", __LINE__); return 1; } } while (0)
int main(int argc, char **argv)
{
	REQUIRE(argc == 2);
	FILE *input = fopen(argv[1], "rb");
	REQUIRE(input && !fseek(input, 0, SEEK_END));
	long bytes = ftell(input);
	REQUIRE(bytes > 0 && bytes <= 8 * 1024 * 1024 && !fseek(input, 0, SEEK_SET));
	unsigned char *stream = malloc(bytes);
	REQUIRE(stream && fread(stream, 1, bytes, input) == (size_t)bytes);
	fclose(input);
	struct hevc_fixture f;
	struct w202_vpu_config c;
	unsigned int frames = 0;
	for (unsigned int i = 0; !hevc_parse_idr(stream, bytes, i, &f); i++) {
		unsigned int log = f.sps.log2_min_luma_coding_block_size_minus3 + 3 + f.sps.log2_diff_max_min_luma_coding_block_size;
		REQUIRE(log >= 4 && log <= 6);
		unsigned int width = (f.sps.pic_width_in_luma_samples + (1U << log) - 1) & ~((1U << log) - 1);
		unsigned int height = (f.sps.pic_height_in_luma_samples + (1U << log) - 1) & ~((1U << log) - 1);
		struct w202_hevc_job j = {.sps = &f.sps, .pps = &f.pps, .slice = &f.slice, .decode = &f.decode,
			.input_address = 0x40000000, .output_address = 0x41000000,
			.auxiliary_address = 0x41000000 + width * height * 3 / 2,
			.input_capacity = f.nal_size + W202_VPU_BSM_PADDING,
			.output_capacity = width * height * 3 / 2, .auxiliary_capacity = width * height,
			.input_bytes = f.nal_size, .nalu_bytes = f.nal_size,
			.header_bits = f.slice.data_byte_offset * 8};
		REQUIRE(!w202_hevc_build_idr(&j, &c));
		REQUIRE(c.codec == W202_VPU_HEVC && c.width == width && c.height == height);
		printf("IDR %u: %ux%u visible=%ux%u CTU=%u nal=%zu header=%u QP=%d regs=%u\n",
		       i, width, height, f.width, f.height, 1U << log, f.nal_size,
		       f.slice.data_byte_offset, 26 + f.pps.init_qp_minus26 + f.slice.slice_qp_delta, c.count);
		if (!i) for (unsigned int k = 0; k < c.count; k++)
			if (c.regs[k].offset < 0x1800) printf("%04x=%08x\n", c.regs[k].offset, c.regs[k].value);
		f.sps.bit_depth_luma_minus8 = 2;
		REQUIRE(w202_hevc_build_idr(&j, &c) == -EOPNOTSUPP);
		f.sps.bit_depth_luma_minus8 = 0;
		f.pps.flags |= V4L2_HEVC_PPS_FLAG_ENTROPY_CODING_SYNC_ENABLED;
		REQUIRE(w202_hevc_build_idr(&j, &c) == -EOPNOTSUPP);
		f.pps.flags &= ~V4L2_HEVC_PPS_FLAG_ENTROPY_CODING_SYNC_ENABLED;
		j.input_capacity--;
		REQUIRE(w202_hevc_build_idr(&j, &c) == -EINVAL);
		j.input_capacity++;
		j.auxiliary_address = 0xfffffff0;
		REQUIRE(w202_hevc_build_idr(&j, &c) == -EINVAL);
		frames++;
	}
	REQUIRE(frames > 0);
	printf("HEVC builder: %u frames and unsupported-format/DMA-boundary checks passed\n", frames);
	free(stream);
	return 0;
}
