/*
 *  GL Widgets, GLW_IMAGE widget
 *  Copyright (C) 2007 Andreas Öman
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "glw.h"
#include "glw_texture.h"

typedef struct glw_image {
  glw_t w;

  float gi_alpha_self;

  float gi_angle;

  char *gi_pending_filename;
  glw_loadable_texture_t *gi_current;
  glw_loadable_texture_t *gi_pending;

  int16_t gi_border_left;
  int16_t gi_border_right;
  int16_t gi_border_top;
  int16_t gi_border_bottom;

  int16_t gi_padding_left;
  int16_t gi_padding_right;
  int16_t gi_padding_top;
  int16_t gi_padding_bottom;
 
  int gi_bitmap_flags;

  uint8_t gi_mode;

#define GI_MODE_NORMAL           0
#define GI_MODE_BORDER_SCALING   1
#define GI_MODE_REPEATED_TEXTURE 2
#define GI_MODE_ALPHA_EDGES      3

  uint8_t gi_render_initialized;
  uint8_t gi_update;

  uint8_t gi_frozen;

  uint8_t gi_alpha_edge;

  uint8_t gi_was_valid;

  glw_renderer_t gi_gr;

  float gi_saved_size_x;
  float gi_saved_size_y;

  float gi_child_xs;
  float gi_child_ys;
  float gi_child_xt;
  float gi_child_yt;

  glw_rgb_t gi_color;

  float gi_size_scale;
  float gi_size_bias;

} glw_image_t;

static glw_class_t glw_image, glw_icon, glw_backdrop, glw_repeatedimage;

static uint8_t texcords[9][8] = {
    { 0, 1,   1, 1,   1, 0,  0, 0},  // Normal
    { 0, 1,   1, 1,   1, 0,  0, 0},  // Normal
    { 1, 1,   0, 1,   0, 0,  1, 0},  // Mirror X
    { 1, 0,   0, 0,   0, 1,  1, 1},  // 180 deg. rotate
    { 0, 0,   1, 0,   1, 1,  0, 1},  // Mirror Y
    { 0, 0,   0, 0,   0, 0,  0, 0},  // Transpose ???
    { 1, 1,   1, 0,   0, 0,  0, 1},  // Rot 90
    { 0, 0,   0, 0,   0, 0,  0, 0},  // Transverse ???
    { 0, 0,   0, 1,   1, 1,  1, 0},  // Rot 270
};






static void
glw_image_dtor(glw_t *w)
{
  glw_image_t *gi = (void *)w;

  if(gi->gi_current != NULL)
    glw_tex_deref(w->glw_root, gi->gi_current);

  if(gi->gi_pending != NULL)
    glw_tex_deref(w->glw_root, gi->gi_pending);

  if(gi->gi_render_initialized)
    glw_render_free(&gi->gi_gr);
}

/**
 *
 */
static void 
render_child_simple(glw_t *w, glw_rctx_t *rc)
{
  glw_rctx_t rc0 = *rc;
  glw_t *c;

  if((c = TAILQ_FIRST(&w->glw_childs)) != NULL) {
    rc0.rc_alpha = rc->rc_alpha * w->glw_alpha;
    glw_render0(c, &rc0);
  }
}

/**
 *
 */
static void 
render_child_autocentered(glw_image_t *gi, glw_rctx_t *rc)
{
  glw_t *c;
  glw_rctx_t rc0;
  float xs, ys;

  if((c = TAILQ_FIRST(&gi->w.glw_childs)) == NULL)
    return;

  rc0 = *rc;
      
  glw_PushMatrix(&rc0, rc);
      
  glw_Translatef(&rc0, gi->gi_child_xt, gi->gi_child_yt, 0.0f);

  xs = gi->gi_child_xs;
  ys = gi->gi_child_ys;

  glw_Scalef(&rc0, xs, ys, 1.0f);

  rc0.rc_size_x = rc->rc_size_x * xs;
  rc0.rc_size_y = rc->rc_size_y * ys;

  rc0.rc_alpha = rc->rc_alpha * gi->w.glw_alpha;
  glw_render0(c, &rc0);

  glw_PopMatrix();
}


/**
 *
 */
static void
glw_scale_to_pixels(glw_rctx_t *rc, int w, int h)
{
  float xs = w / rc->rc_size_x;
  float ys = h / rc->rc_size_y;

  glw_Scalef(rc, xs, ys, 1.0);
  rc->rc_size_x = w;
  rc->rc_size_y = h;
}


/**
 *
 */
