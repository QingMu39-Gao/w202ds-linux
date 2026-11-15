/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef W202_VPU_HEVC_H
#define W202_VPU_HEVC_H

#include <linux/v4l2-controls.h>
#include "w202_vpu_hw.h"

struct w202_hevc_job {
	const struct v4l2_ctrl_hevc_sps *sps;
	const struct v4l2_ctrl_hevc_pps *pps;
	const struct v4l2_ctrl_hevc_slice_params *slice;
	const struct v4l2_ctrl_hevc_decode_params *decode;
	__u64 input_address, output_address, auxiliary_address;
	__u64 input_capacity, output_capacity, auxiliary_capacity;
	__u32 input_bytes, nalu_bytes, header_bits;
};

int w202_hevc_build_idr(const struct w202_hevc_job *job, struct w202_vpu_config *config);

#endif
