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

int w202_hevc_build_ipb(const struct w202_hevc_job *j, struct w202_vpu_config *c)
{
	const struct v4l2_ctrl_hevc_sps *s;
	const struct v4l2_ctrl_hevc_pps *p;
	const struct v4l2_ctrl_hevc_slice_params *sl;
	const struct v4l2_ctrl_hevc_decode_params *d;
	__u32 min_cb, log_ctu, min_tb, max_tb, cols, rows, width, height;
	__u32 pcm_min, pcm_max, value;
	__u64 y_size, aux_size, entry_bytes;
	unsigned int i, list, active[2], cabac_table;
	int qp, state, inter, bidir, idr, irap, weighted, chroma_denom;

	if (!j || !c || !j->sps || !j->pps || !j->slice || !j->decode)
		return -EINVAL;
	s = j->sps; p = j->pps; sl = j->slice; d = j->decode;
	inter = sl->slice_type != V4L2_HEVC_SLICE_TYPE_I;
	bidir = sl->slice_type == V4L2_HEVC_SLICE_TYPE_B;
	idr = sl->nal_unit_type == 19 || sl->nal_unit_type == 20;
	irap = sl->nal_unit_type >= 16 && sl->nal_unit_type <= 21;
	active[0] = inter ? (unsigned int)sl->num_ref_idx_l0_active_minus1 + 1 : 0;
	active[1] = bidir ? (unsigned int)sl->num_ref_idx_l1_active_minus1 + 1 : 0;
	weighted = inter && (bidir ? p->flags & V4L2_HEVC_PPS_FLAG_WEIGHTED_BIPRED :
				p->flags & V4L2_HEVC_PPS_FLAG_WEIGHTED_PRED);
	chroma_denom = sl->pred_weight_table.luma_log2_weight_denom +
		       sl->pred_weight_table.delta_chroma_log2_weight_denom;
	/* Independent Main 8-bit slices, linear NV12. Unsupported syntax must
	 * fail before power-on and before padding writes. */
	if (s->chroma_format_idc != 1 || s->bit_depth_luma_minus8 ||
	    s->bit_depth_chroma_minus8 || s->sps_max_sub_layers_minus1 > 6 ||
	    (s->flags & (V4L2_HEVC_SPS_FLAG_SEPARATE_COLOUR_PLANE |
			 V4L2_HEVC_SPS_FLAG_SCALING_LIST_ENABLED |
			 V4L2_HEVC_SPS_FLAG_PCM_ENABLED)) ||
	    (p->flags & V4L2_HEVC_PPS_FLAG_TILES_ENABLED) ||
	    sl->slice_type > V4L2_HEVC_SLICE_TYPE_I ||
	    (sl->nal_unit_type > 9 && !irap) ||
	    !sl->nuh_temporal_id_plus1 || sl->nuh_temporal_id_plus1 > 7 ||
	    sl->slice_segment_addr ||
	    sl->colour_plane_id ||
	    sl->slice_act_y_qp_offset || sl->slice_act_cb_qp_offset || sl->slice_act_cr_qp_offset ||
	    (sl->flags & ~(V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_SAO_LUMA |
			   V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_SAO_CHROMA |
			   V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_TEMPORAL_MVP_ENABLED |
			   V4L2_HEVC_SLICE_PARAMS_FLAG_MVD_L1_ZERO |
			   V4L2_HEVC_SLICE_PARAMS_FLAG_CABAC_INIT |
			   V4L2_HEVC_SLICE_PARAMS_FLAG_COLLOCATED_FROM_L0 |
			   V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_DEBLOCKING_FILTER_DISABLED |
			   V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_LOOP_FILTER_ACROSS_SLICES_ENABLED)) ||
	    active[0] > W202_HEVC_MAX_REFERENCES || active[1] > W202_HEVC_MAX_REFERENCES)
		return -EOPNOTSUPP;
	if (!!(d->flags & V4L2_HEVC_DECODE_PARAM_FLAG_IDR_PIC) != idr ||
	    !!(d->flags & V4L2_HEVC_DECODE_PARAM_FLAG_IRAP_PIC) != irap ||
	    (irap && (inter || sl->nuh_temporal_id_plus1 != 1)) ||
	    (idr && (d->pic_order_cnt_val || d->num_active_dpb_entries ||
		     d->num_poc_st_curr_before || d->num_poc_st_curr_after || d->num_poc_lt_curr)) ||
	    d->pic_order_cnt_val != sl->slice_pic_order_cnt ||
	    d->num_active_dpb_entries > V4L2_HEVC_DPB_ENTRIES_NUM_MAX ||
	    d->num_poc_st_curr_before + d->num_poc_st_curr_after + d->num_poc_lt_curr >
		    d->num_active_dpb_entries ||
	    (weighted && (sl->pred_weight_table.luma_log2_weight_denom > 7 ||
			 chroma_denom < 0 || chroma_denom > 7)) ||
	    ((sl->flags & V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_TEMPORAL_MVP_ENABLED) &&
	     (!(s->flags & V4L2_HEVC_SPS_FLAG_SPS_TEMPORAL_MVP_ENABLED) ||
	      (inter && sl->collocated_ref_idx >= active[bidir &&
		      !(sl->flags & V4L2_HEVC_SLICE_PARAMS_FLAG_COLLOCATED_FROM_L0)]))))
		return -EINVAL;
	min_cb = s->log2_min_luma_coding_block_size_minus3 + 3;
	log_ctu = min_cb + s->log2_diff_max_min_luma_coding_block_size;
	/* CTU16 SAO still differs at chroma CTU corners on the target.
	 * Reject this unqualified combination instead of returning corrupt pixels. */
	if (log_ctu == 4 && (s->flags & V4L2_HEVC_SPS_FLAG_SAMPLE_ADAPTIVE_OFFSET))
		return -EOPNOTSUPP;
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
	/* One independent slice covers the complete picture. WPP contributes
	 * one substream boundary per CTU row after the first. Factory software
	 * consumes these header offsets without programming an address table;
	 * bit 0 of 0x105c enables hardware CABAC row synchronization. */
	if (sl->num_entry_point_offsets !=
	    (p->flags & V4L2_HEVC_PPS_FLAG_ENTROPY_CODING_SYNC_ENABLED ? rows - 1 : 0) ||
	    j->num_entry_point_offsets != sl->num_entry_point_offsets ||
	    j->num_entry_point_offsets > W202_HEVC_MAX_ENTRY_POINTS ||
	    (j->num_entry_point_offsets && !j->entry_point_offsets) ||
	    sl->data_byte_offset >= j->input_bytes)
		return -EINVAL;
	entry_bytes = 0;
	for (i = 0; i < j->num_entry_point_offsets; i++) {
		entry_bytes += j->entry_point_offsets[i];
		if (!j->entry_point_offsets[i] ||
		    entry_bytes >= j->input_bytes - sl->data_byte_offset)
			return -EINVAL;
	}
	width = cols << log_ctu; height = rows << log_ctu;
	y_size = (__u64)width * height;
	aux_size = cols * rows * (log_ctu == 4 ? 32 : log_ctu == 5 ? 80 : 320) + 1024;
	if (!valid_span(j->input_address, j->input_capacity, (__u64)j->input_bytes + W202_VPU_BSM_PADDING) ||
	    !valid_span(j->output_address, j->output_capacity, y_size * 3 / 2) ||
	    !valid_span(j->auxiliary_address, j->auxiliary_capacity, aux_size))
		return -EINVAL;
	for (list = 0; list < 2; list++) {
		if (active[list] && !j->reference_lists[list])
			return -EINVAL;
		for (i = 0; i < active[list]; i++) {
			const struct w202_hevc_reference *r = &j->reference_lists[list][i];
			unsigned int index = list ? sl->ref_idx_l1[i] : sl->ref_idx_l0[i];

			if (index >= d->num_active_dpb_entries || d->dpb[index].field_pic ||
			    (d->dpb[index].flags & ~V4L2_HEVC_DPB_ENTRY_LONG_TERM_REFERENCE) ||
			    r->frame_id > 31 ||
			    !valid_span(r->address, r->capacity, y_size * 3 / 2) ||
			    !valid_span(r->auxiliary_address, r->auxiliary_capacity, aux_size))
				return -EINVAL;
		}
	}
	memset(c, 0, sizeof(*c));
	c->codec = W202_VPU_HEVC;
	c->width = width; c->height = height;
	c->stream_address = j->input_address; c->stream_bytes = j->input_bytes;
	c->header_bits = j->header_bits;
	emit(c, 0x1020, 0x11000187 | ((__u32)(inter + bidir) << 16));
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
		(!!weighted << 15) |
		(PF(WEIGHTED_PRED) << 16) | (PF(WEIGHTED_BIPRED) << 17) |
		(PF(CU_QP_DELTA_ENABLED) << 21) |
		(((log_ctu - p->diff_cu_qp_delta_depth + 1) & 3) << 22) |
		((log_ctu & 3) << 24) | (((min_cb + 1) & 3) << 26) |
		(PF(TRANSFORM_SKIP_ENABLED) << 29) | (PF(SIGN_DATA_HIDING_ENABLED) << 30);
	emit(c, 0x1048, value);
	emit(c, 0x104c, (active[0] << 20) | (active[1] << 25) |
	     (((active[0] - 1) & 31) << 10) | (((active[1] - 1) & 31) << 15) |
	     ((__u32)sl->slice_cb_qp_offset & 31) |
	     (((__u32)sl->slice_cr_qp_offset & 31) << 5));
	emit(c, 0x1050, d->pic_order_cnt_val);
	value = sl->collocated_ref_idx | ((!bidir || LF(COLLOCATED_FROM_L0)) << 4) |
		((bidir && active[1] < active[0] ? active[1] : active[0]) << 16) |
		(8U << 20) | (8U << 24) | (PF(LOOP_FILTER_ACROSS_TILES_ENABLED) << 28);
	if (weighted)
		value |= sl->pred_weight_table.luma_log2_weight_denom << 8 | chroma_denom << 12;
	emit(c, 0x1054, value);
	value = 0;
	for (list = 0; list < 2; list++)
		for (i = 0; i < active[list]; i++) {
			unsigned int index = list ? sl->ref_idx_l1[i] : sl->ref_idx_l0[i];

			if (d->dpb[index].flags & V4L2_HEVC_DPB_ENTRY_LONG_TERM_REFERENCE)
				value |= 1U << (list * 16 + i);
		}
	emit(c, 0x1058, value);
	/* PCM is disabled, but the factory still programs the inferred defaults. */
	pcm_min = s->log2_min_pcm_luma_coding_block_size_minus3 + 3;
	pcm_max = pcm_min + s->log2_diff_max_min_pcm_luma_coding_block_size;
	emit(c, 0x105c, PF(ENTROPY_CODING_SYNC_ENABLED) |
	     (LF(SLICE_SAO_LUMA) << 1) | (LF(SLICE_SAO_CHROMA) << 2) |
	     (PF(TRANSQUANT_BYPASS_ENABLED) << 3) | (((pcm_min + 1) & 3) << 4) |
	     (((pcm_max + 1) & 3) << 6) | (LF(SLICE_LOOP_FILTER_ACROSS_SLICES_ENABLED) << 8) |
	     (s->max_transform_hierarchy_depth_intra << 12) |
	     (s->max_transform_hierarchy_depth_inter << 16) |
	     ((5U - sl->five_minus_max_num_merge_cand) << 20) |
	     (((max_tb + 2) & 3) << 24) | (((min_tb + 2) & 3) << 26));
	emit(c, 0x1060, cols | (rows << 8));
	emit(c, 0x1064, SF(SAMPLE_ADAPTIVE_OFFSET) | (SF(AMP_ENABLED) << 1) |
	     (LF(MVD_L1_ZERO) << 2) | (LF(SLICE_TEMPORAL_MVP_ENABLED) << 6) |
	     (SF(STRONG_INTRA_SMOOTHING_ENABLED) << 5) |
	     (((p->log2_parallel_merge_level_minus2 + 8) & 7) << 8) |
	     (((__u32)p->pps_cb_qp_offset & 31) << 16) |
	     (((__u32)p->pps_cr_qp_offset & 31) << 24));
	for (list = 0; list < 2; list++) {
		__u32 ids[16];
		unsigned int word, slot;

		for (i = 0; i < 16; i++)
			ids[i] = i < active[list] ? j->reference_lists[list][i].frame_id : 0;
		for (word = 0; word < 3; word++) {
			value = ((ids[15] >> (2 * word)) & 3) << 30;
			for (slot = 0; slot < 5; slot++)
				value |= ids[word * 5 + slot] << (slot * 6);
			emit(c, 0x1068 + list * 12 + word * 4, inter ? value : 0);
		}
		for (i = 0; i < 32; i++) {
			const struct w202_hevc_reference *r = i < active[list] ?
				&j->reference_lists[list][i] : NULL;
			__u64 address = r ? r->address : j->output_address;
			__u64 auxiliary = r ? r->auxiliary_address : j->auxiliary_address;

			emit(c, 0x2400 + list * 0x80 + i * 4, address >> 4);
			emit(c, 0x2500 + list * 0x80 + i * 4, (address + y_size) >> 4);
			emit(c, 0x2600 + list * 0x80 + i * 4, auxiliary >> 4);
			if (i < 16) {
				unsigned int index = list ? sl->ref_idx_l1[i] : sl->ref_idx_l0[i];

				emit(c, 0x1200 + i * 8 + list * 4,
				     r ? d->dpb[index].pic_order_cnt_val : 0);
			}
		}
		if (weighted)
			for (i = 0; i < active[list]; i++) {
				const struct v4l2_hevc_pred_weight_table *w = &sl->pred_weight_table;
				int y = (1 << w->luma_log2_weight_denom) +
					(list ? w->delta_luma_weight_l1[i] : w->delta_luma_weight_l0[i]);
				const __s8 *chroma = list ? w->delta_chroma_weight_l1[i] : w->delta_chroma_weight_l0[i];
				const __s8 *offset = list ? w->chroma_offset_l1[i] : w->chroma_offset_l0[i];

				emit(c, 0x1280 + list * 0x80 + i * 8, ((__u32)y & 511) |
				     (((__u32)((1 << chroma_denom) + chroma[0]) & 511) << 9) |
				     (((__u32)((1 << chroma_denom) + chroma[1]) & 511) << 18));
				emit(c, 0x1284 + list * 0x80 + i * 8,
				     ((__u32)(list ? w->luma_offset_l1[i] : w->luma_offset_l0[i]) & 255) |
				     (((__u32)offset[0] & 255) << 8) | (((__u32)offset[1] & 255) << 16));
			}
	}
	emit(c, 0x1088, j->output_address >> 4);
	emit(c, 0x108c, (j->output_address + y_size) >> 4);
	emit(c, 0x1090, j->auxiliary_address >> 4);
	cabac_table = 2 - sl->slice_type;
	if (inter && LF(CABAC_INIT))
		cabac_table ^= 3;
	for (i = 0; i < 154; i++) {
		state = ((w202_hevc_cabac_init[cabac_table][i][0] * qp) >> 4) +
			w202_hevc_cabac_init[cabac_table][i][1];
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

int w202_hevc_build_idr(const struct w202_hevc_job *job, struct w202_vpu_config *config)
{
	if (!job || !job->decode || !(job->decode->flags & V4L2_HEVC_DECODE_PARAM_FLAG_IDR_PIC))
		return -EOPNOTSUPP;
	return w202_hevc_build_ipb(job, config);
}
