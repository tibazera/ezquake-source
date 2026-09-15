
/*
Copyright (C) 2018 ezQuake team

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

#ifdef RENDERER_OPTION_VULKAN

#include "quakedef.h"
#include "vk_local.h"

void VK_PrintGfxInfo(void)
{
}

// Shared grow-and-keep helper: doubles *capacity (or sets it to `needed` if
// that's already bigger) and Q_reallocs *buffer to match, only when needed
// exceeds the current capacity -- otherwise a no-op. Used in place of a
// per-frame Q_malloc/Q_free pair for scratch buffers that are the same size
// or grow slowly frame to frame (world draw lists, descriptor-free queues,
// etc.) -- same pattern that was duplicated by hand in three places
// (vk_world.c's worldDraws/worldPassIndices, vk_texture.c's
// deferredDescriptorFrees) before this helper existed.
void VK_GrowBuffer(void** buffer, int* capacity, int needed, size_t elementSize)
{
	int newCapacity;

	if (needed <= *capacity) {
		return;
	}

	newCapacity = *capacity ? *capacity * 2 : 128;
	if (newCapacity < needed) {
		newCapacity = needed;
	}

	*buffer = Q_realloc(*buffer, (size_t)newCapacity * elementSize);
	*capacity = newCapacity;
}

#endif // #ifdef RENDERER_OPTION_VULKAN
