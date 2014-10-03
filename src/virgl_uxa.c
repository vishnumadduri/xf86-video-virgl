/*
 * Copyright 2008 Red Hat, Inc.
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

/** \file virgl_driver.c
 * \author Adam Jackson <ajax@redhat.com>
 * \author Søren Sandmann <sandmann@redhat.com>
 *
 * This is virgl, a driver for the Qumranet paravirtualized graphics device
 * in qemu.
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "virgl.h"

#if HAS_DEVPRIVATEKEYREC
DevPrivateKeyRec uxa_pixmap_index;
#else
int uxa_pixmap_index;
#endif

static Bool
virgl_prepare_access (PixmapPtr pixmap, RegionPtr region, uxa_access_t access)
{
    int n_boxes;
    BoxPtr boxes;
    ScreenPtr pScreen = pixmap->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    RegionRec new;
    virgl_surface_t *surface = get_surface(pixmap);

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
	    virgl_kms_transfer_get_block (surface, boxes->x1, boxes->y1, boxes->x2, boxes->y2);
	    
	    boxes++;
	}
    }
    else
    {
	virgl_kms_transfer_get_block(
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
virgl_finish_access (PixmapPtr pixmap)
{
    ScreenPtr pScreen = pixmap->drawable.pScreen;
    int w = pixmap->drawable.width;
    int h = pixmap->drawable.height;
    int n_boxes;
    BoxPtr boxes;
    virgl_surface_t *surface = get_surface(pixmap);

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
		virgl_kms_transfer_block(surface, boxes->x1, boxes->y1, boxes->x2, boxes->y2);
		
		boxes++;
	    }
	}
	else
	{
	    virgl_kms_transfer_block (surface,
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

static Bool
virgl_pixmap_is_offscreen (PixmapPtr pixmap)
{
    return !!get_surface (pixmap);
}

static Bool
good_alu_and_pm (DrawablePtr drawable, int alu, Pixel planemask)
{
    if (!UXA_PM_IS_SOLID (drawable, planemask))
	return FALSE;

    if (alu != GXcopy)
	return FALSE;

    return TRUE;
}

/*
 * Solid fill
 */
static Bool
virgl_check_solid (DrawablePtr drawable, int alu, Pixel planemask)
{
    if (!good_alu_and_pm (drawable, alu, planemask))
	return FALSE;

    return TRUE;
}

static Bool
virgl_prepare_solid (PixmapPtr pixmap, int alu, Pixel planemask, Pixel fg)
{
    virgl_surface_t *surface;

    if (!(surface = get_surface (pixmap)))
	return FALSE;

    return FALSE;
}

static void
virgl_solid (PixmapPtr pixmap, int x1, int y1, int x2, int y2)
{

}

static void
virgl_done_solid (PixmapPtr pixmap)
{
}

/*
 * Copy
 */
static Bool
virgl_check_copy (PixmapPtr source, PixmapPtr dest,
                int alu, Pixel planemask)
{
    if (!good_alu_and_pm ((DrawablePtr)source, alu, planemask))
	return FALSE;

    if (source->drawable.bitsPerPixel != dest->drawable.bitsPerPixel)
    {
	ErrorF ("differing bitsperpixel - this shouldn't happen\n");
	return FALSE;
    }

    return TRUE;
}

static Bool
virgl_prepare_copy (PixmapPtr source, PixmapPtr dest,
                  int xdir, int ydir, int alu,
                  Pixel planemask)
{
    virgl_surface_t *ds = get_surface(dest);
    virgl_surface_t *ss = get_surface(source);

    if (ds->bo && ss->bo && ds != ss) {
	ds->u.copy_src = ss;
	return TRUE;
    }

    return FALSE;
}

static void
virgl_copy (PixmapPtr dest,
          int src_x1, int src_y1,
          int dest_x1, int dest_y1,
          int width, int height)
{
    virgl_surface_t *ds = get_surface(dest);
    virgl_screen_t *virgl = ds->virgl;
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
		     virgl_kms_bo_get_res_handle(ds->bo),
		     virgl_kms_bo_get_res_handle(ds->u.copy_src->bo),
		     &dbox,
		     &sbox);
}

static void
virgl_done_copy (PixmapPtr dest)
{
    virgl_flush(get_surface(dest)->virgl);
}

/*
 * Composite
 */
