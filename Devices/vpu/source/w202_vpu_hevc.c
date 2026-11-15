// SPDX-License-Identifier: GPL-2.0-only
/* Qogirn6Pro HEVC controls to register values. No MMIO or vendor runtime.
 * Register layout reconstructed from W202DS Android H265DecDecode_NALU;
 * offsets are relative to the decoder resource. */
#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/string.h>
#else
#include <errno.h>
#include <string.h>
#endif
#include "w202_vpu_hevc.h"
#include "w202_vpu_hevc_cabac.h"

static void emit(struct w202_vpu_config *c, __u32 offset, __u32 value)
{
	if (c->count < W202_VPU_MAX_REGS)
		c->regs[c->count++] = (struct w202_vpu_reg){offset, value};
	else
		c->count = W202_VPU_MAX_REGS + 1;
}

static int valid_span(__u64 address, __u64 capacity, __u64 needed)
{
	return address >= 0x20000000 && !(address & 15) && capacity >= needed &&
	       address < 0x60000000 && capacity <= 0x60000000 - address;
}

int w202_hevc_build_idr(const struct w202_hevc_job *j, struct w202_vpu_config *c)
{
	const struct v4l2_ctrl_hevc_sps *s;
	const struct v4l2_ctrl_hevc_pps *p;
	const struct v4l2_ctrl_hevc_slice_params *sl;
	const struct v4l2_ctrl_hevc_decode_params *d;
	__u32 min_cb, log_ctu, min_tb, max_tb, cols, rows, width, height;
	__u32 pcm_min, pcm_max, value;
	__u64 y_size, aux_size;
	unsigned int i;
	int qp, state;

	if (!j || !c || !j->sps || !j->pps || !j->slice || !j->decode)
		return -EINVAL;
	s = j->sps; p = j->pps; sl = j->slice; d = j->decode;
	/* First bring-up: one independent IDR slice, Main 8-bit, linear NV12.
	 * Unsupported syntax must fail before power-on and before padding writes. */
	if (s->chroma_format_idc != 1 || s->bit_depth_luma_minus8 ||
	    s->bit_depth_chroma_minus8 || s->sps_max_sub_layers_minus1 > 6 ||
	    (s->flags & (V4L2_HEVC_SPS_FLAG_SEPARATE_COLOUR_PLANE |
			 V4L2_HEVC_SPS_FLAG_SCALING_LIST_ENABLED |
			 V4L2_HEVC_SPS_FLAG_PCM_ENABLED)) ||
	    (p->flags & (V4L2_HEVC_PPS_FLAG_TILES_ENABLED |
			 V4L2_HEVC_PPS_FLAG_ENTROPY_CODING_SYNC_ENABLED)) ||
	    sl->slice_type != V4L2_HEVC_SLICE_TYPE_I ||
	    (sl->nal_unit_type != 19 && sl->nal_unit_type != 20) ||
	    sl->nuh_temporal_id_plus1 != 1 || sl->slice_segment_addr ||
	    sl->num_entry_point_offsets || sl->colour_plane_id ||
	    sl->slice_act_y_qp_offset || sl->slice_act_cb_qp_offset || sl->slice_act_cr_qp_offset ||
	    (sl->flags & ~(V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_SAO_LUMA |
			   V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_SAO_CHROMA |
			   V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_DEBLOCKING_FILTER_DISABLED |
			   V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_LOOP_FILTER_ACROSS_SLICES_ENABLED)) ||
	    !(d->flags & V4L2_HEVC_DECODE_PARAM_FLAG_IDR_PIC) ||
	    !(d->flags & V4L2_HEVC_DECODE_PARAM_FLAG_IRAP_PIC) ||
	    d->num_active_dpb_entries || d->num_poc_st_curr_before ||
	    d->num_poc_st_curr_after || d->num_poc_lt_curr ||
	    d->pic_order_cnt_val || sl->slice_pic_order_cnt)
		return -EOPNOTSUPP;
	min_cb = s->log2_min_luma_coding_block_size_minus3 + 3;
	log_ctu = min_cb + s->log2_diff_max_min_luma_coding_block_size;
	min_tb = s->log2_min_luma_transform_block_size_minus2 + 2;
	max_tb = min_tb + s->log2_diff_max_min_luma_transform_block_size;
	qp = 26 + p->init_qp_minus26 + sl->slice_qp_delta;
	if (min_cb < 3 || min_cb > 6 || log_ctu < 4 || log_ctu > 6 ||
	    min_tb > 5 || max_tb > 5 || max_tb > log_ctu || min_tb > min_cb ||
	    s->max_transform_hierarchy_depth_inter > log_ctu - min_tb ||
	    s->max_transform_hierarchy_depth_intra > log_ctu - min_tb ||
	    p->diff_cu_qp_delta_depth > log_ctu - min_cb ||
	    p->log2_parallel_merge_level_minus2 > 4 ||
	    s->sps_max_dec_pic_buffering_minus1 > 15 ||
	    !s->pic_width_in_luma_samples || s->pic_width_in_luma_samples > 1920 ||
	    !s->pic_height_in_luma_samples || s->pic_height_in_luma_samples > 1088 ||
	    (s->pic_width_in_luma_samples & 7) || (s->pic_height_in_luma_samples & 7) ||
	    qp < 0 || qp > 51 || sl->five_minus_max_num_merge_cand > 4 ||
	    p->pps_cb_qp_offset < -12 || p->pps_cb_qp_offset > 12 ||
	    p->pps_cr_qp_offset < -12 || p->pps_cr_qp_offset > 12 ||
	    sl->slice_cb_qp_offset < -12 || sl->slice_cb_qp_offset > 12 ||
	    sl->slice_cr_qp_offset < -12 || sl->slice_cr_qp_offset > 12 ||
	    sl->slice_beta_offset_div2 < -6 || sl->slice_beta_offset_div2 > 6 ||
	    sl->slice_tc_offset_div2 < -6 || sl->slice_tc_offset_div2 > 6 ||
	    !j->input_bytes || j->input_bytes > 8 * 1024 * 1024 ||
	    j->input_bytes > 0x1fffffff || sl->bit_size != j->input_bytes * 8 ||
	    j->nalu_bytes > j->input_bytes || !j->nalu_bytes ||
	    j->header_bits < 24 || (j->header_bits & 7) ||
	    j->header_bits >= j->nalu_bytes * 8 || j->header_bits > 4096)
		return -EINVAL;
	cols = (s->pic_width_in_luma_samples + (1U << log_ctu) - 1) >> log_ctu;
	rows = (s->pic_height_in_luma_samples + (1U << log_ctu) - 1) >> log_ctu;
	width = cols << log_ctu; height = rows << log_ctu;
	y_size = (__u64)width * height;
	aux_size = cols * rows * (log_ctu == 4 ? 32 : log_ctu == 5 ? 80 : 320) + 1024;
	if (!valid_span(j->input_address, j->input_capacity, (__u64)j->input_bytes + W202_VPU_BSM_PADDING) ||
	    !valid_span(j->output_address, j->output_capacity, y_size * 3 / 2) ||
	    !valid_span(j->auxiliary_address, j->auxiliary_capacity, aux_size))
		return -EINVAL;
	memset(c, 0, sizeof(*c));
	c->codec = W202_VPU_HEVC;
	c->width = width; c->height = height;
	c->stream_address = j->input_address; c->stream_bytes = j->input_bytes;
	c->header_bits = j->header_bits;
	emit(c, 0x1020, 0x11000187);
	emit(c, 0x1024, cols | (rows << 8));
	emit(c, 0x1180, s->pic_width_in_luma_samples | ((__u32)s->pic_height_in_luma_samples << 16));
	emit(c, 0x103c, (cols * rows) | (qp << 25) |
	     (!(sl->flags & V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_DEBLOCKING_FILTER_DISABLED) ? 0x80000000U : 0));
	emit(c, 0x1040, cols | (rows << 16));
	emit(c, 0x1044, 0);
#define PF(flag) (!!(p->flags & V4L2_HEVC_PPS_FLAG_##flag))
#define SF(flag) (!!(s->flags & V4L2_HEVC_SPS_FLAG_##flag))
#define LF(flag) (!!(sl->flags & V4L2_HEVC_SLICE_PARAMS_FLAG_##flag))
	value = ((__u32)(sl->slice_tc_offset_div2 * 2) & 31) |
		(((__u32)(sl->slice_beta_offset_div2 * 2) & 31) << 5) |
		(PF(CONSTRAINED_INTRA_PRED) << 10) | (1 << 12) |
		(PF(WEIGHTED_PRED) << 16) | (PF(WEIGHTED_BIPRED) << 17) |
		(PF(CU_QP_DELTA_ENABLED) << 21) |
		(((log_ctu - p->diff_cu_qp_delta_depth + 1) & 3) << 22) |
		((log_ctu & 3) << 24) | (((min_cb + 1) & 3) << 26) |
		(PF(TRANSFORM_SKIP_ENABLED) << 29) | (PF(SIGN_DATA_HIDING_ENABLED) << 30);
	emit(c, 0x1048, value);
	emit(c, 0x104c, 0xffc00 | ((__u32)sl->slice_cb_qp_offset & 31) |
	     (((__u32)sl->slice_cr_qp_offset & 31) << 5));
	emit(c, 0x1050, d->pic_order_cnt_val);
	emit(c, 0x1054, 0x10 | 8U << 20 | 8U << 24 | PF(LOOP_FILTER_ACROSS_TILES_ENABLED) << 28);
	emit(c, 0x1058, 0);
	/* PCM is disabled, but the factory still programs the inferred defaults. */
	pcm_min = s->log2_min_pcm_luma_coding_block_size_minus3 + 3;
	pcm_max = pcm_min + s->log2_diff_max_min_pcm_luma_coding_block_size;
	emit(c, 0x105c, (LF(SLICE_SAO_LUMA) << 1) | (LF(SLICE_SAO_CHROMA) << 2) |
	     (PF(TRANSQUANT_BYPASS_ENABLED) << 3) | (((pcm_min + 1) & 3) << 4) |
	     (((pcm_max + 1) & 3) << 6) | (LF(SLICE_LOOP_FILTER_ACROSS_SLICES_ENABLED) << 8) |
	     (s->max_transform_hierarchy_depth_intra << 12) |
	     (s->max_transform_hierarchy_depth_inter << 16) | (5U << 20) |
	     (((max_tb + 2) & 3) << 24) | (((min_tb + 2) & 3) << 26));
	emit(c, 0x1060, cols | (rows << 8));
	emit(c, 0x1064, SF(SAMPLE_ADAPTIVE_OFFSET) | (SF(AMP_ENABLED) << 1) |
	     (SF(STRONG_INTRA_SMOOTHING_ENABLED) << 5) |
	     (((p->log2_parallel_merge_level_minus2 + 8) & 7) << 8) |
	     (((__u32)p->pps_cb_qp_offset & 31) << 16) |
	     (((__u32)p->pps_cr_qp_offset & 31) << 24));
	for (i = 0x1068; i <= 0x107c; i += 4)
		emit(c, i, 0);
	emit(c, 0x1088, j->output_address >> 4);
	emit(c, 0x108c, (j->output_address + y_size) >> 4);
	emit(c, 0x1090, j->auxiliary_address >> 4);
	for (i = 0; i < 154; i++) {
		state = ((w202_hevc_cabac_init[0][i][0] * qp) >> 4) + w202_hevc_cabac_init[0][i][1];
		if (state < 1) state = 1;
		if (state > 126) state = 126;
		state = state <= 63 ? 126 - 2 * state : 2 * state - 127;
		emit(c, 0x1800 + i * 4, state);
		emit(c, 0x1800 + (i + 154) * 4, state);
	}
#undef PF
#undef SF
#undef LF
	return c->count <= W202_VPU_MAX_REGS ? 0 : -E2BIG;
}
