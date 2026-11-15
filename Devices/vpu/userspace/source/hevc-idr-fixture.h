/* SPDX-License-Identifier: GPL-2.0-only */
/* Restricted test parser: one-layer, Main 8-bit SPS/PPS and independent IDRs.
 * Used only by diagnostics. It never runs in the kernel. */
#ifndef W202_HEVC_IDR_FIXTURE_H
#define W202_HEVC_IDR_FIXTURE_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../../source/w202_vpu_hevc.h"

struct hevc_fixture {
	struct v4l2_ctrl_hevc_sps sps;
	struct v4l2_ctrl_hevc_pps pps;
	struct v4l2_ctrl_hevc_slice_params slice;
	struct v4l2_ctrl_hevc_decode_params decode;
	const unsigned char *nal;
	size_t nal_size;
	unsigned int crop_left, crop_top, width, height;
};
struct hevc_bits { const unsigned char *p; size_t size, bit; int bad; };
static uint32_t hevc_get(struct hevc_bits *b, unsigned int n)
{
	uint32_t v = 0;
	if (n > 32 || b->bit > b->size * 8 || n > b->size * 8 - b->bit) {
		b->bad = 1; return 0;
	}
	for (unsigned int i = 0; i < n; i++, b->bit++)
		v = (v << 1) | ((b->p[b->bit / 8] >> (7 - b->bit % 8)) & 1);
	return v;
}
static uint32_t hevc_ue(struct hevc_bits *b)
{
	unsigned int z = 0;
	while (!b->bad && !hevc_get(b, 1))
		if (++z > 30) { b->bad = 1; return 0; }
	return ((1U << z) - 1) + hevc_get(b, z);
}
static int hevc_se(struct hevc_bits *b)
{
	uint32_t v = hevc_ue(b);
	return v & 1 ? (int)((v + 1) / 2) : -(int)(v / 2);
}
static int hevc_sps(struct hevc_bits *b, struct hevc_fixture *f)
{
	struct v4l2_ctrl_hevc_sps *s = &f->sps;
	unsigned int layers, profile[7] = {}, level[7] = {}, right = 0, bottom = 0;

	memset(s, 0, sizeof(*s));
	s->video_parameter_set_id = hevc_get(b, 4);
	layers = hevc_get(b, 3);
	s->sps_max_sub_layers_minus1 = layers;
	if (layers > 6) return -1;
	hevc_get(b, 1);
	/* profile_tier_level: general profile, constraints and level. */
	hevc_get(b, 32); hevc_get(b, 32); hevc_get(b, 32);
	for (unsigned int i = 0; i < layers; i++) {
		profile[i] = hevc_get(b, 1); level[i] = hevc_get(b, 1);
	}
	if (layers)
		for (unsigned int i = layers; i < 8; i++) hevc_get(b, 2);
	for (unsigned int i = 0; i < layers; i++) {
		if (profile[i]) { hevc_get(b, 32); hevc_get(b, 32); hevc_get(b, 24); }
		if (level[i]) hevc_get(b, 8);
	}
	s->seq_parameter_set_id = hevc_ue(b);
	s->chroma_format_idc = hevc_ue(b);
	if (s->chroma_format_idc != 1) return -1;
	unsigned int width = hevc_ue(b), height = hevc_ue(b);
	if (!width || width > 1920 || !height || height > 1088) return -1;
	s->pic_width_in_luma_samples = width; s->pic_height_in_luma_samples = height;
	f->crop_left = f->crop_top = 0;
	if (hevc_get(b, 1)) {
		f->crop_left = 2 * hevc_ue(b); right = 2 * hevc_ue(b);
		f->crop_top = 2 * hevc_ue(b); bottom = 2 * hevc_ue(b);
	}
	if (f->crop_left + right >= width || f->crop_top + bottom >= height) return -1;
	f->width = width - f->crop_left - right; f->height = height - f->crop_top - bottom;
	s->bit_depth_luma_minus8 = hevc_ue(b); s->bit_depth_chroma_minus8 = hevc_ue(b);
	if (s->bit_depth_luma_minus8 || s->bit_depth_chroma_minus8) return -1;
	s->log2_max_pic_order_cnt_lsb_minus4 = hevc_ue(b);
	unsigned int ordering = hevc_get(b, 1);
	for (unsigned int i = ordering ? 0 : layers; i <= layers; i++) {
		s->sps_max_dec_pic_buffering_minus1 = hevc_ue(b);
		s->sps_max_num_reorder_pics = hevc_ue(b);
		s->sps_max_latency_increase_plus1 = hevc_ue(b);
	}
	s->log2_min_luma_coding_block_size_minus3 = hevc_ue(b);
	s->log2_diff_max_min_luma_coding_block_size = hevc_ue(b);
	s->log2_min_luma_transform_block_size_minus2 = hevc_ue(b);
	s->log2_diff_max_min_luma_transform_block_size = hevc_ue(b);
	s->max_transform_hierarchy_depth_inter = hevc_ue(b);
	s->max_transform_hierarchy_depth_intra = hevc_ue(b);
	if (hevc_get(b, 1)) return -1; /* scaling lists */
	if (hevc_get(b, 1)) s->flags |= V4L2_HEVC_SPS_FLAG_AMP_ENABLED;
	if (hevc_get(b, 1)) s->flags |= V4L2_HEVC_SPS_FLAG_SAMPLE_ADAPTIVE_OFFSET;
	if (hevc_get(b, 1)) return -1; /* PCM */
	s->num_short_term_ref_pic_sets = hevc_ue(b);
	if (s->num_short_term_ref_pic_sets) return -1;
	if (hevc_get(b, 1)) return -1; /* long-term SPS references */
	if (hevc_get(b, 1)) s->flags |= V4L2_HEVC_SPS_FLAG_SPS_TEMPORAL_MVP_ENABLED;
	if (hevc_get(b, 1)) s->flags |= V4L2_HEVC_SPS_FLAG_STRONG_INTRA_SMOOTHING_ENABLED;
	return b->bad ? -1 : 0;
}
static int hevc_pps(struct hevc_bits *b, struct hevc_fixture *f)
{
	struct v4l2_ctrl_hevc_pps *p = &f->pps;
	memset(p, 0, sizeof(*p));
	p->pic_parameter_set_id = hevc_ue(b);
	if (hevc_ue(b) != f->sps.seq_parameter_set_id) return -1;
#define PFLAG(name) do { if (hevc_get(b, 1)) p->flags |= V4L2_HEVC_PPS_FLAG_##name; } while (0)
	PFLAG(DEPENDENT_SLICE_SEGMENT_ENABLED); PFLAG(OUTPUT_FLAG_PRESENT);
	p->num_extra_slice_header_bits = hevc_get(b, 3);
	PFLAG(SIGN_DATA_HIDING_ENABLED); PFLAG(CABAC_INIT_PRESENT);
	p->num_ref_idx_l0_default_active_minus1 = hevc_ue(b);
	p->num_ref_idx_l1_default_active_minus1 = hevc_ue(b);
	p->init_qp_minus26 = hevc_se(b);
	PFLAG(CONSTRAINED_INTRA_PRED); PFLAG(TRANSFORM_SKIP_ENABLED); PFLAG(CU_QP_DELTA_ENABLED);
	if (p->flags & V4L2_HEVC_PPS_FLAG_CU_QP_DELTA_ENABLED) p->diff_cu_qp_delta_depth = hevc_ue(b);
	p->pps_cb_qp_offset = hevc_se(b); p->pps_cr_qp_offset = hevc_se(b);
	PFLAG(PPS_SLICE_CHROMA_QP_OFFSETS_PRESENT); PFLAG(WEIGHTED_PRED); PFLAG(WEIGHTED_BIPRED);
	PFLAG(TRANSQUANT_BYPASS_ENABLED); PFLAG(TILES_ENABLED); PFLAG(ENTROPY_CODING_SYNC_ENABLED);
	if (p->flags & (V4L2_HEVC_PPS_FLAG_TILES_ENABLED | V4L2_HEVC_PPS_FLAG_ENTROPY_CODING_SYNC_ENABLED)) return -1;
	PFLAG(PPS_LOOP_FILTER_ACROSS_SLICES_ENABLED); PFLAG(DEBLOCKING_FILTER_CONTROL_PRESENT);
	if (p->flags & V4L2_HEVC_PPS_FLAG_DEBLOCKING_FILTER_CONTROL_PRESENT) {
		PFLAG(DEBLOCKING_FILTER_OVERRIDE_ENABLED); PFLAG(PPS_DISABLE_DEBLOCKING_FILTER);
		if (!(p->flags & V4L2_HEVC_PPS_FLAG_PPS_DISABLE_DEBLOCKING_FILTER)) {
			p->pps_beta_offset_div2 = hevc_se(b); p->pps_tc_offset_div2 = hevc_se(b);
		}
	}
	if (hevc_get(b, 1)) return -1; /* PPS scaling lists */
	PFLAG(LISTS_MODIFICATION_PRESENT);
	p->log2_parallel_merge_level_minus2 = hevc_ue(b);
	PFLAG(SLICE_SEGMENT_HEADER_EXTENSION_PRESENT);
#undef PFLAG
	return b->bad ? -1 : 0;
}
static int hevc_slice(struct hevc_bits *b, const size_t *map, struct hevc_fixture *f, unsigned int type)
{
	struct v4l2_ctrl_hevc_slice_params *sl = &f->slice;
	struct v4l2_ctrl_hevc_pps *p = &f->pps;
	int disabled = !!(p->flags & V4L2_HEVC_PPS_FLAG_PPS_DISABLE_DEBLOCKING_FILTER);
	memset(sl, 0, sizeof(*sl));
	memset(&f->decode, 0, sizeof(f->decode));
	sl->nal_unit_type = type; sl->nuh_temporal_id_plus1 = 1;
	sl->bit_size = f->nal_size * 8;
	f->decode.flags = V4L2_HEVC_DECODE_PARAM_FLAG_IDR_PIC | V4L2_HEVC_DECODE_PARAM_FLAG_IRAP_PIC;
	if (!hevc_get(b, 1)) return -1;
	if (hevc_get(b, 1)) f->decode.flags |= V4L2_HEVC_DECODE_PARAM_FLAG_NO_OUTPUT_OF_PRIOR;
	if (hevc_ue(b) != p->pic_parameter_set_id) return -1;
	hevc_get(b, p->num_extra_slice_header_bits);
	sl->slice_type = hevc_ue(b);
	if (sl->slice_type != V4L2_HEVC_SLICE_TYPE_I) return -1;
	if (p->flags & V4L2_HEVC_PPS_FLAG_OUTPUT_FLAG_PRESENT) hevc_get(b, 1);
	if (f->sps.flags & V4L2_HEVC_SPS_FLAG_SAMPLE_ADAPTIVE_OFFSET) {
		if (hevc_get(b, 1)) sl->flags |= V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_SAO_LUMA;
		if (hevc_get(b, 1)) sl->flags |= V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_SAO_CHROMA;
	}
	sl->slice_qp_delta = hevc_se(b);
	if (p->flags & V4L2_HEVC_PPS_FLAG_PPS_SLICE_CHROMA_QP_OFFSETS_PRESENT) {
		sl->slice_cb_qp_offset = hevc_se(b); sl->slice_cr_qp_offset = hevc_se(b);
	}
	sl->slice_beta_offset_div2 = p->pps_beta_offset_div2;
	sl->slice_tc_offset_div2 = p->pps_tc_offset_div2;
	if ((p->flags & V4L2_HEVC_PPS_FLAG_DEBLOCKING_FILTER_OVERRIDE_ENABLED) && hevc_get(b, 1)) {
		disabled = hevc_get(b, 1);
		if (!disabled) {
			sl->slice_beta_offset_div2 = hevc_se(b); sl->slice_tc_offset_div2 = hevc_se(b);
		}
	}
	if (disabled) sl->flags |= V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_DEBLOCKING_FILTER_DISABLED;
	if ((p->flags & V4L2_HEVC_PPS_FLAG_PPS_LOOP_FILTER_ACROSS_SLICES_ENABLED) &&
	    (!disabled || (sl->flags & (V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_SAO_LUMA |
				       V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_SAO_CHROMA))))
		if (hevc_get(b, 1)) sl->flags |= V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_LOOP_FILTER_ACROSS_SLICES_ENABLED;
	if (p->flags & V4L2_HEVC_PPS_FLAG_SLICE_SEGMENT_HEADER_EXTENSION_PRESENT) {
		unsigned int count = hevc_ue(b);
		if (count > 512) return -1;
		for (unsigned int i = 0; i < count; i++) hevc_get(b, 8);
	}
	if (hevc_get(b, 1) != 1) return -1;
	while (b->bit & 7) if (hevc_get(b, 1) != 0 || b->bad) return -1;
	if (b->bad || b->bit / 8 >= b->size) return -1;
	sl->data_byte_offset = map[b->bit / 8];
	return 0;
}
static size_t hevc_start(const unsigned char *p, size_t n, size_t at, size_t *header)
{
	for (size_t i = at; i + 3 < n; i++)
		if (!p[i] && !p[i + 1] && (p[i + 2] == 1 || (!p[i + 2] && p[i + 3] == 1))) {
			*header = i + (p[i + 2] == 1 ? 3 : 4); return i;
		}
	*header = n; return n;
}
static int hevc_parse_idr(const unsigned char *stream, size_t bytes, unsigned int wanted, struct hevc_fixture *f)
{
	size_t nal, start = hevc_start(stream, bytes, 0, &nal);
	unsigned int frame = 0;
	int have_sps = 0, have_pps = 0, ret = -1;
	unsigned char *rbsp = malloc(bytes);
	size_t *map = malloc(bytes * sizeof(*map));
	if (!rbsp || !map) goto out;
	memset(f, 0, sizeof(*f));
	while (start < bytes && nal + 2 < bytes) {
		size_t next_nal, next = hevc_start(stream, bytes, nal, &next_nal), end = next, n = 0;
		while (end > nal + 2 && !stream[end - 1]) end--;
		if ((stream[nal] & 0x81) || stream[nal + 1] != 1) goto out;
		unsigned int type = (stream[nal] >> 1) & 63, zeros = 0;
		for (size_t i = nal; i < end; i++) {
			if (i >= nal + 2 && zeros == 2 && stream[i] == 3) {
				if (i + 1 == end || stream[i + 1] > 3) goto out;
				zeros = 0; continue;
			}
			rbsp[n] = stream[i]; map[n++] = i - nal;
			zeros = stream[i] ? 0 : zeros + 1;
		}
		struct hevc_bits b = {.p = rbsp, .size = n, .bit = 16};
		if (type == 33) { if (hevc_sps(&b, f)) goto out; have_sps = 1; }
		if (type == 34) { if (!have_sps || hevc_pps(&b, f)) goto out; have_pps = 1; }
		if (type <= 31) {
			if (!have_sps || !have_pps || (type != 19 && type != 20)) goto out;
			if (frame++ == wanted) {
				f->nal = stream + nal; f->nal_size = end - nal;
				ret = hevc_slice(&b, map, f, type); goto out;
			}
		}
		start = next; nal = next_nal;
	}
out:
	free(rbsp); free(map); return ret;
}
#endif
