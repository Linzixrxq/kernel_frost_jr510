// SPDX-License-Identifier: GPL-2.0
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_file.h>
#include "komeda_dev.h"
#include "komeda_kms.h"
#include "komeda_framebuffer.h"
#include "komeda_color_mgmt.h"
#include "komeda_drm.h"
#include "malidp_math.h"

int komeda_plane_init_data_flow(struct drm_plane_state *st,
				struct komeda_crtc_state *kcrtc_st,
				struct komeda_data_flow_cfg *dflow)
{
	struct komeda_plane *kplane = to_kplane(st->plane);
	struct komeda_plane_state *kplane_st = to_kplane_st(st);
	struct drm_framebuffer *fb = st->fb;
	const struct komeda_format_caps *caps = to_kfb(fb)->format_caps;
	struct komeda_pipeline *pipe = kplane->layer ?
		kplane->layer->base.pipeline : kplane->atu->base.pipeline;

	memset(dflow, 0, sizeof(*dflow));

	dflow->pixel_blend_mode = st->pixel_blend_mode;
	dflow->layer_alpha = st->alpha >> 8;

	dflow->out_x = st->crtc_x;
	dflow->out_y = st->crtc_y;
	dflow->out_w = st->crtc_w;
	dflow->out_h = st->crtc_h;

	dflow->in_x = st->src_x >> 16;
	dflow->in_y = st->src_y >> 16;
	dflow->in_w = st->src_w >> 16;
	dflow->in_h = st->src_h >> 16;

	dflow->rot = drm_rotation_simplify(st->rotation, caps->supported_rots);
	if (!has_bits(dflow->rot, caps->supported_rots)) {
		DRM_DEBUG_ATOMIC("rotation(0x%x) isn't supported by %s.\n",
				 dflow->rot,
				 komeda_get_format_name(caps->fourcc,
							fb->modifier));
		return -EINVAL;
	}

	dflow->en_atu = !!kplane_st->vp_outrect;
	dflow->pixel_blend_mode = st->pixel_blend_mode;
	dflow->channel_scaling = kplane_st->channel_scaling;

	komeda_complete_data_flow_cfg(pipe, kcrtc_st, dflow, fb);

	/* debug flag, force en_split after comlete data flow cfg */
	if (kplane->force_layer_split)
		dflow->en_split = true;

	return 0;
}

/*
 * komeda_plane_prepare is the first check step, which does some sanity check,
 * compute the input data flow configuration according to the plane/crtc state
 * for the next check() operations.
 */
int komeda_plane_prepare(struct drm_plane *plane,
			 struct drm_plane_state *state)
{
	struct komeda_plane_state *kplane_st = to_kplane_st(state);
	struct komeda_data_flow_cfg *dflow = &kplane_st->dflow;
	struct drm_crtc_state *crtc_st;
	struct drm_plane_state *old;
	int err;

	if (!state->crtc || !state->fb)
		return 0;

	crtc_st = drm_atomic_get_crtc_state(state->state, state->crtc);
	if (IS_ERR(crtc_st) || !crtc_st->enable) {
		DRM_DEBUG_ATOMIC("Cannot update plane on a disabled CRTC.\n");
		return -EINVAL;
	}

	/* crtc is inactive, skip the resource assignment */
	if (!crtc_st->active)
		return 0;

	/* komeda only updates komeda state when crtc/plane is active, so once
	 * the user do the update during the inactive period, we may lose the
	 * changed flag when do the real komeda update in the active period,
	 * and lose the drm state updating that during the inactive period.
	 * To avoid such problem, force dirty all the changed flags when switch
	 * a plane from disable to enable.
	 */
	old = drm_atomic_get_old_plane_state(state->state, plane);
	if (!old->crtc) {
		kplane_st->color_mgmt_changed = true;
		kplane_st->atu_cfg_changed = U32_MAX;
	}

	err = komeda_plane_init_data_flow(state, to_kcrtc_st(crtc_st), dflow);
	if (err)
		return err;

	if (dflow->en_atu &&
	   (to_kcrtc(state->crtc)->side_by_side || dflow->en_split)) {
		DRM_DEBUG_ATOMIC("atu doesn't support layer split or SBS.\n");
		return -EINVAL;
	}