static void 
glw_image_render(glw_t *w, glw_rctx_t *rc)
{
  glw_image_t *gi = (void *)w;
  glw_loadable_texture_t *glt = gi->gi_current;
  float alpha_self;
  glw_rctx_t rc0;

  if(glt == NULL || glt->glt_state != GLT_STATE_VALID)
    return;

  if(!glw_is_tex_inited(&glt->glt_texture))
    alpha_self = 0;
  else
    alpha_self = rc->rc_alpha * w->glw_alpha * gi->gi_alpha_self;

  if(gi->gi_mode == GI_MODE_NORMAL || gi->gi_mode == GI_MODE_ALPHA_EDGES) {

    rc0 = *rc;

    glw_PushMatrix(&rc0, rc);

    glw_align_1(&rc0, w->glw_alignment);
      
    if(gi->gi_bitmap_flags & GLW_IMAGE_RESIZE) {
      glw_root_t *gr = w->glw_root;
      float siz = gi->gi_size_scale * gr->gr_fontsize_px + gi->gi_size_bias;
      glw_scale_to_pixels(&rc0, siz, siz);
    } else if(gi->gi_bitmap_flags & GLW_IMAGE_FIXED_SIZE)
      glw_scale_to_pixels(&rc0, glt->glt_xs, glt->glt_ys);
    else if(w->glw_class == &glw_image || w->glw_class == &glw_icon)
      glw_scale_to_aspect(&rc0, glt->glt_aspect);

    if(gi->gi_angle != 0)
      glw_Rotatef(&rc0, -gi->gi_angle, 0, 0, 1);

    glw_align_2(&rc0, w->glw_alignment);

    if(glw_is_focusable(w))
      glw_store_matrix(w, &rc0);

    if(gi->gi_bitmap_flags & GLW_IMAGE_INFRONT)
      render_child_simple(w, &rc0);
    
    if(alpha_self > 0.01) {

      if(w->glw_flags & GLW_SHADOW && !rc0.rc_inhibit_shadows) {
	float xd, yd;

	xd =  3.0 / rc0.rc_size_x;
	yd = -3.0 / rc0.rc_size_y;

	glw_Translatef(&rc0, xd, yd, 0.0);
	
	glw_render(&gi->gi_gr, w->glw_root, &rc0, 
		   GLW_RENDER_MODE_QUADS, GLW_RENDER_ATTRIBS_TEX,
		   &glt->glt_texture,
		   0,0,0, alpha_self * 0.75);
	glw_Translatef(&rc0, -xd, -yd, 0.0);
      }

      glw_render(&gi->gi_gr, w->glw_root, &rc0, 
		 GLW_RENDER_MODE_QUADS,
		 gi->gi_mode == GI_MODE_ALPHA_EDGES ? 
		 GLW_RENDER_ATTRIBS_TEX_COLOR : GLW_RENDER_ATTRIBS_TEX,
		 &glt->glt_texture,
		 gi->gi_color.r, gi->gi_color.g, gi->gi_color.b, alpha_self);

    }

    if(!(gi->gi_bitmap_flags & GLW_IMAGE_INFRONT))
      render_child_simple(w, &rc0);

    glw_PopMatrix();

  } else {

    if(glw_is_focusable(w))
      glw_store_matrix(w, rc);

    if(gi->gi_bitmap_flags & GLW_IMAGE_INFRONT)
       render_child_autocentered(gi, rc);

    if(alpha_self > 0.01)
      glw_render(&gi->gi_gr, w->glw_root, rc, 
		 GLW_RENDER_MODE_QUADS, GLW_RENDER_ATTRIBS_TEX,
		 &glt->glt_texture,
		 gi->gi_color.r, gi->gi_color.g, gi->gi_color.b, alpha_self);

    if(!(gi->gi_bitmap_flags & GLW_IMAGE_INFRONT))
      render_child_autocentered(gi, rc);

  }
}

/**
 *
 */
