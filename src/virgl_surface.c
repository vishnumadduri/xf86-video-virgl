/*
 * Copyright 2010 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "virgl.h"

void
virgl_get_formats (int bpp, pixman_format_code_t *pformat, uint32_t *virgl_format)
{
    switch (bpp)
    {
    case 8:
	*pformat = PIXMAN_a8;
	*virgl_format = VIRGL_FORMAT_A8_UNORM;
	break;

    case 16:
	*pformat = PIXMAN_r5g6b5;
	*virgl_format = VIRGL_FORMAT_B5G6R5_UNORM;
	break;

    case 24:
	*pformat = PIXMAN_x8r8g8b8;
	*virgl_format = VIRGL_FORMAT_B8G8R8X8_UNORM;
	break;
	
    case 32:
	*pformat = PIXMAN_a8r8g8b8;
	*virgl_format = VIRGL_FORMAT_B8G8R8A8_UNORM;
	break;

    default:
	*pformat = -1;
	*virgl_format = 0;
	break;
    }
}

virgl_surface_t *
virgl_create_primary (virgl_screen_t *virgl, int bpp)
{
    ScrnInfoPtr pScrn = virgl->pScrn;
    pixman_format_code_t pformat;
    uint8_t *dev_addr;
    pixman_image_t *host_image;
    virgl_surface_t *surface;
    struct virgl_bo *bo = NULL;
    uint32_t format;
    int cpp = (bpp + 7) >> 3;
    virgl_get_formats(bpp, &pformat, &format);
    if (pformat == -1) {
      ErrorF("unknown pixel format\n");
      return NULL;
    }

    bo = virgl_bo_create_primary_resource(virgl, pScrn->virtualX, pScrn->virtualY, pScrn->virtualX * cpp, format, 1);
    if (!bo) {
      ErrorF("unable to allocate primary bo\n");
      return NULL;
    }

    dev_addr = virgl->bo_funcs->bo_map(bo);
    host_image = pixman_image_create_bits (pformat, 
					   pScrn->virtualX, pScrn->virtualY,
					   (uint32_t *)dev_addr, pScrn->virtualX * cpp);
    surface = malloc (sizeof *surface);
    surface->host_image = host_image;
    surface->virgl = virgl;
    surface->bo = bo;
    REGION_INIT (NULL, &(surface->access_region), (BoxPtr)NULL, 0);
    surface->access_type = UXA_ACCESS_RO;
    
    return surface;
}

void
virgl_surface_set_pixmap (virgl_surface_t *surface, PixmapPtr pixmap)
{
    surface->pixmap = pixmap;

    assert (get_surface (pixmap) == surface);
}