	/* Assign the komeda input pipeline to the data flow */
	dflow->layer = to_kplane(plane)->layer;

	return 0;
}

/**
 * komeda_plane_atomic_check - build input data flow
 * @plane: DRM plane
 * @state: the plane state object
 *
 * RETURNS:
 * Zero for success or -errno
 */
static int
komeda_plane_atomic_check(struct drm_plane *plane,
			  struct drm_plane_state *state)
{
	struct komeda_plane *kplane = to_kplane(plane);
	struct komeda_plane_state *kplane_st = to_kplane_st(state);
	struct komeda_data_flow_cfg *dflow = &kplane_st->dflow;
	struct komeda_layer *layer = dflow->layer;
	struct drm_crtc_state *crtc_st;
	struct komeda_crtc *kcrtc;
	struct komeda_crtc_state *kcrtc_st;
	bool is_fullscreen;
	int err;

	/* No layer assigned means the plane is disabled, skip it */
	if (!layer && !dflow->en_atu)
		return 0;

	crtc_st = drm_atomic_get_crtc_state(state->state, state->crtc);

	kcrtc = to_kcrtc(crtc_st->crtc);
	kcrtc_st = to_kcrtc_st(crtc_st);

	if (state->fb->format->is_yuv)
		kcrtc_st->yuv_plane_mask |= drm_plane_mask(plane);

	is_fullscreen = abs(state->crtc_w - kcrtc->base.mode.hdisplay) <= 16 &&
		        abs(state->crtc_h - kcrtc->base.mode.vdisplay) <= 16;

	if (is_fullscreen)
		kcrtc_st->fullscreen_plane_mask |= drm_plane_mask(plane);

	dflow->blending_zorder = state->normalized_zpos;
	if (dflow->en_atu || layer->base.pipeline == kcrtc->master)
		dflow->blending_zorder -= kcrtc_st->max_slave_zorder;
	if (dflow->blending_zorder < 0) {
		DRM_DEBUG_ATOMIC("%s zorder:%d < max_slave_zorder: %d.\n",
				 state->plane->name, state->normalized_zpos,
				 kcrtc_st->max_slave_zorder);
		return -EINVAL;
	}

	if (kcrtc->side_by_side)
		err = komeda_build_layer_sbs_data_flow(layer,
				kplane_st, kcrtc_st, dflow);
	else if (dflow->en_split)
		err = komeda_build_layer_split_data_flow(layer,
				kplane_st, kcrtc_st, dflow);
	else if (dflow->en_atu)
		err = komeda_atu_set_vp(kplane->atu, kplane_st, dflow);
	else
		err = komeda_build_layer_data_flow(layer,
				kplane_st, kcrtc_st, dflow);

	return err;
}

/* plane doesn't represent a real HW, so there is no HW update for plane.
 * komeda handles all the HW update in crtc->atomic_flush
 */
static void
komeda_plane_atomic_update(struct drm_plane *plane,
			   struct drm_plane_state *old_state)
{
}

static const struct drm_plane_helper_funcs komeda_plane_helper_funcs = {
	.atomic_check	= komeda_plane_atomic_check,
	.atomic_update	= komeda_plane_atomic_update,
};

static void komeda_plane_destroy(struct drm_plane *plane)
{
	drm_plane_cleanup(plane);

	kfree(to_kplane(plane));
}

static void komeda_plane_reset(struct drm_plane *plane)
{
	struct komeda_plane_state *state;
	struct komeda_plane *kplane = to_kplane(plane);

	if (plane->state)
		__drm_atomic_helper_plane_destroy_state(plane->state);

	kfree(plane->state);
	plane->state = NULL;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (state) {
		state->base.rotation = DRM_MODE_ROTATE_0;
		state->base.pixel_blend_mode = DRM_MODE_BLEND_PREMULTI;
		state->base.alpha = DRM_BLEND_ALPHA_OPAQUE;
		state->base.color_encoding = DRM_COLOR_YCBCR_BT601;
		state->base.color_range = DRM_COLOR_YCBCR_LIMITED_RANGE;
		/* Disable the color channel scaling by maximum value.*/
		state->channel_scaling = KOMEDA_MAX_CHANNEL_SCALING;
		if (kplane->layer)
			state->base.zpos = kplane->layer->base.id;
		else
			state->base.zpos = kplane->atu->base.id -
					   KOMEDA_COMPONENT_ATU0;
		plane->state = &state->base;
		plane->state->plane = plane;
	}
}