static void
glw_image_layout_tesselated(glw_root_t *gr, glw_rctx_t *rc, glw_image_t *gi, 
			    glw_loadable_texture_t *glt)
{
  float tex[4][2];
  float vex[4][2];
  float cvex[2][2];

  int x, y, i = 0;
  int bf = gi->gi_bitmap_flags;

  if(gr->gr_normalized_texture_coords) {
    tex[0][0] = 0.0;
    tex[1][0] = 0.0 + (float)gi->gi_border_left  / glt->glt_xs;
    tex[2][0] = 1.0 - (float)gi->gi_border_right / glt->glt_xs;
    tex[3][0] = 1.0;

    tex[0][1] = 0.0;
    tex[1][1] = 0.0 + (float)gi->gi_border_top    / glt->glt_ys;
    tex[2][1] = 1.0 - (float)gi->gi_border_bottom / glt->glt_ys;
    tex[3][1] = 1.0;
  } else {
    tex[0][0] = 0.0;
    tex[1][0] = gi->gi_border_left;
    tex[2][0] = glt->glt_xs - gi->gi_border_right;
    tex[3][0] = glt->glt_xs;

    tex[0][1] = 0.0;
    tex[1][1] = gi->gi_border_top;
    tex[2][1] = glt->glt_ys - gi->gi_border_bottom;
    tex[3][1] = glt->glt_ys;
  }


  float bl = bf & GLW_IMAGE_BORDER_LEFT   ? gi->gi_border_left   : 0;
  float bt = bf & GLW_IMAGE_BORDER_TOP    ? gi->gi_border_top    : 0;
  float br = bf & GLW_IMAGE_BORDER_RIGHT  ? gi->gi_border_right  : 0;
  float bb = bf & GLW_IMAGE_BORDER_BOTTOM ? gi->gi_border_bottom : 0;

  float pl = gi->gi_padding_left;
  float pt = gi->gi_padding_top;
  float pr = gi->gi_padding_right;
  float pb = gi->gi_padding_bottom;


  vex[0][0] = -1.0;
  vex[1][0] = GLW_MIN(-1.0 + 2.0 * bl / rc->rc_size_x, 0.0);
  vex[2][0] = GLW_MAX( 1.0 - 2.0 * br / rc->rc_size_x, 0.0);
  vex[3][0] = 1.0;
    
  vex[0][1] = 1.0;
  vex[1][1] = GLW_MAX( 1.0 - 2.0 * bt / rc->rc_size_y, 0.0);
  vex[2][1] = GLW_MIN(-1.0 + 2.0 * bb / rc->rc_size_y, 0.0);
  vex[3][1] = -1.0;


  bl = gi->gi_border_left;
  bt = gi->gi_border_top;
  br = gi->gi_border_right;
  bb = gi->gi_border_bottom;

  cvex[0][0] = GLW_MIN(-1.0 + 2.0 * (bl + pl) / rc->rc_size_x, 0.0);
  cvex[1][0] = GLW_MAX( 1.0 - 2.0 * (br + pr) / rc->rc_size_x, 0.0);
  cvex[0][1] = GLW_MAX( 1.0 - 2.0 * (bt + pt) / rc->rc_size_y, 0.0);
  cvex[1][1] = GLW_MIN(-1.0 + 2.0 * (bb + pb) / rc->rc_size_y, 0.0);

  gi->gi_child_xt = (cvex[1][0] + cvex[0][0]) * 0.5f;
  gi->gi_child_yt = (cvex[0][1] + cvex[1][1]) * 0.5f;
  gi->gi_child_xs = (cvex[1][0] - cvex[0][0]) * 0.5f;
  gi->gi_child_ys = (cvex[0][1] - cvex[1][1]) * 0.5f;

  if(gi->gi_bitmap_flags & GLW_IMAGE_MIRROR_X) {
    GLW_SWAP(vex[0][1], vex[3][1]);
    GLW_SWAP(vex[1][1], vex[2][1]);
    gi->gi_child_xt = -gi->gi_child_xt;
  }

  if(gi->gi_bitmap_flags & GLW_IMAGE_MIRROR_Y) {
    GLW_SWAP(vex[0][0], vex[3][0]);
    GLW_SWAP(vex[1][0], vex[2][0]);
    gi->gi_child_yt = -gi->gi_child_yt;
  }


  glw_render_set_pre(&gi->gi_gr);

  for(y = 0; y < 3; y++) {
    
    if(y == 0 && !(gi->gi_bitmap_flags & GLW_IMAGE_BORDER_TOP))
      continue;

    if(y == 2 && !(gi->gi_bitmap_flags & GLW_IMAGE_BORDER_BOTTOM))
      continue;

    for(x = 0; x < 3; x++) {

      if(x == 0 && !(gi->gi_bitmap_flags & GLW_IMAGE_BORDER_LEFT))
	continue;
      
      if(x == 2 && !(gi->gi_bitmap_flags & GLW_IMAGE_BORDER_RIGHT))
	continue;

      glw_render_vtx_pos(&gi->gi_gr, i, vex[x + 0][0], vex[y + 1][1], 0.0f);
      glw_render_vtx_st (&gi->gi_gr, i, tex[x + 0][0], tex[y + 1][1]);
      i++;

      glw_render_vtx_pos(&gi->gi_gr, i, vex[x + 1][0], vex[y + 1][1], 0.0f);
      glw_render_vtx_st (&gi->gi_gr, i, tex[x + 1][0], tex[y + 1][1]);
      i++;

      glw_render_vtx_pos(&gi->gi_gr, i, vex[x + 1][0], vex[y + 0][1], 0.0f);
      glw_render_vtx_st (&gi->gi_gr, i, tex[x + 1][0], tex[y + 0][1]);
      i++;

      glw_render_vtx_pos(&gi->gi_gr, i, vex[x + 0][0], vex[y + 0][1], 0.0f);
      glw_render_vtx_st (&gi->gi_gr, i, tex[x + 0][0], tex[y + 0][1]);
      i++;
    }
  }

  glw_render_set_vertices(&gi->gi_gr, i);
  glw_render_set_post(&gi->gi_gr);
}

static const float alphaborder[4][4] = {
  {0,0,0,0},
  {0,1,1,0},
  {0,1,1,0},
  {0,0,0,0},
};

/**
 *
 */
