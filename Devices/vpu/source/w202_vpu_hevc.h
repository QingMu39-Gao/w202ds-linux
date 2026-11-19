/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef W202_VPU_HEVC_H
#define W202_VPU_HEVC_H

#include <linux/v4l2-controls.h>
#include "w202_vpu_hw.h"

#define W202_HEVC_MAX_REFERENCES 16
#define W202_HEVC_MAX_ENTRY_POINTS (1088 / 16 - 1)
struct w202_hevc_reference {
	__u64 address, auxiliary_address;
	__u64 capacity, auxiliary_capacity;
	__u8 frame_id;
};

struct w202_hevc_job {
	const struct v4l2_ctrl_hevc_sps *sps;
	const struct v4l2_ctrl_hevc_pps *pps;
	const struct v4l2_ctrl_hevc_slice_params *slice;
	const struct v4l2_ctrl_hevc_decode_params *decode;
	__u64 input_address, output_address, auxiliary_address;
	__u64 input_capacity, output_capacity, auxiliary_capacity;
	__u32 input_bytes, nalu_bytes, header_bits;
	const __u32 *entry_point_offsets;
	__u32 num_entry_point_offsets;
	/* Resolved CAPTURE buffers in slice-list order, with stable picture IDs. */
	const struct w202_hevc_reference *reference_lists[2];
};

int w202_hevc_build_idr(const struct w202_hevc_job *job, struct w202_vpu_config *config);
int w202_hevc_build_ipb(const struct w202_hevc_job *job, struct w202_vpu_config *config);

#endif