static struct drm_plane_state *
komeda_plane_atomic_duplicate_state(struct drm_plane *plane)
{
	struct komeda_plane_state *new;
	struct komeda_plane_state *old;

	if (WARN_ON(!plane->state))
		return NULL;

	new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return NULL;

	__drm_atomic_helper_plane_duplicate_state(plane, &new->base);

	old = to_kplane_st(plane->state);

	new->viewport_clamp = old->viewport_clamp;
	new->channel_scaling = old->channel_scaling;

	new->spline_coeff_r = komeda_drm_blob_get(old->spline_coeff_r);
	new->spline_coeff_g = komeda_drm_blob_get(old->spline_coeff_g);
	new->spline_coeff_b = komeda_drm_blob_get(old->spline_coeff_b);
	new->vp_outrect = komeda_drm_blob_get(old->vp_outrect);
	new->viewport_trans = komeda_drm_blob_get(old->viewport_trans);
	new->layer_project = komeda_drm_blob_get(old->layer_project);
	new->layer_quad = komeda_drm_blob_get(old->layer_quad);
	INIT_LIST_HEAD(&new->zlist_node);

	return &new->base;
}

static void
komeda_plane_atomic_destroy_state(struct drm_plane *plane,
				  struct drm_plane_state *state)
{
	__drm_atomic_helper_plane_destroy_state(state);
	kfree(to_kplane_st(state));
}

static int
komeda_plane_atomic_get_property(struct drm_plane *plane,
				 const struct drm_plane_state *state,
				 struct drm_property *property,
				 uint64_t *val)
{
	struct drm_device *drm = plane->dev;
	struct komeda_dev *mdev = drm->dev_private;
	struct komeda_kms_dev *kms = mdev->kms_dev;
	struct komeda_plane_state *st = to_kplane_st(state);
	struct komeda_plane *kplane = to_kplane(plane);

	if (property == kms->prop_spline_coeff_r)
		*val = (st->spline_coeff_r) ? st->spline_coeff_r->base.id : 0;
	else if (property == kms->prop_spline_coeff_g)
		*val = (st->spline_coeff_g) ? st->spline_coeff_g->base.id : 0;
	else if (property == kms->prop_spline_coeff_b)
		*val = (st->spline_coeff_b) ? st->spline_coeff_b->base.id : 0;
	else if (property == kplane->prop_viewport_outrect)
		*val = (st->vp_outrect) ? st->vp_outrect->base.id : 0;
	else if (property == kplane->prop_viewport_trans)
		*val = (st->viewport_trans) ? st->viewport_trans->base.id : 0;
	else if (property == kplane->prop_layer_projection)
		*val = (st->layer_project) ? st->layer_project->base.id : 0;
	else if (property == kplane->prop_layer_quad)
		*val = (st->layer_quad) ? st->layer_quad->base.id : 0;
	else if (property == kplane->prop_channel_scaling)
		*val = st->channel_scaling;
	else if (property == kplane->degamma_lut_property)
		*val = (st->degamma_lut) ? st->degamma_lut->base.id : 0;
	else if (property == kplane->ctm_property)
		*val = (st->ctm) ? st->ctm->base.id : 0;
	else if (property == kplane->ctm_ext_property)
		*val = (st->ctm) ? st->ctm->base.id : 0;
	else if (property == kplane->gamma_lut_property)
		*val = (st->gamma_lut) ? st->gamma_lut->base.id : 0;

	return 0;
}