static void
glw_image_layout_alpha_edges(glw_root_t *gr, glw_rctx_t *rc, glw_image_t *gi, 
			       glw_loadable_texture_t *glt)
{
  float tex[4][2];
  float vex[4][2];
  float cvex[2][2];

  int x, y, i = 0;

  if(gr->gr_normalized_texture_coords) {
    tex[0][0] = 0.0;
    tex[1][0] = 0.0 + (float)gi->gi_alpha_edge / glt->glt_xs;
    tex[2][0] = 1.0 - (float)gi->gi_alpha_edge / glt->glt_xs;
    tex[3][0] = 1.0;

    tex[0][1] = 0.0;
    tex[1][1] = 0.0 + (float)gi->gi_alpha_edge / glt->glt_ys;
    tex[2][1] = 1.0 - (float)gi->gi_alpha_edge / glt->glt_ys;
    tex[3][1] = 1.0;
  } else {
    tex[0][0] = 0.0;
    tex[1][0] = gi->gi_alpha_edge;
    tex[2][0] = glt->glt_xs - gi->gi_alpha_edge;
    tex[3][0] = glt->glt_xs;

    tex[0][1] = 0.0;
    tex[1][1] = gi->gi_alpha_edge;
    tex[2][1] = glt->glt_ys - gi->gi_alpha_edge;
    tex[3][1] = glt->glt_ys;
  }


  float bl = gi->gi_alpha_edge;
  float bt = gi->gi_alpha_edge;
  float br = gi->gi_alpha_edge;
  float bb = gi->gi_alpha_edge;


  vex[0][0] = -1.0;
  vex[1][0] = GLW_MIN(-1.0 + 2.0 * bl / rc->rc_size_x, 0.0);
  vex[2][0] = GLW_MAX( 1.0 - 2.0 * br / rc->rc_size_x, 0.0);
  vex[3][0] = 1.0;
    
  vex[0][1] = 1.0;
  vex[1][1] = GLW_MAX( 1.0 - 2.0 * bt / rc->rc_size_y, 0.0);
  vex[2][1] = GLW_MIN(-1.0 + 2.0 * bb / rc->rc_size_y, 0.0);
  vex[3][1] = -1.0;


  cvex[0][0] = GLW_MIN(-1.0 + 2.0 * bl / rc->rc_size_x, 0.0);
  cvex[1][0] = GLW_MAX( 1.0 - 2.0 * br / rc->rc_size_x, 0.0);
  cvex[0][1] = GLW_MAX( 1.0 - 2.0 * bt / rc->rc_size_y, 0.0);
  cvex[1][1] = GLW_MIN(-1.0 + 2.0 * bb / rc->rc_size_y, 0.0);

  gi->gi_child_xt = (cvex[1][0] + cvex[0][0]) * 0.5f;
  gi->gi_child_yt = (cvex[0][1] + cvex[1][1]) * 0.5f;
  gi->gi_child_xs = (cvex[1][0] - cvex[0][0]) * 0.5f;
  gi->gi_child_ys = (cvex[0][1] - cvex[1][1]) * 0.5f;

  if(gi->gi_bitmap_flags & GLW_IMAGE_MIRROR_X) {
    GLW_SWAP(vex[0][1], vex[3][1]);
    GLW_SWAP(vex[1][1], vex[2][1]);
    gi->gi_child_xt = -gi->gi_child_xt;
  }

  if(gi->gi_bitmap_flags & GLW_IMAGE_MIRROR_Y) {
    GLW_SWAP(vex[0][0], vex[3][0]);
    GLW_SWAP(vex[1][0], vex[2][0]);
    gi->gi_child_yt = -gi->gi_child_yt;
  }


  glw_render_set_pre(&gi->gi_gr);

  for(y = 0; y < 3; y++) {

    for(x = 0; x < 3; x++) {
      int p1,p2,p3,p4;


      if((x == 0 && y == 0) || (x == 2 && y == 2)) {
	p1 = i+3;
	p2 = i+0;
	p3 = i+1;
	p4 = i+2;

      } else {
	p1 = i;
	p2 = i+1;
	p3 = i+2;
	p4 = i+3;
      }

      glw_render_vtx_pos(&gi->gi_gr, p1, vex[x + 0][0], vex[y + 1][1], 0.0f);
      glw_render_vtx_st (&gi->gi_gr, p1, tex[x + 0][0], tex[y + 1][1]);
      glw_render_vtx_col(&gi->gi_gr, p1, 1, 1, 1, alphaborder[x+0][y+1]);


      glw_render_vtx_pos(&gi->gi_gr, p2, vex[x + 1][0], vex[y + 1][1], 0.0f);
      glw_render_vtx_st (&gi->gi_gr, p2, tex[x + 1][0], tex[y + 1][1]);
      glw_render_vtx_col(&gi->gi_gr, p2, 1, 1, 1, alphaborder[x+1][y+1]);

      glw_render_vtx_pos(&gi->gi_gr, p3, vex[x + 1][0], vex[y + 0][1], 0.0f);
      glw_render_vtx_st (&gi->gi_gr, p3, tex[x + 1][0], tex[y + 0][1]);
      glw_render_vtx_col(&gi->gi_gr, p3, 1, 1, 1, alphaborder[x+1][y+0]);

      glw_render_vtx_pos(&gi->gi_gr, p4, vex[x + 0][0], vex[y + 0][1], 0.0f);
      glw_render_vtx_st (&gi->gi_gr, p4, tex[x + 0][0], tex[y + 0][1]);
      glw_render_vtx_col(&gi->gi_gr, p4, 1, 1, 1, alphaborder[x+0][y+0]);

      i+=4;
    }
  }

  glw_render_set_vertices(&gi->gi_gr, i);
  glw_render_set_post(&gi->gi_gr);
}


