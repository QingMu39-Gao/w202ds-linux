/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef W202_VPU_HW_H
#define W202_VPU_HW_H

#include <linux/types.h>

/* Internal, pointer-free hardware program, never a userspace ABI. The caller
 * owns every DMA mapping until native_end has stopped and reset the core. */
#define W202_VPU_MAX_REGS 1024
#define W202_VPU_BSM_PADDING 1024
enum w202_vpu_codec { W202_VPU_H264, W202_VPU_HEVC };
struct w202_vpu_reg {
	__u32 offset, value;
};
struct w202_vpu_config {
	__u32 width, height;
	__u32 stream_address, stream_bytes, header_bits;
	enum w202_vpu_codec codec;
	unsigned int count;
	struct w202_vpu_reg regs[W202_VPU_MAX_REGS];
};

#endif