static int
komeda_plane_atomic_set_property(struct drm_plane *plane,
				 struct drm_plane_state *state,
				 struct drm_property *property,
				 uint64_t val)
{
	struct drm_device *drm = plane->dev;
	struct komeda_plane *kplane = to_kplane(plane);
	struct komeda_dev *mdev = drm->dev_private;
	struct komeda_kms_dev *kms = mdev->kms_dev;
	struct komeda_plane_state *kplane_st = to_kplane_st(state);
	bool replaced = false;
	int ret = 0;

	if (property == kms->prop_spline_coeff_r) {
		ret = drm_property_replace_blob_from_id(drm,
					&kplane_st->spline_coeff_r,
					val,
					KOMEDA_SPLINE_COEFF_SIZE * sizeof(u32),
					KOMEDA_SPLINE_COEFF_SIZE, &replaced);
		kplane_st->spline_coeff_r_changed = replaced;
	} else if (property == kms->prop_spline_coeff_g) {
		ret = drm_property_replace_blob_from_id(drm,
					&kplane_st->spline_coeff_g,
					val,
					KOMEDA_SPLINE_COEFF_SIZE * sizeof(u32),
					KOMEDA_SPLINE_COEFF_SIZE, &replaced);
		kplane_st->spline_coeff_g_changed = replaced;
	} else if (property == kms->prop_spline_coeff_b) {
		ret = drm_property_replace_blob_from_id(drm,
					&kplane_st->spline_coeff_b,
					val,
					KOMEDA_SPLINE_COEFF_SIZE * sizeof(u32),
					KOMEDA_SPLINE_COEFF_SIZE, &replaced);
		kplane_st->spline_coeff_b_changed = replaced;
	} else if (property == kplane->prop_viewport_outrect) {
		ret = drm_property_replace_blob_from_id(drm,
					&kplane_st->vp_outrect,
					val,
					4 * sizeof(uint32_t),
					-1, &replaced);
		kplane_st->vp_rect_changed = replaced;
	} else if (property == kplane->prop_viewport_trans) {
		ret = drm_property_replace_blob_from_id(drm,
					&kplane_st->viewport_trans,
					val,
					sizeof(struct malidp_matrix4),
					-1, &replaced);
		kplane_st->mat_coeff_changed = replaced;
	} else if (property == kplane->prop_layer_projection) {
		ret = drm_property_replace_blob_from_id(drm,
					&kplane_st->layer_project,
					val,
					sizeof(struct malidp_matrix4),
					-1, &replaced);
		kplane_st->mat_coeff_changed = replaced;
	} else if (property == kplane->prop_layer_quad) {
		ret = drm_property_replace_blob_from_id(drm,
					&kplane_st->layer_quad,
					val,
					sizeof(struct malidp_matrix4),
					-1, &replaced);
		kplane_st->mat_coeff_changed = replaced;
	} else if (property == kplane->prop_viewport_clamp) {
		kplane_st->viewport_clamp = !!val;
	} else if (property == kplane->prop_channel_scaling) {
		kplane_st->channel_scaling = val;
	} else if (property == kplane->degamma_lut_property) {
		ret = drm_property_replace_blob_from_id(drm,
						&kplane_st->degamma_lut,
						val, -1, sizeof(struct drm_color_lut),
						&replaced);
		kplane_st->color_mgmt_changed |= replaced;
	} else if (property == kplane->ctm_property) {
		ret = drm_property_replace_blob_from_id(drm,
						&kplane_st->ctm,
						val,
						sizeof(struct drm_color_ctm), -1,
						&replaced);
		kplane_st->color_mgmt_changed |= replaced;
	} else if (property == kplane->ctm_ext_property) {
		ret = drm_property_replace_blob_from_id(drm,
						&kplane_st->ctm,
						val,
						sizeof(struct color_ctm_ext), -1,
						&replaced);
		kplane_st->color_mgmt_changed |= replaced;
	} else if (property == kplane->gamma_lut_property) {
		ret = drm_property_replace_blob_from_id(drm,
						&kplane_st->gamma_lut,
						val, -1, sizeof(struct drm_color_lut),
						&replaced);
		kplane_st->color_mgmt_changed |= replaced;
	}

	return ret;
}

static bool
komeda_plane_format_mod_supported(struct drm_plane *plane,
				  u32 format, u64 modifier)
{
	struct komeda_dev *mdev = plane->dev->dev_private;
	struct komeda_plane *kplane = to_kplane(plane);
	u32 layer_type = kplane->layer ?
			 kplane->layer->layer_type : kplane->atu->layer_type;

	return komeda_format_mod_supported(&mdev->fmt_tbl, layer_type,
					   format, modifier, 0);
}

