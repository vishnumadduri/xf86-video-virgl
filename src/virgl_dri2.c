#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xorg-server.h"
#include "virgl.h"
#ifdef DRI2
#include "dri2.h"
#endif

static inline int log2i(int i)
{
	int r = 0;

	if (i & 0xffff0000) {
		i >>= 16;
		r += 16;
	}
	if (i & 0x0000ff00) {
		i >>= 8;
		r += 8;
	}
	if (i & 0x000000f0) {
		i >>= 4;
		r += 4;
	}
	if (i & 0x0000000c) {
		i >>= 2;
		r += 2;
	}
	if (i & 0x00000002) {
		r += 1;
	}
	return r;
}

static inline int round_down_pow2(int x)
{
	return 1 << log2i(x);
}

static inline int round_up_pow2(int x)
{
   int r = round_down_pow2(x);
   if (r < x)
      r <<= 1;
   return r;
}


struct virgl_dri2_buffer {
    DRI2BufferRec base;
    PixmapPtr ppix;
};

static inline struct virgl_dri2_buffer *
virgl_dri2_buffer(DRI2BufferPtr buf)
{
	return (struct virgl_dri2_buffer *)buf;
}

static PixmapPtr get_drawable_pixmap(DrawablePtr drawable)
{
	if (drawable->type == DRAWABLE_PIXMAP)
		return (PixmapPtr)drawable;
	else
		return (*drawable->pScreen->GetWindowPixmap)((WindowPtr)drawable);
}

static DRI2BufferPtr
virgl_dri2_create_buffer2(ScreenPtr screen, DrawablePtr draw, unsigned int attachment,
			unsigned int format)
{
    ScrnInfoPtr    scrn = xf86ScreenToScrn (screen);
    virgl_screen_t * virgl = scrn->driverPrivate;
    struct virgl_dri2_buffer *qbuf;
    PixmapPtr ppix = NULL;

    qbuf = calloc(1, sizeof(*qbuf));
    if (!qbuf)
	return NULL;

    if (attachment == DRI2BufferFrontLeft) {
	struct virgl_surface_t *surf;
	ppix = get_drawable_pixmap(draw);
	if (ppix)
	    ppix->refcnt++;

	/* okay flip */

	/* get name */
	surf = get_surface(ppix);
	if (ppix->usage_hint == CREATE_PIXMAP_USAGE_BACKING_PIXMAP) {
	    if (!surf->bo) {
		virgl_kms_3d_resource_migrate(surf);
		virgl_kms_transfer_block(surf, 0, 0, surf->pixmap->drawable.width, surf->pixmap->drawable.height);
		ErrorF("migrated pixmap %p %p\n", ppix, surf);
	    }
	}
    } else {
	int bpp;
	unsigned int usage_hint = 0;

	bpp = round_up_pow2(format ? format : draw->depth);

	usage_hint = VIRGL_CREATE_PIXMAP_DRI2;
	ppix = screen->CreatePixmap(screen, draw->width,
				    draw->height, bpp,
				    usage_hint);

    }

    qbuf->base.attachment = attachment;
    qbuf->base.driverPrivate = qbuf;
    qbuf->base.format = format;
    qbuf->base.flags = 0;
    qbuf->ppix = ppix;

    if (ppix) {
        int ret;
	struct virgl_surface_t *surf;
	int name;
	/* get name */
	surf = get_surface(ppix);
	if (!surf)
	    goto fail;

	qbuf->base.name = virgl_kms_bo_get_handle(surf->bo);
    }

    ErrorF("create buffer2 called %d\n", attachment);
    return &qbuf->base;
fail:
    screen->DestroyPixmap(qbuf->ppix);
    free(qbuf);
    return NULL;
}

static void
virgl_dri2_destroy_buffer2(ScreenPtr screen, DrawablePtr draw, DRI2BufferPtr buf)
{
    struct virgl_dri2_buffer *qbuf;

    qbuf = virgl_dri2_buffer(buf);
    if (!qbuf)
	return;

    if (qbuf->ppix)
	screen->DestroyPixmap(qbuf->ppix);
    free(qbuf);
}

static DRI2BufferPtr
virgl_dri2_create_buffer(DrawablePtr pDraw, unsigned int attachment,
			   unsigned int format)
{
	return virgl_dri2_create_buffer2(pDraw->pScreen, pDraw,
				       attachment, format);
}

static void
virgl_dri2_destroy_buffer(DrawablePtr pDraw, DRI2BufferPtr buf)
{
	virgl_dri2_destroy_buffer2(pDraw->pScreen, pDraw, buf);
}

static void
virgl_dri2_copy_region2(ScreenPtr pScreen, DrawablePtr pDraw, RegionPtr pRegion,
			 DRI2BufferPtr pDstBuffer, DRI2BufferPtr pSrcBuffer)
{
  struct virgl_dri2_buffer *src = virgl_dri2_buffer(pSrcBuffer);
  struct virgl_dri2_buffer *dst = virgl_dri2_buffer(pDstBuffer);
  RegionPtr pCopyClip;
  GCPtr pGC;
  DrawablePtr src_draw, dst_draw;

  src_draw = &src->ppix->drawable;
  dst_draw = &dst->ppix->drawable;

  if (src->base.attachment == DRI2BufferFrontLeft)
    src_draw = pDraw;
  if (dst->base.attachment == DRI2BufferFrontLeft)
    dst_draw = pDraw;

  pGC = GetScratchGC(pDraw->depth, pScreen);
  pCopyClip = REGION_CREATE(pScreen, NULL, 0);
  REGION_COPY(pScreen, pCopyClip, pRegion);
  pGC->funcs->ChangeClip(pGC, CT_REGION, pCopyClip, 0);
  ValidateGC(dst_draw, pGC);

  pGC->ops->CopyArea(src_draw, dst_draw, pGC, 0, 0,
		     pDraw->width, pDraw->height, 0, 0);
  
  FreeScratchGC(pGC);

}

static void
virgl_dri2_copy_region(DrawablePtr pDraw, RegionPtr pRegion,
			 DRI2BufferPtr pDstBuffer, DRI2BufferPtr pSrcBuffer)
{
    return virgl_dri2_copy_region2(pDraw->pScreen, pDraw, pRegion,
				 pDstBuffer, pSrcBuffer);
}

Bool
virgl_dri2_init(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    virgl_screen_t *virgl = pScrn->driverPrivate;
    DRI2InfoRec dri2 = { 0 };
    const char *drivername[1] = { "virgl" };

    dri2.driverNames = drivername;
    dri2.numDrivers = 1;
    dri2.driverName = dri2.driverNames[0];

    dri2.fd = virgl->drm_fd;
    dri2.deviceName = virgl->drm_device_name;

    dri2.version = DRI2INFOREC_VERSION;
    dri2.CreateBuffer = virgl_dri2_create_buffer;
    dri2.DestroyBuffer = virgl_dri2_destroy_buffer;
    dri2.CopyRegion = virgl_dri2_copy_region;

#if DRI2INFOREC_VERSION >= 7
    dri2.version = 7;
    dri2.GetParam = NULL;
#endif

#if DRI2INFOREC_VERSION >= 9
    dri2.version = 9;
    dri2.CreateBuffer2 = virgl_dri2_create_buffer2;
    dri2.DestroyBuffer2 = virgl_dri2_destroy_buffer2;
    dri2.CopyRegion2 = virgl_dri2_copy_region2;
#endif
    return DRI2ScreenInit(pScreen, &dri2);
}

void
virgl_dri2_fini(ScreenPtr pScreen)
{
    DRI2CloseScreen(pScreen);
}