static Bool
can_accelerate_picture (PicturePtr pict)
{
    if (!pict)
	return TRUE;

    if (pict->format != PICT_a8r8g8b8		&&
	pict->format != PICT_x8r8g8b8		&&
	pict->format != PICT_a8)
    {
	return FALSE;
    }

    if (!pict->pDrawable)
	return FALSE;

    if (pict->transform)
    {
	if (pict->transform->matrix[2][0] != 0	||
	    pict->transform->matrix[2][1] != 0	||
	    pict->transform->matrix[2][2] != pixman_int_to_fixed (1))
	{
	    return FALSE;
	}
    }

    if (pict->filter != PictFilterBilinear	&&
	pict->filter != PictFilterNearest)
    {
	return FALSE;
    }

    return TRUE;
}

static Bool
virgl_has_composite (virgl_screen_t *virgl)
{
    return FALSE;
}

static Bool
virgl_has_a8_surfaces (virgl_screen_t *virgl)
{
    return FALSE;
}

static Bool
virgl_check_composite (int op,
		     PicturePtr pSrcPicture,
		     PicturePtr pMaskPicture,
		     PicturePtr pDstPicture,
		     int width, int height)
{
    int i;
    ScreenPtr pScreen = pDstPicture->pDrawable->pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn (pScreen);
    virgl_screen_t *virgl = pScrn->driverPrivate;

    static const int accelerated_ops[] =
    {
	PictOpClear, PictOpSrc, PictOpDst, PictOpOver, PictOpOverReverse,
	PictOpIn, PictOpInReverse, PictOpOut, PictOpOutReverse,
	PictOpAtop, PictOpAtopReverse, PictOpXor, PictOpAdd,
	PictOpSaturate, PictOpMultiply, PictOpScreen, PictOpOverlay,
	PictOpDarken, PictOpLighten, PictOpColorDodge, PictOpColorBurn,
	PictOpHardLight, PictOpSoftLight, PictOpDifference, PictOpExclusion,
	PictOpHSLHue, PictOpHSLSaturation, PictOpHSLColor, PictOpHSLLuminosity,
    };

    if (!virgl_has_composite (virgl))
	return FALSE;

    if (!can_accelerate_picture (pSrcPicture)	||
	!can_accelerate_picture (pMaskPicture)	||
	!can_accelerate_picture (pDstPicture))
    {
	return FALSE;
    }

    for (i = 0; i < sizeof (accelerated_ops) / sizeof (accelerated_ops[0]); ++i)
    {
	if (accelerated_ops[i] == op)
	    goto found;
    }
    return FALSE;

found:
    return TRUE;
}

static Bool
virgl_check_composite_target (PixmapPtr pixmap)
{
    return TRUE;
}

static Bool
virgl_check_composite_texture (ScreenPtr screen,
			     PicturePtr pPicture)
{
    return TRUE;
}

static Bool
virgl_prepare_composite (int op,
		       PicturePtr pSrcPicture,
		       PicturePtr pMaskPicture,
		       PicturePtr pDstPicture,
		       PixmapPtr pSrc,
		       PixmapPtr pMask,
		       PixmapPtr pDst)
{
    return FALSE;
}

static void
virgl_composite (PixmapPtr pDst,
	       int src_x, int src_y,
	       int mask_x, int mask_y,
	       int dst_x, int dst_y,
	       int width, int height)
{
}

static void
virgl_done_composite (PixmapPtr pDst)
{
}

static Bool
virgl_put_image (PixmapPtr pDst, int x, int y, int w, int h,
               char *src, int src_pitch)
{
    virgl_surface_t *surface = get_surface (pDst);

    return FALSE;
}

static void
virgl_set_screen_pixmap (PixmapPtr pixmap)
{
    pixmap->drawable.pScreen->devPrivate = pixmap;
}