#ifdef CONFIG_DEBUG_FS
static int
komeda_plane_debugfs_init(struct drm_plane *plane)
{
	struct komeda_plane *kplane = to_kplane(plane);
	struct dentry *dir;
	char name[32];

	if (!kplane->layer || !kplane->layer->right)
		return 0;

	snprintf(name, sizeof(name), "plane-%d", drm_plane_index(plane));

	dir = debugfs_create_dir(name, plane->dev->primary->debugfs_root);
	if (!dir) {
		DRM_DEBUG_ATOMIC("Cannot create debugfs dir for %s\n", name);
		return 0;
	}

	debugfs_create_bool("force_layer_split", 0644, dir,
			    &kplane->force_layer_split);

	return 0;
}
#endif /*CONFIG_DEBUG_FS*/

static const struct drm_plane_funcs komeda_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= komeda_plane_destroy,
	.reset			= komeda_plane_reset,
	.atomic_duplicate_state	= komeda_plane_atomic_duplicate_state,
	.atomic_destroy_state	= komeda_plane_atomic_destroy_state,
	.atomic_get_property	= komeda_plane_atomic_get_property,
	.atomic_set_property	= komeda_plane_atomic_set_property,
	.format_mod_supported	= komeda_plane_format_mod_supported,
#ifdef CONFIG_DEBUG_FS
	.late_register		= komeda_plane_debugfs_init,
#endif /*CONFIG_DEBUG_FS*/
};

/* for komeda, which is pipeline can be share between crtcs */
static u32 get_possible_crtcs(struct komeda_kms_dev *kms,
			      struct komeda_pipeline *pipe)
{
	struct komeda_crtc *crtc;
	u32 possible_crtcs = 0;
	int i;

	for (i = 0; i < kms->n_crtcs; i++) {
		crtc = &kms->crtcs[i];

		if ((pipe == crtc->master) || (pipe == crtc->slave))
			possible_crtcs |= BIT(i);
	}

	return possible_crtcs;
}

static void
komeda_set_crtc_plane_mask(struct komeda_kms_dev *kms,
			   struct komeda_component *c,
			   struct drm_plane *plane)
{
	struct komeda_crtc *kcrtc;
	int i;

	/* global always use as master */
	if (c->global)
		return;

	for (i = 0; i < kms->n_crtcs; i++) {
		kcrtc = &kms->crtcs[i];

		if (c->pipeline == kcrtc->slave)
			kcrtc->slave_planes |= BIT(drm_plane_index(plane));
	}
}

/* use Layer0 as primary */
static u32 get_plane_type(struct komeda_kms_dev *kms,
			  struct komeda_component *c)
{
	bool is_primary = (c->id == KOMEDA_COMPONENT_LAYER0);

	return is_primary ? DRM_PLANE_TYPE_PRIMARY : DRM_PLANE_TYPE_OVERLAY;
}

static int
attach_atu_property_to_plane(struct komeda_kms_dev *kms,
			     struct komeda_plane *kplane,
			     struct komeda_atu *atu)
{
	struct drm_plane *plane = &kplane->base;

	kplane->prop_viewport_outrect = drm_property_create(plane->dev,
			DRM_MODE_PROP_ATOMIC | DRM_MODE_PROP_BLOB,
			"VIEWPORT_OUTRECT", 0);

	if (!kplane->prop_viewport_outrect)
		return -ENOMEM;

	kplane->prop_viewport_trans = drm_property_create(plane->dev,
			DRM_MODE_PROP_ATOMIC | DRM_MODE_PROP_BLOB,
			"VIEWPORT_TRANS", 0);
	if (!kplane->prop_viewport_trans)
		return -ENOMEM;

	kplane->prop_layer_projection = drm_property_create(plane->dev,
			DRM_MODE_PROP_ATOMIC | DRM_MODE_PROP_BLOB,
			"LAYER_PROJECTION", 0);

	if (!kplane->prop_layer_projection)
		return -ENOMEM;

	kplane->prop_layer_quad = drm_property_create(plane->dev,
			DRM_MODE_PROP_ATOMIC | DRM_MODE_PROP_BLOB,
			"LAYER_QUAD", 0);

	if (!kplane->prop_layer_quad)
		return -ENOMEM;

	/* property: layer clamp */
	kplane->prop_viewport_clamp = drm_property_create_range(plane->dev,
			DRM_MODE_PROP_ATOMIC, "VIEWPORT_CLAMP", 0, 1);
	if (!kplane->prop_viewport_clamp)
		return -ENOMEM;

