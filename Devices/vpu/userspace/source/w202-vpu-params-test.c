// SPDX-License-Identifier: GPL-2.0-only
/* Offline control-boundary tests. Hardware pixel tests remain mandatory. */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "../../source/w202_vpu_h264.h"
#include "../../source/w202_vpu_hevc.h"

#define REQUIRE(x) do { if (!(x)) { \
	fprintf(stderr, "parameter check failed at line %d: %s\n", __LINE__, #x); \
	return 1; } } while (0)

static __u32 reg_value(const struct w202_vpu_config *c, __u32 offset)
{
	for (unsigned int i = 0; i < c->count; i++)
		if (c->regs[i].offset == offset)
			return c->regs[i].value;
	return ~0U;
}

static int h264(void)
{
	struct v4l2_ctrl_h264_sps s = {
		.profile_idc = 77, .chroma_format_idc = 1,
		.pic_width_in_mbs_minus1 = 3, .pic_height_in_map_units_minus1 = 3,
		.flags = V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY,
	};
	struct v4l2_ctrl_h264_pps p = {};
	struct v4l2_ctrl_h264_slice_params sl = {
		.slice_type = V4L2_H264_SLICE_TYPE_I, .header_bit_size = 8,
	};
	struct v4l2_ctrl_h264_decode_params d = {
		.flags = V4L2_H264_DECODE_PARAM_FLAG_IDR_PIC, .nal_ref_idc = 3,
	};
	struct w202_h264_job j = {
		.sps = &s, .pps = &p, .slice = &sl, .decode = &d,
		.input_address = 0x40000000, .input_capacity = 4096,
		.output_address = 0x41000000, .output_capacity = 6144,
		.auxiliary_address = 0x41001800, .auxiliary_capacity = 4096,
		.input_bytes = 100, .nalu_bytes = 100,
	};
	struct w202_vpu_config c;

	REQUIRE(!w202_h264_build_idr(&j, &c));
	d.frame_num = 7;
	REQUIRE(w202_h264_build_ipb(&j, &c) == -EINVAL);
	d.flags = 0;
	REQUIRE(!w202_h264_build_ipb(&j, &c));
	REQUIRE(w202_h264_build_idr(&j, &c) == -EOPNOTSUPP);
	d.nal_ref_idc = 0;
	REQUIRE(!w202_h264_build_ipb(&j, &c));
	d.frame_num = 16;
	REQUIRE(w202_h264_build_ipb(&j, &c) == -EINVAL);
	d.frame_num = 7;
	sl.first_mb_in_slice = 4;
	REQUIRE(w202_h264_build_ipb(&j, &c) == -EINVAL);
	j.slice_index = 1;
	REQUIRE(!w202_h264_build_ipb(&j, &c));
	sl.first_mb_in_slice = 16;
	REQUIRE(w202_h264_build_ipb(&j, &c) == -EINVAL);
	sl.first_mb_in_slice = 4;
	j.slice_index = 512;
	REQUIRE(w202_h264_build_ipb(&j, &c) == -EINVAL);
	j.slice_index = 1;
	sl.first_mb_in_slice = 0;
	REQUIRE(w202_h264_build_ipb(&j, &c) == -EINVAL);
	return 0;
}