/**
 *
 */
static void
glw_image_layout_normal(glw_root_t *gr, glw_rctx_t *rc, glw_image_t *gi, 
			glw_loadable_texture_t *glt)
{
  float xs = gr->gr_normalized_texture_coords ? 1.0 : glt->glt_xs;
  float ys = gr->gr_normalized_texture_coords ? 1.0 : glt->glt_ys;

  uint8_t tex[8];
  int o = glt->glt_orientation < 9 ? glt->glt_orientation : 0;
  memcpy(tex, texcords[o], 8);

  if(gi->gi_bitmap_flags & GLW_IMAGE_MIRROR_X) {
    GLW_SWAP(tex[0], tex[2]);
    GLW_SWAP(tex[4], tex[6]);
  }
	
  if(gi->gi_bitmap_flags & GLW_IMAGE_MIRROR_Y) {
    GLW_SWAP(tex[1], tex[7]);
    GLW_SWAP(tex[5], tex[3]);
  }

  glw_render_vtx_pos(&gi->gi_gr, 0, -1.0, -1.0, 0.0);
  glw_render_vtx_st (&gi->gi_gr, 0, 
		     tex[0] * xs , tex[1] * ys);

  glw_render_vtx_pos(&gi->gi_gr, 1,  1.0, -1.0, 0.0);
  glw_render_vtx_st (&gi->gi_gr, 1,
		     tex[2] * xs , tex[3] * ys);

  glw_render_vtx_pos(&gi->gi_gr, 2,  1.0,  1.0, 0.0);
  glw_render_vtx_st (&gi->gi_gr, 2,
		     tex[4] * xs , tex[5] * ys);

  glw_render_vtx_pos(&gi->gi_gr, 3, -1.0,  1.0, 0.0);
  glw_render_vtx_st (&gi->gi_gr, 3,
		     tex[6] * xs , tex[7] * ys);

  gi->gi_child_xs = 1;
  gi->gi_child_ys = 1;
}


/**
 *
 */
static void
glw_image_layout_repeated(glw_root_t *gr, glw_rctx_t *rc, glw_image_t *gi, 
			  glw_loadable_texture_t *glt)
{
  float xs = gr->gr_normalized_texture_coords ? 1.0 : glt->glt_xs;
  float ys = gr->gr_normalized_texture_coords ? 1.0 : glt->glt_ys;

  if(!(gi->gi_bitmap_flags & GLW_IMAGE_STRETCH_X))
    xs *= rc->rc_size_x / glt->glt_xs;

  if(!(gi->gi_bitmap_flags & GLW_IMAGE_STRETCH_Y))
    ys *= rc->rc_size_y / glt->glt_ys;

  glw_render_vtx_pos(&gi->gi_gr, 0, -1.0, -1.0, 0.0);
  glw_render_vtx_st (&gi->gi_gr, 0, 0, ys);

  glw_render_vtx_pos(&gi->gi_gr, 1,  1.0, -1.0, 0.0);
  glw_render_vtx_st (&gi->gi_gr, 1, xs, ys);

  glw_render_vtx_pos(&gi->gi_gr, 2,  1.0,  1.0, 0.0);
  glw_render_vtx_st (&gi->gi_gr, 2, xs, 0);

  glw_render_vtx_pos(&gi->gi_gr, 3, -1.0,  1.0, 0.0);
  glw_render_vtx_st (&gi->gi_gr, 3, 0, 0);
}


/**
 *
 */