	/* other property */
	drm_object_attach_property(&plane->base, kplane->prop_viewport_outrect, 0);
	drm_object_attach_property(&plane->base, kplane->prop_viewport_trans, 0);
	drm_object_attach_property(&plane->base, kplane->prop_layer_projection, 0);
	drm_object_attach_property(&plane->base, kplane->prop_layer_quad, 0);
	drm_object_attach_property(&plane->base, kplane->prop_viewport_clamp, 0);
	drm_object_attach_property(&plane->base, kms->prop_spline_coeff_r, 0);
	drm_object_attach_property(&plane->base, kms->prop_spline_coeff_g, 0);
	drm_object_attach_property(&plane->base, kms->prop_spline_coeff_b, 0);

	return 0;
}

static int
create_plane_private_properties(struct komeda_plane *kplane)
{
	struct drm_plane *plane = &kplane->base;
	struct komeda_layer *layer = kplane->layer;
	struct komeda_atu *atu = kplane->atu;
	struct komeda_compiz *compiz = NULL;
	struct drm_property *prop = NULL;

	u32 flags = DRM_MODE_PROP_ATOMIC;

	compiz = layer ? layer->base.pipeline->compiz : atu->base.pipeline->compiz;
	if (compiz->support_channel_scaling) {
		prop = drm_property_create_range(plane->dev, flags, "channel_scaling",
					         0, KOMEDA_MAX_CHANNEL_SCALING);
		if (!prop)
			return -ENOMEM;
		kplane->prop_channel_scaling = prop;
		drm_object_attach_property(&plane->base, kplane->prop_channel_scaling,
				           KOMEDA_MAX_CHANNEL_SCALING);
	}

	return 0;
}

/**
* drm_plane_enable_color_mgmt - enable color management properties
* @plane: DRM Plane
* @plane_degamma_lut_size: the size of the degamma lut (before CSC)
* @plane_has_ctm: whether to attach ctm_property for CSC matrix
* @plane_gamma_lut_size: the size of the gamma lut (after CSC)
*
* This function lets the driver enable the color correction
* properties on a plane. This includes 3 degamma, csc and gamma
* properties that userspace can set and 2 size properties to inform
* the userspace of the lut sizes. Each of the properties are
* optional. The gamma and degamma properties are only attached if
* their size is not 0 and ctm_property is only attached if has_ctm is
* true.
*/
static void drm_plane_enable_color_mgmt(struct komeda_plane *kplane,
		u32 plane_degamma_lut_size,
		bool plane_has_ctm,
		u32 plane_gamma_lut_size)
{
	struct drm_plane *plane = &kplane->base;

	if (plane_degamma_lut_size) {
		drm_object_attach_property(&plane->base,
			kplane->degamma_lut_property, 0);
		drm_object_attach_property(&plane->base,
			kplane->degamma_lut_size_property,
			plane_degamma_lut_size);
	}

	if (plane_has_ctm) {
		drm_object_attach_property(&plane->base,
			kplane->ctm_property, 0);
		drm_object_attach_property(&plane->base,
			kplane->ctm_ext_property, 0);
	}

	if (plane_gamma_lut_size) {
		drm_object_attach_property(&plane->base,
			kplane->gamma_lut_property, 0);
		drm_object_attach_property(&plane->base,
			kplane->gamma_lut_size_property,
			plane_gamma_lut_size);
	}
}

/**
* DOC: Plane Color Properties
*
* Plane Color management or color space adjustments is supported
* through a set of 5 properties on the &drm_plane object.
*
* degamma_lut_property:
*     Blob property which allows a userspace to provide LUT values
*     to apply degamma curve using the h/w plane degamma processing
*     engine, thereby making the content as linear for further color
*     processing.
*
* degamma_lut_size_property:
*     Range Property to indicate size of the plane degamma LUT.
*
* ctm_property:
*	Blob property which allows a userspace to provide CTM coefficients
*	to do color space conversion or any other enhancement by doing a
*	matrix multiplication using the h/w CTM processing engine
*
* gamma_lut_property:
*	Blob property which allows a userspace to provide LUT values
*	to apply gamma/tone-mapping curve using the h/w plane gamma
*	processing engine, thereby making the content as non-linear
*	or to perform any tone mapping operation for HDR usecases.
*
* gamma_lut_size_property:
*	Range Property to indicate size of the plane gamma LUT.
*/
static int drm_plane_color_create_prop(struct drm_device *dev,
						struct komeda_plane *plane)
{
	struct drm_property *prop;