static int hevc(void)
{
	struct v4l2_ctrl_hevc_sps s = {
		.chroma_format_idc = 1, .pic_width_in_luma_samples = 64,
		.pic_height_in_luma_samples = 64,
		.log2_diff_max_min_luma_coding_block_size = 3,
		.log2_diff_max_min_luma_transform_block_size = 3,
	};
	struct v4l2_ctrl_hevc_pps p = {};
	struct v4l2_ctrl_hevc_slice_params sl = {
		.bit_size = 800, .data_byte_offset = 3, .nal_unit_type = 19,
		.nuh_temporal_id_plus1 = 1, .slice_type = V4L2_HEVC_SLICE_TYPE_I,
	};
	struct v4l2_ctrl_hevc_decode_params d = {
		.flags = V4L2_HEVC_DECODE_PARAM_FLAG_IDR_PIC |
			 V4L2_HEVC_DECODE_PARAM_FLAG_IRAP_PIC,
	};
	struct w202_hevc_reference refs[16];
	struct w202_hevc_job j = {
		.sps = &s, .pps = &p, .slice = &sl, .decode = &d,
		.input_address = 0x40000000, .input_capacity = 4096,
		.output_address = 0x41000000, .output_capacity = 6144,
		.auxiliary_address = 0x41001800, .auxiliary_capacity = 4096,
		.input_bytes = 100, .nalu_bytes = 100, .header_bits = 24,
	};
	struct w202_vpu_config c;
	__u32 offsets[2] = { 1, 1 };

	REQUIRE(!w202_hevc_build_idr(&j, &c));
	d.flags = V4L2_HEVC_DECODE_PARAM_FLAG_IRAP_PIC;
	sl.nal_unit_type = 21;
	d.pic_order_cnt_val = sl.slice_pic_order_cnt = 8;
	s.flags |= V4L2_HEVC_SPS_FLAG_SPS_TEMPORAL_MVP_ENABLED;
	sl.flags |= V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_TEMPORAL_MVP_ENABLED;
	REQUIRE(!w202_hevc_build_ipb(&j, &c));
	REQUIRE(w202_hevc_build_idr(&j, &c) == -EOPNOTSUPP);
	d.flags = 0;
	sl.nal_unit_type = 1;
	sl.slice_type = V4L2_HEVC_SLICE_TYPE_P;
	d.num_active_dpb_entries = 16;
	sl.num_ref_idx_l0_active_minus1 = 15;
	for (unsigned int i = 0; i < 16; i++) {
		refs[i] = (struct w202_hevc_reference) {
			.address = 0x42000000 + i * 0x10000,
			.auxiliary_address = 0x42001800 + i * 0x10000,
			.capacity = 6144, .auxiliary_capacity = 4096, .frame_id = i,
		};
		sl.ref_idx_l0[i] = i;
		d.dpb[i].timestamp = i + 1;
		d.dpb[i].pic_order_cnt_val = i - 8;
	}
	REQUIRE(w202_hevc_build_ipb(&j, &c) == -EINVAL);
	j.reference_lists[0] = refs;
	REQUIRE(!w202_hevc_build_ipb(&j, &c));
	/* Factory slice-header fields +0xce8 (MVD L1 zero) and +0xc5a
	 * (temporal MVP) feed bits 2 and 6 of 0x1064 respectively. A swap
	 * decodes I/first-P correctly but corrupts later temporal predictions. */
	REQUIRE(reg_value(&c, 0x1064) == 0x40);
	sl.slice_type = V4L2_HEVC_SLICE_TYPE_B;
	j.reference_lists[1] = refs;
	for (unsigned int flags = 0; flags < 4; flags++) {
		static const __u32 expected[] = { 0, 0x40, 0x04, 0x44 };

		sl.flags = (flags & 1 ? V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_TEMPORAL_MVP_ENABLED : 0) |
			   (flags & 2 ? V4L2_HEVC_SLICE_PARAMS_FLAG_MVD_L1_ZERO : 0);
		REQUIRE(!w202_hevc_build_ipb(&j, &c));
		REQUIRE(reg_value(&c, 0x1064) == expected[flags]);
	}
	sl.slice_type = V4L2_HEVC_SLICE_TYPE_P;
	sl.flags = V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_TEMPORAL_MVP_ENABLED;
	refs[15].auxiliary_capacity = 1343;
	REQUIRE(w202_hevc_build_ipb(&j, &c) == -EINVAL);
	refs[15].auxiliary_capacity = 4096;
	sl.ref_idx_l0[15] = 16;
	REQUIRE(w202_hevc_build_ipb(&j, &c) == -EINVAL);
	sl.ref_idx_l0[15] = 15;
	refs[15].frame_id = 32;
	REQUIRE(w202_hevc_build_ipb(&j, &c) == -EINVAL);
	refs[15].frame_id = 15;
	sl.collocated_ref_idx = 16;
	REQUIRE(w202_hevc_build_ipb(&j, &c) == -EINVAL);
	sl.collocated_ref_idx = 15;
	REQUIRE(!w202_hevc_build_ipb(&j, &c));
	s.bit_depth_luma_minus8 = 2;
	REQUIRE(w202_hevc_build_ipb(&j, &c) == -EOPNOTSUPP);
	s.bit_depth_luma_minus8 = 0;
	p.flags |= V4L2_HEVC_PPS_FLAG_ENTROPY_CODING_SYNC_ENABLED;
	REQUIRE(!w202_hevc_build_ipb(&j, &c));
	REQUIRE(reg_value(&c, 0x105c) & 1);
	s.log2_diff_max_min_luma_coding_block_size = 2;
	REQUIRE(w202_hevc_build_ipb(&j, &c) == -EINVAL);
	sl.num_entry_point_offsets = 1;
	j.num_entry_point_offsets = 1;
	REQUIRE(w202_hevc_build_ipb(&j, &c) == -EINVAL);
	j.entry_point_offsets = offsets;
	REQUIRE(!w202_hevc_build_ipb(&j, &c));
	offsets[0] = 0;
	REQUIRE(w202_hevc_build_ipb(&j, &c) == -EINVAL);
	offsets[0] = 97;
	REQUIRE(w202_hevc_build_ipb(&j, &c) == -EINVAL);
	offsets[0] = ~0U;
	REQUIRE(w202_hevc_build_ipb(&j, &c) == -EINVAL);
	offsets[0] = 96;
	REQUIRE(!w202_hevc_build_ipb(&j, &c));
	j.num_entry_point_offsets = 2;
	REQUIRE(w202_hevc_build_ipb(&j, &c) == -EINVAL);
	j.num_entry_point_offsets = sl.num_entry_point_offsets = 2;
	REQUIRE(w202_hevc_build_ipb(&j, &c) == -EINVAL);
	j.num_entry_point_offsets = sl.num_entry_point_offsets = 1;
	p.flags = 0;
	REQUIRE(w202_hevc_build_ipb(&j, &c) == -EINVAL);
	j.num_entry_point_offsets = sl.num_entry_point_offsets = 0;
	j.entry_point_offsets = NULL;
	REQUIRE(!w202_hevc_build_ipb(&j, &c));
	REQUIRE(!(reg_value(&c, 0x105c) & 1));
	p.flags = V4L2_HEVC_PPS_FLAG_TILES_ENABLED;
	REQUIRE(w202_hevc_build_ipb(&j, &c) == -EOPNOTSUPP);
	p.flags = 0;
	s.log2_diff_max_min_luma_coding_block_size = 1;
	s.log2_diff_max_min_luma_transform_block_size = 2;
	s.flags |= V4L2_HEVC_SPS_FLAG_SAMPLE_ADAPTIVE_OFFSET;
	REQUIRE(w202_hevc_build_ipb(&j, &c) == -EOPNOTSUPP);
	s.flags &= ~V4L2_HEVC_SPS_FLAG_SAMPLE_ADAPTIVE_OFFSET;
	REQUIRE(!w202_hevc_build_ipb(&j, &c));
	return 0;
}

int main(void)
{
	if (h264() || hevc())
		return 1;
	puts("H.264/HEVC control boundary tests passed (no hardware qualification)");
	return 0;
}
