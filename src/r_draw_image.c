/*
Copyright (C) 2017-2018 ezQuake team

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include "quakedef.h"
#include "common_draw.h"
#include "glm_draw.h"
#include "tr_types.h"
#include "glsl/constants.glsl"
#include "r_matrix.h"
#include "glm_vao.h"
#include "r_state.h"
#include "glc_vao.h"
#include "r_buffers.h"
#include "r_renderer.h"
#include "r_texture_internal.h"

extern cvar_t r_smoothtext, r_smoothcrosshair, r_smoothimages;
float cachedMatrix[16];

glm_image_framedata_t imageData;

int Draw_ImagePosition(void)
{
	return imageData.imageCount;
}

void Draw_AdjustImages(int first, int last, float x_offset)
{
	float v1[4] = { x_offset, 0, 0, 1 };
	float v2[4] = { 0, 0, 0, 1 };
	float dx, dy;

	R_MultiplyVector(cachedMatrix, v1, v1);
	R_MultiplyVector(cachedMatrix, v2, v2);

	// Android's pre-rotation compensation matrix can swap x and y (see
	// VK_SetCoordinates), so a horizontal offset in authoring space doesn't
	// necessarily land on pos[0] post-transform -- derive both components
	// instead of assuming it's still purely a pos[0] delta.
	dx = v1[0] - v2[0];
	dy = v1[1] - v2[1];

	renderer.AdjustImages(first, last, dx, dy);
}

void R_DrawImage(float x, float y, float width, float height, float tex_s, float tex_t, float tex_width, float tex_height, byte* color, qbool alpha_test, texture_ref texnum, qbool isText, qbool isCrosshair)
{
	int flags = IMAGEPROG_FLAGS_TEXTURE;

	if (imageData.imageCount >= MAX_MULTI_IMAGE_BATCH) {
		return;
	}
	if (!R_LogCustomImageTypeWithTexture(imagetype_image, imageData.imageCount, texnum)) {
		return;
	}

	flags |= (alpha_test ? IMAGEPROG_FLAGS_ALPHATEST : 0);
	if (isCrosshair) {
		if (!r_smoothcrosshair.integer) {
			flags |= IMAGEPROG_FLAGS_NEAREST;
		}
	}
	else if (isText) {
		flags |= IMAGEPROG_FLAGS_TEXT;
		if (!r_smoothtext.integer) {
			flags |= IMAGEPROG_FLAGS_NEAREST;
		}
	}
	else {
		if (!r_smoothimages.integer) {
			flags |= IMAGEPROG_FLAGS_NEAREST;
		}
	}

	renderer.DrawImage(x, y, width, height, tex_s, tex_t, tex_width, tex_height, color, flags);
}

void R_DrawRectangle(float x, float y, float width, float height, byte* color)
{
	renderer.DrawRectangle(x, y, width, height, color);
}

void R_Cache2DMatrix(void)
{
	R_MultiplyMatrix(R_ProjectionMatrix(), R_ModelviewMatrix(), cachedMatrix);
}

void R_UndoLastCharacter(void)
{
	if (imageData.imageCount) {
		--imageData.imageCount;
		R_HudUndoLastElement();
	}
}