	prop = drm_property_create(dev, DRM_MODE_PROP_BLOB,
			"PLANE_DEGAMMA_LUT", 0);
	if (!prop)
		return -ENOMEM;

	plane->degamma_lut_property = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_IMMUTABLE,
			"PLANE_DEGAMMA_LUT_SIZE", 0,
			UINT_MAX);
	if (!prop)
		return -ENOMEM;
	plane->degamma_lut_size_property = prop;

	prop = drm_property_create(dev, DRM_MODE_PROP_BLOB,
			"PLANE_CTM", 0);
	if (!prop)
		return -ENOMEM;

	plane->ctm_property = prop;

	prop = drm_property_create(dev, DRM_MODE_PROP_BLOB,
			"PLANE_CTM_EXT", 0);
	if (!prop)
		return -ENOMEM;

	plane->ctm_ext_property = prop;

	prop = drm_property_create(dev, DRM_MODE_PROP_BLOB,
			"PLANE_GAMMA_LUT", 0);
	if (!prop)
		return -ENOMEM;

	plane->gamma_lut_property = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_IMMUTABLE,
			"PLANE_GAMMA_LUT_SIZE", 0,
			UINT_MAX);
	if (!prop)
		return -ENOMEM;

	plane->gamma_lut_size_property = prop;

	return 0;
}

static int komeda_plane_add(struct komeda_kms_dev *kms,
			    struct komeda_layer *layer, struct komeda_atu *atu)
{
	struct komeda_dev *mdev = kms->base->dev_private;
	struct komeda_component *master_comp;
	struct komeda_color_manager *color_mgr;
	struct komeda_plane *kplane;
	struct drm_plane *plane;
	u32 *formats, layer_type, n_formats = 0, zpos;
	int err;

	kplane = kzalloc(sizeof(*kplane), GFP_KERNEL);
	if (!kplane)
		return -ENOMEM;

	plane = &kplane->base;
	kplane->layer = layer;
	kplane->atu  = atu;
	kplane->force_layer_split = false;

	master_comp = layer ? &layer->base : &atu->base;
	layer_type = layer ? layer->layer_type : atu->layer_type;
	zpos = layer ? layer->base.id : atu->base.id - KOMEDA_COMPONENT_ATU0;

	formats = komeda_get_layer_fourcc_list(&mdev->fmt_tbl,
					       layer_type, &n_formats);

	err = drm_universal_plane_init(kms->base, plane,
			get_possible_crtcs(kms, master_comp->pipeline),
			&komeda_plane_funcs,
			formats, n_formats, komeda_supported_modifiers,
			get_plane_type(kms, master_comp),
			"%s", master_comp->name);

	komeda_put_fourcc_list(formats);

	if (err)
		goto cleanup;

	if (atu)
		attach_atu_property_to_plane(kms, kplane, atu);

	drm_plane_helper_add(plane, &komeda_plane_helper_funcs);

	err = create_plane_private_properties(kplane);
	if (err)
		goto cleanup;

	err = drm_plane_create_alpha_property(plane);
	if (err)
		goto cleanup;

	err = drm_plane_create_blend_mode_property(plane,
			BIT(DRM_MODE_BLEND_PIXEL_NONE) |
			BIT(DRM_MODE_BLEND_PREMULTI)   |
			BIT(DRM_MODE_BLEND_COVERAGE));
	if (err)
		goto cleanup;

	err = drm_plane_create_color_properties(plane,
			BIT(DRM_COLOR_YCBCR_BT601) |
			BIT(DRM_COLOR_YCBCR_BT709) |
			BIT(DRM_COLOR_YCBCR_BT2020),
			BIT(DRM_COLOR_YCBCR_LIMITED_RANGE) |
			BIT(DRM_COLOR_YCBCR_FULL_RANGE),
			DRM_COLOR_YCBCR_BT601,
			DRM_COLOR_YCBCR_LIMITED_RANGE);
	if (err)
		goto cleanup;

