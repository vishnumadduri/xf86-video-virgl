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


static void
virgl_download_box (virgl_surface_t *surface, int x1, int y1, int x2, int y2)
{
    virgl_kms_transfer_get_block(surface, x1, y1, x2, y2);
}

Bool
virgl_surface_prepare_access (virgl_surface_t  *surface,
			    PixmapPtr       pixmap,
			    RegionPtr       region,
			    uxa_access_t    access)
{
    int n_boxes;
    BoxPtr boxes;
    ScreenPtr pScreen = pixmap->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    RegionRec new;

    if (!pScrn->vtSema)
        return FALSE;

    if (!surface->bo) {
	goto out;
    }

    REGION_INIT (NULL, &new, (BoxPtr)NULL, 0);
    REGION_SUBTRACT (NULL, &new, region, &surface->access_region);

    if (access == UXA_ACCESS_RW)
	surface->access_type = UXA_ACCESS_RW;
    
    region = &new;
    
    n_boxes = REGION_NUM_RECTS (region);
    boxes = REGION_RECTS (region);

    if (n_boxes < 25)
    {
	while (n_boxes--)
	{
	    virgl_download_box (surface, boxes->x1, boxes->y1, boxes->x2, boxes->y2);
	    
	    boxes++;
	}
    }
    else
    {
	virgl_download_box (
	    surface,
	    new.extents.x1, new.extents.y1, new.extents.x2, new.extents.y2);
    }
    
    REGION_UNION (pScreen,
		  &(surface->access_region),
		  &(surface->access_region),
		      region);
    
    REGION_UNINIT (NULL, &new);

 out:    
    pScreen->ModifyPixmapHeader(
	pixmap,
	pixmap->drawable.width,
	pixmap->drawable.height,
	-1, -1, -1,
	pixman_image_get_data (surface->host_image));

    pixmap->devKind = pixman_image_get_stride (surface->host_image);
    
    return TRUE;
}


static void
virgl_upload_box (virgl_surface_t *surface, int x1, int y1, int x2, int y2)
{
    virgl_kms_transfer_block(surface, x1, y1, x2, y2); 
}

void
virgl_surface_finish_access (virgl_surface_t *surface, PixmapPtr pixmap)
{
    ScreenPtr pScreen = pixmap->drawable.pScreen;
    int w = pixmap->drawable.width;
    int h = pixmap->drawable.height;
    int n_boxes;
    BoxPtr boxes;

    if (!surface->bo) {
	pScreen->ModifyPixmapHeader(pixmap, w, h, -1, -1, 0, NULL);
	return;
    }

    n_boxes = REGION_NUM_RECTS (&surface->access_region);
    boxes = REGION_RECTS (&surface->access_region);

    if (surface->access_type == UXA_ACCESS_RW)
    {
	if (n_boxes < 25)
	{
	    while (n_boxes--)
	    {
		virgl_upload_box (surface, boxes->x1, boxes->y1, boxes->x2, boxes->y2);
		
		boxes++;
	    }
	}
	else
	{
	    virgl_upload_box (surface,
			surface->access_region.extents.x1,
			surface->access_region.extents.y1,
			surface->access_region.extents.x2,
			surface->access_region.extents.y2);
	}
    }

    REGION_EMPTY (pScreen, &surface->access_region);
    surface->access_type = UXA_ACCESS_RO;
    
    pScreen->ModifyPixmapHeader(pixmap, w, h, -1, -1, 0, NULL);
}


#ifdef DEBUG_REGIONS
static void
print_region (const char *header, RegionPtr pRegion)
{
    int nbox = REGION_NUM_RECTS (pRegion);
    BoxPtr pbox = REGION_RECTS (pRegion);
    
    ErrorF ("%s", header);

    if (nbox == 0)
	ErrorF (" (empty)\n");
    else
	ErrorF ("\n");
    
    while (nbox--)
    {
	ErrorF ("   %d %d %d %d (size: %d %d)\n",
		pbox->x1, pbox->y1, pbox->x2, pbox->y2,
		pbox->x2 - pbox->x1, pbox->y2 - pbox->y1);
	
	pbox++;
    }
}
#endif // DEBUG_REGIONS

/* solid */
Bool
virgl_surface_prepare_solid (virgl_surface_t *destination,
			   Pixel	  fg)
{
    return FALSE;
}

void
virgl_surface_solid (virgl_surface_t *destination,
		   int	          x1,
		   int	          y1,
		   int	          x2,
		   int	          y2)
{

}

/* copy */
Bool
virgl_surface_prepare_copy (virgl_surface_t *dest,
			    virgl_surface_t *source)
{
    if (dest->bo && source->bo && dest != source) {
	dest->u.copy_src = source;
	return TRUE;
    }

    return FALSE;
}

void
virgl_surface_copy (virgl_surface_t *dest,
		    int  src_x1, int src_y1,
		    int  dest_x1, int dest_y1,
		    int width, int height)
{
    virgl_screen_t *virgl = dest->virgl;
    struct drm_virtgpu_3d_box sbox, dbox;

    sbox.x = src_x1;
    sbox.y = src_y1;
    sbox.z = 0;
    sbox.w = width;
    sbox.h = height;
    sbox.d = 1;

    dbox.x = dest_x1;
    dbox.y = dest_y1;
    dbox.z = 0;
    dbox.w = width;
    dbox.h = height;
    dbox.d = 1;
    graw_encode_blit(virgl->gr_enc,
		     virgl_kms_bo_get_res_handle(dest->bo),
		     virgl_kms_bo_get_res_handle(dest->u.copy_src->bo),
		     &dbox,
		     &sbox);
}

void
virgl_surface_done_copy (virgl_surface_t *dest)
{
    virgl_flush(dest->virgl);
}

/* composite */
Bool
virgl_surface_prepare_composite (int op,
			       PicturePtr	src_picture,
			       PicturePtr	mask_picture,
			       PicturePtr	dest_picture,
			       virgl_surface_t *	src,
			       virgl_surface_t *	mask,
			       virgl_surface_t *	dest)
{
    return FALSE;
}


void
virgl_surface_composite (virgl_surface_t *dest,
		       int src_x, int src_y,
		       int mask_x, int mask_y,
		       int dest_x, int dest_y,
		       int width, int height)
{

}

Bool
virgl_surface_put_image (virgl_surface_t *dest,
		       int x, int y, int width, int height,
		       const char *src, int src_pitch)
{
    return FALSE;
}

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