static PixmapPtr
virgl_create_pixmap (ScreenPtr screen, int w, int h, int depth, unsigned usage_hint)
{
    ScrnInfoPtr    scrn = xf86ScreenToScrn (screen);
    PixmapPtr      pixmap;
    virgl_screen_t * virgl = scrn->driverPrivate;
    virgl_surface_t *surface;

    if (w > 32767 || h > 32767)
	return NULL;

    if (uxa_swapped_out (screen))
	goto fallback;

    if (depth == 8 && !virgl_has_a8_surfaces (virgl))
    {
	/* FIXME: When we detect a _change_ in the property of having a8
	 * surfaces, we should copy all existing a8 surface to host memory
	 * and then destroy the ones on the device.
	 */
	goto fallback;
    }

    if (!w || !h)
      goto fallback;

    surface = virgl->bo_funcs->create_surface (virgl, w, h, depth, usage_hint);
    if (surface)
    {
	/* ErrorF ("   Successfully created surface in video memory\n"); */

        pixmap = fbCreatePixmap (screen, 0, 0, depth, usage_hint);

	screen->ModifyPixmapHeader (pixmap, w, h,
	                            -1, -1, -1,
	                            NULL);

#if 0
	ErrorF ("Create pixmap %p with surface %p\n", pixmap, surface);
#endif
	set_surface (pixmap, surface);
	virgl_surface_set_pixmap (surface, pixmap);

    }
    else
    {

    fallback:
	pixmap = fbCreatePixmap (screen, w, h, depth, usage_hint);

    }

    return pixmap;
}

static Bool
virgl_destroy_pixmap (PixmapPtr pixmap)
{
    ScreenPtr      screen = pixmap->drawable.pScreen;
    ScrnInfoPtr    scrn = xf86ScreenToScrn (screen);
    virgl_screen_t * virgl = scrn->driverPrivate;
    virgl_surface_t *surface = NULL;

    if (pixmap->refcnt == 1)
    {
	surface = get_surface (pixmap);

#if 0
	ErrorF ("- Destroy %p (had surface %p)\n", pixmap, surface);
#endif

	if (surface)
	{
	    virgl->bo_funcs->destroy_surface(surface);
	    set_surface (pixmap, NULL);

	}
    }

    fbDestroyPixmap (pixmap);
    return TRUE;
}

static void
set_uxa_functions(virgl_screen_t *virgl, ScreenPtr screen)
{
    /* Solid fill */
    virgl->uxa->check_solid = virgl_check_solid;
    virgl->uxa->prepare_solid = virgl_prepare_solid;
    virgl->uxa->solid = virgl_solid;
    virgl->uxa->done_solid = virgl_done_solid;

    /* Copy */
    virgl->uxa->check_copy = virgl_check_copy;
    virgl->uxa->prepare_copy = virgl_prepare_copy;
    virgl->uxa->copy = virgl_copy;
    virgl->uxa->done_copy = virgl_done_copy;

    /* Composite */
    virgl->uxa->check_composite = virgl_check_composite;
    virgl->uxa->check_composite_target = virgl_check_composite_target;
    virgl->uxa->check_composite_texture = virgl_check_composite_texture;
    virgl->uxa->prepare_composite = virgl_prepare_composite;
    virgl->uxa->composite = virgl_composite;
    virgl->uxa->done_composite = virgl_done_composite;

    /* PutImage */
    virgl->uxa->put_image = virgl_put_image;

    /* Prepare access */
    virgl->uxa->prepare_access = virgl_prepare_access;
    virgl->uxa->finish_access = virgl_finish_access;

    virgl->uxa->pixmap_is_offscreen = virgl_pixmap_is_offscreen;

    screen->SetScreenPixmap = virgl_set_screen_pixmap;
    screen->CreatePixmap = virgl_create_pixmap;
    screen->DestroyPixmap = virgl_destroy_pixmap;
}

Bool
virgl_uxa_init (virgl_screen_t *virgl, ScreenPtr screen)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn (screen);

#if HAS_DIXREGISTERPRIVATEKEY
    if (!dixRegisterPrivateKey (&uxa_pixmap_index, PRIVATE_PIXMAP, 0))
	return FALSE;
#else
    if (!dixRequestPrivate (&uxa_pixmap_index, 0))
	return FALSE;
#endif

    virgl->uxa = uxa_driver_alloc ();
    if (virgl->uxa == NULL)
	return FALSE;

    memset (virgl->uxa, 0, sizeof (*virgl->uxa));

    virgl->uxa->uxa_major = 1;
    virgl->uxa->uxa_minor = 0;

    set_uxa_functions(virgl, screen);

    if (!uxa_driver_init (screen, virgl->uxa))
    {
	xf86DrvMsg (scrn->scrnIndex, X_ERROR,
	            "UXA initialization failed\n");
	free (virgl->uxa);
	return FALSE;
    }

#if 0
    uxa_set_fallback_debug (screen, FALSE);
#endif

#if 0
    if (!uxa_driver_init (screen, virgl->uxa))
	return FALSE;
#endif

    return TRUE;
}