	err = drm_plane_color_create_prop(plane->dev, kplane);
	if (err)
		goto cleanup;

	color_mgr = atu ? & atu->color_mgr : &layer->color_mgr;
	drm_plane_enable_color_mgmt(kplane,
			color_mgr->igamma_mgr ? KOMEDA_COLOR_LUT_SIZE : 0,
			color_mgr->has_ctm,
			color_mgr->fgamma_mgr ? KOMEDA_COLOR_LUT_SIZE : 0);

	err = drm_plane_create_zpos_property(plane, zpos, 0, 8);
	if (err)
		goto cleanup;

	komeda_set_crtc_plane_mask(kms, master_comp, plane);

	/* below is the normal layer properties */
	if (!layer)
		return 0;

	err = drm_plane_create_rotation_property(plane, DRM_MODE_ROTATE_0,
						 layer->supported_rots);
	if (err)
		goto cleanup;

	return 0;
cleanup:
	komeda_plane_destroy(plane);
	return err;
}

int komeda_kms_create_plane_properties(struct komeda_kms_dev *kms,
				       struct komeda_dev *mdev)
{
	struct drm_device *drm = kms->base;
	struct drm_property *prop;
	bool has_atu = !!mdev->pipelines[0]->n_atus;

	if (!has_atu)
		return 0;

	prop = drm_property_create(drm,
			DRM_MODE_PROP_ATOMIC | DRM_MODE_PROP_BLOB,
			"SPLINE_COEFF_R", 0);
	if (!prop)
		return -ENOMEM;
	kms->prop_spline_coeff_r = prop;

	prop = drm_property_create(drm,
			DRM_MODE_PROP_ATOMIC | DRM_MODE_PROP_BLOB,
			"SPLINE_COEFF_G", 0);
	if (!prop)
		return -ENOMEM;
	kms->prop_spline_coeff_g = prop;

	prop = drm_property_create(drm,
			DRM_MODE_PROP_ATOMIC | DRM_MODE_PROP_BLOB,
			"SPLINE_COEFF_B", 0);
	if (!prop)
		return -ENOMEM;
	kms->prop_spline_coeff_b = prop;

	return 0;
}

static struct komeda_atu *
get_atu_by_layer(struct komeda_pipeline *pipe, struct komeda_layer *layer)
{
	int i;

	for (i = 0; i < pipe->n_atus; i++)
		if (pipe->atu[i]->slave_resource == layer->base.id)
			return pipe->atu[i];

	return NULL;
}

static struct komeda_plane *
get_atu_vp_buddy(struct komeda_kms_dev *kms, struct komeda_plane *kplane)
{
	struct komeda_plane *node;
	struct drm_plane *plane;

	drm_for_each_plane(plane, kms->base) {
		node = to_kplane(plane);
		if (!node->atu || node == kplane)
      			continue;
		if (node->atu == kplane->atu)
			return node;
	}
	return NULL;
}

int komeda_kms_add_planes(struct komeda_kms_dev *kms, struct komeda_dev *mdev)
{
	struct komeda_pipeline *pipe;
	struct komeda_plane *kplane;
	struct drm_plane *plane;
	int i, j, err;

	for (i = 0; i < mdev->n_pipelines; i++) {
		pipe = mdev->pipelines[i];

		for (j = 0; j < pipe->n_layers; j++) {
			struct komeda_layer *layer = pipe->layers[j];
			struct komeda_atu *atu = get_atu_by_layer(pipe, layer);
			err = komeda_plane_add(kms, layer, atu);
			if (err)
				return err;
		}
	}
	/* Add ATU*_VP1 planes */
	for (i = 0; i < mdev->n_pipelines; i++) {
		pipe = mdev->pipelines[i];

		for (j = 0; j < pipe->n_atus; j++) {
			if (pipe->atu[j]->n_vp < 2)
				continue;
			err = komeda_plane_add(kms, NULL, pipe->atu[j]);
			if (err)
				return err;
		}
	}

	drm_for_each_plane(plane, kms->base) {
		kplane = to_kplane(plane);
		if (!kplane->atu)
			continue;
		kplane->atu_vp_buddy = get_atu_vp_buddy(kms, kplane);
	}

	return 0;
}