static void
glw_image_update_constraints(glw_image_t *gi)
{
  glw_loadable_texture_t *glt = gi->gi_current;
  glw_t *c;
  glw_root_t *gr = gi->w.glw_root;

  if(gi->gi_bitmap_flags & GLW_IMAGE_FIXED_SIZE) {

    glw_set_constraints(&gi->w, 
			glt->glt_xs,
			glt->glt_ys,
			0, 0, 
			GLW_CONSTRAINT_X | GLW_CONSTRAINT_Y, 0);

  } else if(gi->w.glw_class == &glw_backdrop) {

    c = TAILQ_FIRST(&gi->w.glw_childs);

    if(c != NULL) {
      glw_set_constraints(&gi->w, 
			  c->glw_req_size_x +
			  gi->gi_border_left + gi->gi_border_right +
			  gi->gi_padding_left + gi->gi_padding_right,
			  c->glw_req_size_y + 
			  gi->gi_border_top + gi->gi_border_bottom + 
			  gi->gi_padding_top + gi->gi_padding_bottom,
			  0, 0, 
			  glw_filter_constraints(c->glw_flags),
			  0);
    } else if(glt != NULL) {
      glw_set_constraints(&gi->w, 
			  glt->glt_xs,
			  glt->glt_ys,
			  0, 0, 0, 0);
    }

  } else if(gi->w.glw_class == &glw_icon) {

    float siz = gi->gi_size_scale * gr->gr_fontsize_px + gi->gi_size_bias;

    glw_set_constraints(&gi->w, siz, siz, 0, 0,
			GLW_CONSTRAINT_X | GLW_CONSTRAINT_Y, 0);

  } else {
    
    glw_set_constraints(&gi->w, 0, 0,
			glt && glt->glt_state == GLT_STATE_VALID ? 
			glt->glt_aspect : 1, 0,
			GLW_CONSTRAINT_A, 0);
  }
}


/**
 *
 */
static void 
glw_image_layout(glw_t *w, glw_rctx_t *rc)
{
  glw_image_t *gi = (void *)w;
  glw_root_t *gr = w->glw_root;
  glw_loadable_texture_t *glt;
  glw_rctx_t rc0;
  glw_t *c;

  if(gi->gi_pending_filename != NULL) {
    // Request to load
    int xs = -1, ys = -1;
    int flags = 0;
    
    if(gi->gi_pending != NULL)
      glw_tex_deref(w->glw_root, gi->gi_pending);
    
    
    if(w->glw_class == &glw_repeatedimage)
      flags |= GLW_TEX_REPEAT;


    if(gi->gi_bitmap_flags & GLW_IMAGE_HQ_SCALING) {

      if(rc->rc_size_x < rc->rc_size_y) {
	xs = rc->rc_size_x;
      } else {
	ys = rc->rc_size_y;
      }
    }

    if(gi->gi_pending_filename != NULL && xs && ys) {

      gi->gi_pending = glw_tex_create(w->glw_root, gi->gi_pending_filename,
				      flags, xs, ys);

      free(gi->gi_pending_filename);
      gi->gi_pending_filename = NULL;
    } else {
      gi->gi_pending = NULL;
    }
  }


  if((glt = gi->gi_pending) != NULL) {
    glw_tex_layout(gr, glt);

    if(glt->glt_state == GLT_STATE_VALID || 
       glt->glt_state == GLT_STATE_ERROR) {
      // Pending texture completed, ok or error: transfer to current

      if(gi->gi_current != NULL)
	glw_tex_deref(w->glw_root, gi->gi_current);

      gi->gi_current = gi->gi_pending;
      gi->gi_pending = NULL;
      gi->gi_update = 1;
    }
  }

  if((glt = gi->gi_current) == NULL)
    return;

  glw_tex_layout(gr, glt);

  if(glt->glt_state == GLT_STATE_ERROR) {
    if(!gi->gi_was_valid) {
      glw_signal0(w, GLW_SIGNAL_READY, NULL);
      gi->gi_was_valid = 1;
    }
  } else if(glt->glt_state == GLT_STATE_VALID) {

    if(!gi->gi_was_valid) {
      glw_signal0(w, GLW_SIGNAL_READY, NULL);
      gi->gi_was_valid = 1;
    }

    if(gi->gi_update && !gi->gi_frozen) {
      gi->gi_update = 0;

      if(gi->gi_render_initialized)
	glw_render_free(&gi->gi_gr);

      gi->gi_render_initialized = 1;

      switch(gi->gi_mode) {
	
      case GI_MODE_NORMAL:
	glw_render_init(&gi->gi_gr, 4, GLW_RENDER_ATTRIBS_TEX);
	glw_image_layout_normal(gr, rc, gi, glt);
	break;
      case GI_MODE_BORDER_SCALING:
	glw_render_init(&gi->gi_gr, 4 * 9, GLW_RENDER_ATTRIBS_TEX);
	glw_image_layout_tesselated(gr, rc, gi, glt);
	break;
      case GI_MODE_REPEATED_TEXTURE:
	glw_render_init(&gi->gi_gr, 4, GLW_RENDER_ATTRIBS_TEX);
	glw_image_layout_repeated(gr, rc, gi, glt);
	break;
      case GI_MODE_ALPHA_EDGES:
	glw_render_init(&gi->gi_gr, 4 * 9, GLW_RENDER_ATTRIBS_TEX_COLOR);
	glw_image_layout_alpha_edges(gr, rc, gi, glt);
	break;
      default:
	abort();
      }


    } else if(gi->gi_saved_size_x != rc->rc_size_x ||
	      gi->gi_saved_size_y != rc->rc_size_y) {

      gi->gi_saved_size_x = rc->rc_size_x;
      gi->gi_saved_size_y = rc->rc_size_y;

      switch(gi->gi_mode) {
	
      case GI_MODE_NORMAL:
	break;
      case GI_MODE_BORDER_SCALING:
	glw_image_layout_tesselated(gr, rc, gi, glt);
	break;
      case GI_MODE_REPEATED_TEXTURE:
	glw_image_layout_repeated(gr, rc, gi, glt);
	break;
      case GI_MODE_ALPHA_EDGES:
	glw_image_layout_alpha_edges(gr, rc, gi, glt);
	break;
      }

      if(gi->gi_bitmap_flags & GLW_IMAGE_HQ_SCALING && gi->gi_pending == NULL &&
	 gi->gi_pending_filename == NULL) {

	int xs = -1, ys = -1, rescale;
	
	if(rc->rc_size_x < rc->rc_size_y) {
	  rescale = abs(rc->rc_size_x - glt->glt_xs) > glt->glt_xs / 10;
	  xs = rc->rc_size_x;
	} else {
	  rescale = abs(rc->rc_size_y - glt->glt_ys) > glt->glt_ys / 10;
	  ys = rc->rc_size_y;
	}
	
	if(rescale) {
	  int flags = 0;
	  if(w->glw_class == &glw_repeatedimage)
	    flags |= GLW_TEX_REPEAT;

	  gi->gi_pending = glw_tex_create(w->glw_root, glt->glt_filename,
					  flags, xs, ys);
	}
      }
    }
  }

  glw_image_update_constraints(gi);

  if((c = TAILQ_FIRST(&w->glw_childs)) != NULL) {
    rc0 = *rc;
    rc0.rc_size_x = rc->rc_size_x * gi->gi_child_xs;
    rc0.rc_size_y = rc->rc_size_y * gi->gi_child_ys;
    glw_layout0(c, &rc0);
  }
}


/*
 *
 */
static int
glw_image_callback(glw_t *w, void *opaque, glw_signal_t signal,
		    void *extra)
{
  glw_t *c;
  switch(signal) {
  default:
    break;
  case GLW_SIGNAL_LAYOUT:
    glw_image_layout(w, extra);
    break;
  case GLW_SIGNAL_EVENT:
    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
      if(glw_signal0(c, GLW_SIGNAL_EVENT, extra))
	return 1;
    break;

  case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
  case GLW_SIGNAL_CHILD_CREATED:
    glw_image_update_constraints((glw_image_t *)w);
    return 1;
  case GLW_SIGNAL_CHILD_DESTROYED:
    glw_set_constraints(w, 0, 0, 0, 0, 0, 0);
    return 1;

  }
  return 0;
}

/*
 *
 */
static void 
glw_image_set(glw_t *w, int init, va_list ap)
{
  glw_image_t *gi = (void *)w;
  glw_attribute_t attrib;
  const char *filename = NULL;
  glw_root_t *gr = w->glw_root;

  if(init) {
    gi->gi_alpha_self = 1;
    gi->gi_color.r = 1.0;
    gi->gi_color.g = 1.0;
    gi->gi_color.b = 1.0;
    gi->gi_size_scale = 1.0;

    gi->gi_bitmap_flags =
      GLW_IMAGE_BORDER_LEFT |
      GLW_IMAGE_BORDER_RIGHT |
      GLW_IMAGE_BORDER_TOP |
      GLW_IMAGE_BORDER_BOTTOM;

    if(w->glw_class == &glw_image)
      glw_set_constraints(&gi->w, 0, 0, 1, 0, GLW_CONSTRAINT_A, 0); 

    if(w->glw_class == &glw_repeatedimage)
      gi->gi_mode = GI_MODE_REPEATED_TEXTURE;
  }

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {
    case GLW_ATTRIB_FREEZE:
      gi->gi_frozen = va_arg(ap, int);
      break;

    case GLW_ATTRIB_BORDER:
      gi->gi_mode = GI_MODE_BORDER_SCALING;
      gi->gi_border_left   = va_arg(ap, double);
      gi->gi_border_top    = va_arg(ap, double);
      gi->gi_border_right  = va_arg(ap, double);
      gi->gi_border_bottom = va_arg(ap, double);
      gi->gi_update = 1;
      glw_image_update_constraints(gi);
      break;

    case GLW_ATTRIB_PADDING:
      gi->gi_padding_left   = va_arg(ap, double);
      gi->gi_padding_top    = va_arg(ap, double);
      gi->gi_padding_right  = va_arg(ap, double);
      gi->gi_padding_bottom = va_arg(ap, double);
      gi->gi_update = 1;
      glw_image_update_constraints(gi);
      break;
      

    case GLW_ATTRIB_SET_FLAGS:
      (void)va_arg(ap, int);
      glw_image_update_constraints((glw_image_t *)w);
      break;

    case GLW_ATTRIB_ANGLE:
      gi->gi_angle = va_arg(ap, double);
      break;

    case GLW_ATTRIB_ALPHA_SELF:
      gi->gi_alpha_self = va_arg(ap, double);
      break;
      
    case GLW_ATTRIB_SOURCE:
      filename = va_arg(ap, char *);

      char *curname;

      if(gi->gi_pending_filename != NULL)
	curname = gi->gi_pending_filename;
      else if(gi->gi_pending != NULL) 
	curname = gi->gi_pending->glt_filename;
      else if(gi->gi_current != NULL) 
	curname = gi->gi_current->glt_filename;
      else 
	curname = NULL;

      if(curname != NULL && filename != NULL && !strcmp(filename, curname))
	break;

      if(gi->gi_pending_filename != NULL)
	free(gi->gi_pending_filename);

      gi->gi_pending_filename = filename ? strdup(filename) : NULL;
      break;

    case GLW_ATTRIB_PIXMAP:
      if(gi->gi_pending != NULL)
	glw_tex_deref(w->glw_root, gi->gi_pending);

      free(gi->gi_pending_filename);
      gi->gi_pending_filename = NULL;

      gi->gi_pending = glw_tex_create_from_pixmap(w->glw_root, 
						  va_arg(ap, pixmap_t *));
      break;

    case GLW_ATTRIB_SET_IMAGE_FLAGS:
      gi->gi_bitmap_flags |= va_arg(ap, int);
      gi->gi_update = 1;
      break;

    case GLW_ATTRIB_CLR_IMAGE_FLAGS:
      gi->gi_bitmap_flags &= ~va_arg(ap, int);
      gi->gi_update = 1;
      break;

    case GLW_ATTRIB_RGB:
      gi->gi_color.r = va_arg(ap, double);
      gi->gi_color.g = va_arg(ap, double);
      gi->gi_color.b = va_arg(ap, double);
      break;

    case GLW_ATTRIB_SIZE_SCALE:
      gi->gi_size_scale = va_arg(ap, double);
      break;

    case GLW_ATTRIB_SIZE_BIAS:
      gi->gi_size_bias = va_arg(ap, double);
      break;

    case GLW_ATTRIB_ALPHA_EDGES:
      gi->gi_alpha_edge = va_arg(ap, int);
      gi->gi_mode = GI_MODE_ALPHA_EDGES;
      break;

    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);

  if(w->glw_class == &glw_icon) {
    float siz = gi->gi_size_scale * gr->gr_fontsize_px + gi->gi_size_bias;
    glw_set_constraints(&gi->w, siz, siz, 0, 0,
			GLW_CONSTRAINT_X | GLW_CONSTRAINT_Y, 0);
  }
}


/**
 *
 */
static int
glw_image_ready(glw_t *w)
{
  glw_image_t *gi = (glw_image_t *)w;
  glw_loadable_texture_t *glt = gi->gi_current;
 
  return glt != NULL && (glt->glt_state == GLT_STATE_VALID || 
			 glt->glt_state == GLT_STATE_ERROR);
}


/**
 *
 */
static glw_class_t glw_image = {
  .gc_name = "image",
  .gc_instance_size = sizeof(glw_image_t),
  .gc_render = glw_image_render,
  .gc_dtor = glw_image_dtor,
  .gc_set = glw_image_set,
  .gc_signal_handler = glw_image_callback,
  .gc_default_alignment = GLW_ALIGN_CENTER,
  .gc_ready = glw_image_ready,
};

GLW_REGISTER_CLASS(glw_image);


/**
 *
 */
static glw_class_t glw_icon = {
  .gc_name = "icon",
  .gc_instance_size = sizeof(glw_image_t),
  .gc_render = glw_image_render,
  .gc_dtor = glw_image_dtor,
  .gc_set = glw_image_set,
  .gc_signal_handler = glw_image_callback,
  .gc_default_alignment = GLW_ALIGN_CENTER,
};

GLW_REGISTER_CLASS(glw_icon);


/**
 *
 */
static glw_class_t glw_backdrop = {
  .gc_name = "backdrop",
  .gc_instance_size = sizeof(glw_image_t),
  .gc_render = glw_image_render,
  .gc_dtor = glw_image_dtor,
  .gc_set = glw_image_set,
  .gc_signal_handler = glw_image_callback,
  .gc_default_alignment = GLW_ALIGN_CENTER,
};

GLW_REGISTER_CLASS(glw_backdrop);



/**
 *
 */
static glw_class_t glw_repeatedimage = {
  .gc_name = "repeatedimage",
  .gc_instance_size = sizeof(glw_image_t),
  .gc_render = glw_image_render,
  .gc_dtor = glw_image_dtor,
  .gc_set = glw_image_set,
  .gc_signal_handler = glw_image_callback,
  .gc_default_alignment = GLW_ALIGN_CENTER,
};

GLW_REGISTER_CLASS(glw_repeatedimage);
