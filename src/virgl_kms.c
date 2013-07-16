#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef XF86DRM_MODE
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include "virgl.h"


static void graw_flush_eq(struct graw_encoder_state *eq, void *closure);

static Bool virgl_open_drm_master(ScrnInfoPtr pScrn)
{
    virgl_screen_t *virgl = pScrn->driverPrivate;
    struct pci_device *dev = virgl->pci;
    char *busid;
    drmSetVersion sv;
    int err;

#if XORG_VERSION_CURRENT >= XORG_VERSION_NUMERIC(1,9,99,901,0)
    XNFasprintf(&busid, "pci:%04x:%02x:%02x.%d",
                dev->domain, dev->bus, dev->dev, dev->func);
#else
    busid = XNFprintf("pci:%04x:%02x:%02x.%d",
		      dev->domain, dev->bus, dev->dev, dev->func);
#endif

    virgl->drm_fd = drmOpen("virgl", busid);
    if (virgl->drm_fd == -1) {

	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "[drm] Failed to open DRM device for %s: %s\n",
		   busid, strerror(errno));
	free(busid);
	return FALSE;
    }
    free(busid);

    /* Check that what we opened was a master or a master-capable FD,
     * by setting the version of the interface we'll use to talk to it.
     * (see DRIOpenDRMMaster() in DRI1)
     */
    sv.drm_di_major = 1;
    sv.drm_di_minor = 1;
    sv.drm_dd_major = -1;
    sv.drm_dd_minor = -1;
    err = drmSetInterfaceVersion(virgl->drm_fd, &sv);
    if (err != 0) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "[drm] failed to set drm interface version.\n");
	drmClose(virgl->drm_fd);
	virgl->drm_fd = -1;

	return FALSE;
    }

 out:
    virgl->drmmode.fd = virgl->drm_fd;
    virgl->drm_device_name = drmGetDeviceNameFromFd(virgl->drm_fd);
    return TRUE;
}

static int dispatch_dirty_region(ScrnInfoPtr scrn,
				 PixmapPtr pixmap,
				 DamagePtr damage,
				 int fb_id)
{
    virgl_screen_t *virgl = scrn->driverPrivate;
    RegionPtr dirty = DamageRegion(damage);
    unsigned num_cliprects = REGION_NUM_RECTS(dirty);
    if (num_cliprects) {
        drmModeClip *clip = malloc(num_cliprects * sizeof(drmModeClip));
        BoxPtr rect = REGION_RECTS(dirty);
        int i, ret;
            
        if (!clip)
            return -ENOMEM;

        /* XXX no need for copy? */
        for (i = 0; i < num_cliprects; i++, rect++) {
            clip[i].x1 = rect->x1;
            clip[i].y1 = rect->y1;
            clip[i].x2 = rect->x2;
            clip[i].y2 = rect->y2;
        }

        /* TODO query connector property to see if this is needed */
        ret = drmModeDirtyFB(virgl->drm_fd, fb_id, clip, num_cliprects);
        free(clip);
        DamageEmpty(damage);
        if (ret) {
            if (ret == -EINVAL)
                return ret;
        }
    }
    return 0;

}

static void dispatch_dirty(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn (pScreen);
    virgl_screen_t *virgl = pScrn->driverPrivate;
    PixmapPtr pixmap = pScreen->GetScreenPixmap(pScreen);
    int fb_id = virgl->drmmode.fb_id;
    int ret;

    ret = dispatch_dirty_region(pScrn, pixmap, virgl->damage, fb_id);

}
static void virglBlockHandler(BLOCKHANDLER_ARGS_DECL)
{
    SCREEN_PTR(arg);
    virgl_screen_t *virgl = xf86ScreenToScrn(pScreen)->driverPrivate;

    pScreen->BlockHandler = virgl->BlockHandler;
    pScreen->BlockHandler(BLOCKHANDLER_ARGS);
    pScreen->BlockHandler = virglBlockHandler;

    graw_flush_eq(virgl->gr_enc, NULL);

    dispatch_dirty(pScreen);
}

void virgl_flush(virgl_screen_t *virgl)
{
    graw_flush_eq(virgl->gr_enc, NULL);
}

static Bool
virgl_close_screen_kms (CLOSE_SCREEN_ARGS_DECL)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn (pScreen);
    virgl_screen_t *virgl = pScrn->driverPrivate;
    Bool result;

    virgl_dri2_fini(pScreen);

    pScreen->CloseScreen = virgl->close_screen;

    result = pScreen->CloseScreen (CLOSE_SCREEN_ARGS);

    return result;
}

static Bool
virgl_color_setup (ScrnInfoPtr pScrn)
{
    int   scrnIndex = pScrn->scrnIndex;
    Gamma gzeros = { 0.0, 0.0, 0.0 };
    rgb   rzeros = { 0, 0, 0 };
    
    if (!xf86SetDepthBpp (pScrn, 0, 0, 0, Support32bppFb))
	return FALSE;
    
    if (pScrn->depth != 15 && pScrn->depth != 24)
    {
	xf86DrvMsg (scrnIndex, X_ERROR, "Depth %d is not supported\n",
	            pScrn->depth);
	return FALSE;
    }
    xf86PrintDepthBpp (pScrn);
    
    if (!xf86SetWeight (pScrn, rzeros, rzeros))
	return FALSE;
    
    if (!xf86SetDefaultVisual (pScrn, -1))
	return FALSE;
    
    if (!xf86SetGamma (pScrn, gzeros))
	return FALSE;
    
    return TRUE;
}


Bool virgl_pre_init_kms(ScrnInfoPtr pScrn, int flags)
{
    int           scrnIndex = pScrn->scrnIndex;
    virgl_screen_t *virgl = NULL;

    if (!pScrn->confScreen)
	return FALSE;

    /* zaphod mode is for suckers and i choose not to implement it */
    if (xf86IsEntityShared (pScrn->entityList[0]))
    {
	xf86DrvMsg (scrnIndex, X_ERROR, "No Zaphod mode for you\n");
	return FALSE;
    }
    
    if (!pScrn->driverPrivate)
	pScrn->driverPrivate = xnfcalloc (sizeof (virgl_screen_t), 1);

    virgl = pScrn->driverPrivate;
    virgl->pScrn = pScrn;
    virgl->entity = xf86GetEntityInfo (pScrn->entityList[0]);
    virgl->kms_enabled = TRUE;
    xorg_list_init(&virgl->ums_bos);

    virgl_kms_setup_funcs(virgl);
    virgl->pci = xf86GetPciInfoForEntity (virgl->entity->index);

    pScrn->monitor = pScrn->confScreen->monitor;

    if (virgl_open_drm_master(pScrn) == FALSE) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Kernel modesetting setup failed\n");
	goto out;
    }

    if (!virgl_color_setup(pScrn))
	goto out;

    xf86SetDpi (pScrn, 0, 0);

    if (!xf86LoadSubModule (pScrn, "fb"))
	goto out;

    if (!xf86LoadSubModule (pScrn, "ramdac"))
	goto out;

    if (drmmode_pre_init(pScrn, &virgl->drmmode, pScrn->bitsPerPixel / 8) == FALSE)
      goto out;

    virgl->virtual_x = 1024;
    virgl->virtual_y = 768;
    
    pScrn->display->virtualX = virgl->virtual_x;
    pScrn->display->virtualY = virgl->virtual_y;

    xf86DrvMsg (scrnIndex, X_INFO, "PreInit complete\n");
#ifdef GIT_VERSION
    xf86DrvMsg (scrnIndex, X_INFO, "git commit %s\n", GIT_VERSION);
#endif

    return TRUE;

 out:
    if (virgl)
      free(virgl);
    return FALSE;
}

static Bool
virgl_create_screen_resources_kms(ScreenPtr pScreen)
{
    ScrnInfoPtr    pScrn = xf86ScreenToScrn (pScreen);
    virgl_screen_t * virgl = pScrn->driverPrivate;
    Bool           ret;
    PixmapPtr      pPixmap;
    virgl_surface_t *surf;
    int            i;
    
    pScreen->CreateScreenResources = virgl->create_screen_resources;
    ret = pScreen->CreateScreenResources (pScreen);
    pScreen->CreateScreenResources = virgl_create_screen_resources_kms;
    
    if (!ret)
	return FALSE;
    
    pPixmap = pScreen->GetScreenPixmap (pScreen);
    
    virgl_set_screen_pixmap_header (pScreen);
    
    if ((surf = get_surface (pPixmap)))
        virgl->bo_funcs->destroy_surface(surf);
    
    set_surface (pPixmap, virgl->primary);

    virgl->gr_enc = graw_encoder_init_queue(virgl->drm_fd);
    virgl->damage = DamageCreate(NULL, NULL, DamageReportNone, TRUE,
			       pScreen, pPixmap);
    if (virgl->damage) {
	DamageRegister(&pPixmap->drawable, virgl->damage);
    }

    if (!uxa_resources_init (pScreen))
	return FALSE;
    
    return TRUE;
}

static Bool
virgl_blank_screen (ScreenPtr pScreen, int mode)
{
    return TRUE;
}

Bool
virgl_enter_vt_kms (VT_FUNC_ARGS_DECL)
{
    SCRN_INFO_PTR (arg);
    virgl_screen_t *virgl = pScrn->driverPrivate;
    int ret;

    ret = drmSetMaster(virgl->drm_fd);
    if (ret) {
	xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		   "drmSetMaster failed: %s\n",
		   strerror(errno));
    }

    if (!xf86SetDesiredModes(pScrn))
	return FALSE;

    //    pScrn->EnableDisableFBAccess (XF86_SCRN_ARG (pScrn), TRUE);
    return TRUE;
}

void
virgl_leave_vt_kms (VT_FUNC_ARGS_DECL)
{
    SCRN_INFO_PTR (arg); 
    int ret;
    virgl_screen_t *virgl = pScrn->driverPrivate;
    xf86_hide_cursors (pScrn);
    //    pScrn->EnableDisableFBAccess (XF86_SCRN_ARG (pScrn), FALSE);

    ret = drmDropMaster(virgl->drm_fd);
    if (ret) {
	xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		   "drmDropMaster failed: %s\n",
		   strerror(errno));
    }
}

void
virgl_set_screen_pixmap_header (ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn (pScreen);
    virgl_screen_t *virgl = pScrn->driverPrivate;
    PixmapPtr pPixmap = pScreen->GetScreenPixmap (pScreen);
    
    // TODO: don't ModifyPixmapHeader too early?
    
    if (pPixmap)
    {
	pScreen->ModifyPixmapHeader (pPixmap,
	                             pScrn->virtualX, pScrn->virtualY,
	                             -1, -1,
	                             pScrn->virtualX * 4,
	                             virgl_surface_get_host_bits(virgl->primary));
    }
    else
    {
	ErrorF ("pix: %p;\n", pPixmap);
    }
}

void *
virgl_surface_get_host_bits(virgl_surface_t *surface)
{
    if (!surface)
	return NULL;
    return (void *) pixman_image_get_data(surface->host_image);
}


Bool
virgl_fb_init (virgl_screen_t *virgl, ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = virgl->pScrn;
   
    if (!fbScreenInit (pScreen, virgl_surface_get_host_bits(virgl->primary),
                       pScrn->virtualX, pScrn->virtualY,
                       pScrn->xDpi, pScrn->yDpi, pScrn->virtualX,
                       pScrn->bitsPerPixel))
	return FALSE;
    
    fbPictureInit (pScreen, NULL, 0);
    return TRUE;
}

Bool virgl_screen_init_kms(SCREEN_INIT_ARGS_DECL)
{
    ScrnInfoPtr    pScrn = xf86ScreenToScrn (pScreen);
    virgl_screen_t * virgl = pScrn->driverPrivate;
    VisualPtr      visual;
    uint64_t n_surf;

    miClearVisualTypes ();
    if (!miSetVisualTypes (pScrn->depth, miGetDefaultVisualMask (pScrn->depth),
                           pScrn->rgbBits, pScrn->defaultVisual))
	goto out;
    if (!miSetPixmapDepths ())
	goto out;
    pScrn->displayWidth = pScrn->virtualX;

    pScrn->virtualX = pScrn->currentMode->HDisplay;
    pScrn->virtualY = pScrn->currentMode->VDisplay;
    if (!virgl_fb_init (virgl, pScreen))
	goto out;
    
    visual = pScreen->visuals + pScreen->numVisuals;
    while (--visual >= pScreen->visuals)
    {
	if ((visual->class | DynamicClass) == DirectColor)
	{
	    visual->offsetRed = pScrn->offset.red;
	    visual->offsetGreen = pScrn->offset.green;
	    visual->offsetBlue = pScrn->offset.blue;
	    visual->redMask = pScrn->mask.red;
	    visual->greenMask = pScrn->mask.green;
	    visual->blueMask = pScrn->mask.blue;
	}
    }
    
    virgl->uxa = uxa_driver_alloc ();

// GETPARAM
    /* no surface cache for kms surfaces for now */
    pScreen->SaveScreen = virgl_blank_screen;

    virgl_dri2_init(pScreen);

    virgl_uxa_init (virgl, pScreen);

    DamageSetup (pScreen);

    miDCInitialize (pScreen, xf86GetPointerScreenFuncs());

#if 1
    xf86_cursors_init (pScreen, 64, 64,
		       (HARDWARE_CURSOR_TRUECOLOR_AT_8BPP |
			HARDWARE_CURSOR_AND_SOURCE_WITH_MASK |
			HARDWARE_CURSOR_SOURCE_MASK_INTERLEAVE_1 |
			HARDWARE_CURSOR_UPDATE_UNHIDDEN |
			HARDWARE_CURSOR_ARGB));
#endif
    if (!miCreateDefColormap (pScreen))
        goto out;

    if (!xf86CrtcScreenInit (pScreen))
	return FALSE;

    virgl->primary = virgl_create_primary(virgl, 32);
    /* create primary resource */
    //    if (!virgl_resize_primary_to_virtual (virgl))
    //	return FALSE;

    virgl->create_screen_resources = pScreen->CreateScreenResources;
    pScreen->CreateScreenResources = virgl_create_screen_resources_kms;

    virgl->close_screen = pScreen->CloseScreen;
    pScreen->CloseScreen = virgl_close_screen_kms;

    virgl->BlockHandler = pScreen->BlockHandler;
    pScreen->BlockHandler = virglBlockHandler;
    return virgl_enter_vt_kms(VT_FUNC_ARGS);
 out:
    return FALSE;

}

struct virgl_kms_bo {
    uint32_t handle;
    uint32_t size;
    struct xorg_list bos;
    void *mapping;
    virgl_screen_t *virgl;
    int refcnt;
    uint32_t kname;
    uint32_t res_handle;
};

struct virgl_bo *virgl_bo_alloc(virgl_screen_t *virgl,
				uint32_t target, uint32_t format, uint32_t bind,
				uint32_t width, uint32_t height)
{
    struct drm_virgl_3d_resource_create create;
    struct virgl_kms_bo *bo;
    int ret;
    uint32_t size;
    size = width * height * 4; // TODO
    bo = calloc(1, sizeof(struct virgl_kms_bo));
    if (!bo)
	return NULL;

    memset(&create, 0, sizeof(create));
    create.target = target;
    create.format = format;
    create.bind = bind;
    create.width = width;
    create.height = height;
    create.depth = 1;
    create.size = size;

    ret = drmIoctl(virgl->drm_fd, DRM_IOCTL_VIRGL_RESOURCE_CREATE, &create);
    if (ret) {
        xf86DrvMsg(virgl->pScrn->scrnIndex, X_ERROR,
                   "error doing VIRGL resource create\n");
	free(bo);
        return NULL; // an invalid handle
    }

 out:
    bo->size = size;
    bo->handle = create.bo_handle;
    bo->res_handle = create.res_handle;
    bo->virgl = virgl;
    bo->refcnt = 1;
    return (struct virgl_bo *)bo;
}

static void *virgl_bo_map(struct virgl_bo *_bo)
{
    struct virgl_kms_bo *bo = (struct virgl_kms_bo *)_bo;
    void *map;
    struct drm_virgl_map virgl_map;
    virgl_screen_t *virgl;

    if (!bo)
	return NULL;

    virgl = bo->virgl;
    if (bo->mapping)
	return bo->mapping;

    memset(&virgl_map, 0, sizeof(virgl_map));

    virgl_map.handle = bo->handle;
    
    if (drmIoctl(virgl->drm_fd, DRM_IOCTL_VIRGL_MAP, &virgl_map)) {
	xf86DrvMsg(virgl->pScrn->scrnIndex, X_ERROR,
                   "error doing VIRGL_MAP: %s\n", strerror(errno));
        return NULL;
    }

    map = mmap(0, bo->size, PROT_READ | PROT_WRITE, MAP_SHARED, virgl->drm_fd,
               virgl_map.offset);
    if (map == MAP_FAILED) {
        xf86DrvMsg(virgl->pScrn->scrnIndex, X_ERROR,
                   "mmap failure: %s\n", strerror(errno));
        return NULL;
    }

    bo->mapping = map;
    return bo->mapping;
}

static void virgl_bo_unmap(struct virgl_bo *_bo)
{
}

static void virgl_bo_decref(virgl_screen_t *virgl, struct virgl_bo *_bo)
{
    struct virgl_kms_bo *bo = (struct virgl_kms_bo *)_bo;
    struct drm_gem_close args;
    int ret;

    bo->refcnt--;
    if (bo->refcnt > 0)
	return;

    munmap(bo->mapping, bo->size);
	
    /* just close the handle */
    args.handle = bo->handle;
    ret = drmIoctl(virgl->drm_fd, DRM_IOCTL_GEM_CLOSE, &args);
    if (ret) {
        xf86DrvMsg(virgl->pScrn->scrnIndex, X_ERROR,
                   "error doing VIRGL_DECREF %d %d %d\n", ret, errno, bo->handle);
    }
 out:
    free(bo);
}

struct virgl_bo *virgl_bo_create_primary_resource(virgl_screen_t *virgl, uint32_t width, uint32_t height, int32_t stride, uint32_t format)
{
    /* create a resource */
    struct virgl_bo *bo;

    bo = virgl_bo_alloc(virgl, 2, 1, (1 << 1), width, height);
    return bo;
}

struct virgl_bo *virgl_bo_create_argb_cursor_resource(virgl_screen_t *virgl,
						      uint32_t width, uint32_t height) 
{
    struct virgl_bo *bo;
    struct drm_virgl_3d_resource_create res;
    int ret;

    bo = virgl_bo_alloc(virgl, 2, 1, (1 << 16), width, height);
    if (!bo)
	return NULL;
    return bo;
}

static virgl_surface_t *
virgl_kms_surface_create(virgl_screen_t *virgl,
		       int width,
		       int height,
		       int bpp, uint32_t usage_hint)
{
    virgl_surface_t *surface;
    int stride;
    struct virgl_kms_bo *bo = NULL;
    pixman_format_code_t pformat;
    int format;
    void *dev_ptr;
    int ret;
    uint32_t *dev_addr;
    int handle;


    if ((bpp & 3) != 0)
    {
	ErrorF ("%s: Bad bpp: %d (%d)\n", __FUNCTION__, bpp, bpp & 7);
	return NULL;
    }

    if (bpp != 8 && bpp != 16 && bpp != 32 && bpp != 24)
    {
	ErrorF ("%s: Unknown bpp\n", __FUNCTION__);
	return NULL;
    }

    if (width == 0 || height == 0)
    {
	ErrorF ("%s: Zero width or height\n", __FUNCTION__);
	return NULL;
    }

    virgl_get_formats (bpp, &pformat);
    stride = width * 32 / 8;
    stride = (stride + 3) & ~3;

    /* then fill out the driver surface */
    surface = calloc(1, sizeof *surface);
    surface->virgl = virgl;

    if (usage_hint & VIRGL_CREATE_PIXMAP_DRI2) {
        surface->bo = virgl_bo_create_primary_resource(virgl, width, height, stride,
						  format);

    } else {
	surface->bo = NULL;
    }

    surface->host_image = pixman_image_create_bits (
	pformat, width, height, NULL, stride);
    REGION_INIT (NULL, &(surface->access_region), (BoxPtr)NULL, 0);
    surface->access_type = UXA_ACCESS_RO;

    return surface;
}

static void virgl_kms_surface_destroy(virgl_surface_t *surf)
{
    virgl_screen_t *virgl = surf->virgl;

    if (surf->host_image)
	pixman_image_unref (surf->host_image);

    free(surf);
}

struct virgl_bo_funcs virgl_kms_bo_funcs = {
    virgl_bo_map,
    virgl_bo_unmap,
    virgl_bo_decref,
    virgl_kms_surface_create,
    virgl_kms_surface_destroy,
};

void virgl_kms_setup_funcs(virgl_screen_t *virgl)
{
    virgl->bo_funcs = &virgl_kms_bo_funcs;
}

uint32_t virgl_kms_bo_get_handle(struct virgl_bo *_bo)
{
    struct virgl_kms_bo *bo = (struct virgl_kms_bo *)_bo;
    
    return bo->handle;
}

uint32_t virgl_kms_bo_get_res_handle(struct virgl_bo *_bo)
{
    struct virgl_kms_bo *bo = (struct virgl_kms_bo *)_bo;
    
    return bo->res_handle;
}


int virgl_kms_get_kernel_name(struct virgl_bo *_bo, uint32_t *name)
{
    struct virgl_kms_bo *bo = (struct virgl_kms_bo *)_bo;
    struct drm_gem_flink flink;
    int r;

    if (bo->kname) {
        *name = bo->kname;
        return 0;
    }
    flink.handle = bo->handle;
    r = drmIoctl(bo->virgl->drm_fd, DRM_IOCTL_GEM_FLINK, &flink);
    if (r) {
        return r;
    }
    bo->kname = flink.name;
    *name = flink.name;
    return 0;
}

int virgl_kms_3d_resource_migrate(struct virgl_surface_t *surf)
{
    surf->bo = virgl_bo_alloc(surf->virgl, 2, 1, (1 << 1), surf->pixmap->drawable.width, surf->pixmap->drawable.height);
    return 0;
}

static int virgl_3d_transfer_put(int fd, struct virgl_bo *_bo,
				 struct drm_virgl_3d_box *transfer_box,
				 uint32_t src_stride,
				 uint32_t level)
{
  struct drm_virgl_3d_transfer_put putcmd;
  struct virgl_kms_bo *bo = _bo;
  int ret;

  putcmd.bo_handle = bo->handle;
  putcmd.dst_box = *transfer_box;
  putcmd.dst_level = level;
  putcmd.src_stride = src_stride;
  putcmd.src_offset = 0;
  ret = drmIoctl(fd, DRM_IOCTL_VIRGL_TRANSFER_PUT, &putcmd);
  return ret;
}

static int virgl_3d_transfer_get(int fd, struct virgl_bo *_bo,
				 struct drm_virgl_3d_box *box, uint32_t level)
{
  struct drm_virgl_3d_transfer_get getcmd;
  struct virgl_kms_bo *bo = _bo;
  int ret;
  
  getcmd.bo_handle = bo->handle;
  getcmd.level = level;
  getcmd.box = *box;
  getcmd.dst_offset = 0;
  ret = drmIoctl(fd, DRM_IOCTL_VIRGL_TRANSFER_GET, &getcmd);
  return ret;
}


static int virgl_3d_wait(int fd, struct virgl_bo *_bo)
{
  struct drm_virgl_3d_wait waitcmd;
  struct virgl_kms_bo *bo = _bo;
  int ret;

  waitcmd.handle = bo->handle;

  ret = drmIoctl(fd, DRM_IOCTL_VIRGL_WAIT, &waitcmd);
  return ret;
}

void virgl_kms_transfer_block(struct virgl_surface_t *surf,
			      int x1, int y1, int x2, int y2)
{
   int ret;
   int fd = surf->virgl->drm_fd;
   int size;
   void *ptr;
   int width = x2 - x1;
   int height = y2 - y1;
   struct drm_virgl_3d_box transfer_box;
   void *data;
   int stride;

   transfer_box.x = x1;
   transfer_box.y = y1;
   transfer_box.w = width;
   transfer_box.h = height;
   transfer_box.z = 0;
   transfer_box.d = 1;

   ret = virgl_3d_transfer_put(fd, surf->bo,
			       &transfer_box, 0, 0);
}


void virgl_kms_transfer_get_block(struct virgl_surface_t *surf,
			     int x1, int y1, int x2, int y2)    
{

   int ret;
   int fd = surf->virgl->drm_fd;
   int size;
   void *ptr;
   int width = x2 - x1;
   int height = y2 - y1;
   struct drm_virgl_3d_box box;
   void *data;
   int stride;

   memset(&box, 0, sizeof(box));

   box.x = x1;
   box.y = y1;
   box.w = width;
   box.h = height;
   box.z = 0;
   box.d = 1;

   ret = virgl_3d_transfer_get(fd, surf->bo, &box, 0);

   ret = virgl_3d_wait(fd, surf->bo);
}

int virgl_execbuffer(int fd, uint32_t *block, int ndw)
{
   struct drm_virgl_execbuffer eb;
   int ret;

   if (ndw == 0)
      return 0;

   eb.flags = 0;
   eb.command = (unsigned long)(void *)block;
   eb.size = ndw * 4;
   ret = drmIoctl(fd, DRM_IOCTL_VIRGL_EXECBUFFER, &eb);
   return ret;
}


enum graw_cmd {
   GRAW_NOP = 0,
   GRAW_CREATE_OBJECT = 1,
   GRAW_BIND_OBJECT,
   GRAW_DESTROY_OBJECT,
   GRAW_SET_VIEWPORT_STATE,
   GRAW_SET_FRAMEBUFFER_STATE,
   GRAW_SET_VERTEX_BUFFERS,
   GRAW_CLEAR,
   GRAW_DRAW_VBO,
   GRAW_RESOURCE_INLINE_WRITE,
   GRAW_SET_SAMPLER_VIEWS,
   GRAW_SET_INDEX_BUFFER,
   GRAW_SET_CONSTANT_BUFFER,
   GRAW_SET_STENCIL_REF,
   GRAW_SET_BLEND_COLOR,
   GRAW_SET_SCISSOR_STATE,
   GRAW_BLIT,
   GRAW_RESOURCE_COPY_REGION,
};
#define GRAW_CMD0(cmd, obj, len) ((cmd) | ((obj) << 8) | ((len) << 16))

static inline void graw_encoder_write_dword(struct graw_encoder_state *state,
                                            uint32_t dword)
{
   fprintf(stderr,"[%d] 0x%x\n", state->buf_offset, dword);
   state->buf[state->buf_offset++] = dword;
}

static void graw_encoder_write_cmd_dword(struct graw_encoder_state *enc,
                                       uint32_t dword)
{
   int len = (dword >> 16);

   if (((enc->buf_offset + len + 1) * 4) > enc->buf_total)
      enc->flush(enc, enc->closure);

   graw_encoder_write_dword(enc, dword);
}

int graw_encode_resource_copy_region(struct graw_encoder_state *enc,
                                     uint32_t dst_res_handle,
                                     unsigned dst_level,
                                     unsigned dstx, unsigned dsty, unsigned dstz,
                                     uint32_t src_res_handle,
                                     unsigned src_level,
                                     const struct drm_virgl_3d_box *src_box)
{
   graw_encoder_write_cmd_dword(enc, GRAW_CMD0(GRAW_RESOURCE_COPY_REGION, 0, 13));
   graw_encoder_write_dword(enc, dst_res_handle);
   graw_encoder_write_dword(enc, dst_level);
   graw_encoder_write_dword(enc, dstx);
   graw_encoder_write_dword(enc, dsty);
   graw_encoder_write_dword(enc, dstz);
   graw_encoder_write_dword(enc, src_res_handle);
   graw_encoder_write_dword(enc, src_level);
   graw_encoder_write_dword(enc, src_box->x);
   graw_encoder_write_dword(enc, src_box->y);
   graw_encoder_write_dword(enc, src_box->z);
   graw_encoder_write_dword(enc, src_box->w);
   graw_encoder_write_dword(enc, src_box->h);
   graw_encoder_write_dword(enc, src_box->d);
   return 0;
}

int graw_encode_blit(struct graw_encoder_state *enc,
                     uint32_t dst_handle, uint32_t src_handle,
		     struct drm_virgl_3d_box *dbox,
		     struct drm_virgl_3d_box *sbox)
{
   graw_encoder_write_cmd_dword(enc, GRAW_CMD0(GRAW_BLIT, 0, 23));
   graw_encoder_write_dword(enc, 0xf);
   graw_encoder_write_dword(enc, 0);
   graw_encoder_write_dword(enc, 0);
   graw_encoder_write_dword(enc, 0);
   graw_encoder_write_dword(enc, 0);

   graw_encoder_write_dword(enc, dst_handle);
   graw_encoder_write_dword(enc, 0); // level 
   graw_encoder_write_dword(enc, 0); //format
   graw_encoder_write_dword(enc, dbox->x);
   graw_encoder_write_dword(enc, dbox->y);
   graw_encoder_write_dword(enc, dbox->z);
   graw_encoder_write_dword(enc, dbox->w);
   graw_encoder_write_dword(enc, dbox->h);
   graw_encoder_write_dword(enc, dbox->d);

   graw_encoder_write_dword(enc, src_handle);
   graw_encoder_write_dword(enc, 0); // level
   graw_encoder_write_dword(enc, 0); //format
   graw_encoder_write_dword(enc, sbox->x);
   graw_encoder_write_dword(enc, sbox->y);
   graw_encoder_write_dword(enc, sbox->z);
   graw_encoder_write_dword(enc, sbox->w);
   graw_encoder_write_dword(enc, sbox->h);
   graw_encoder_write_dword(enc, sbox->d);
   return 0;
}

static void graw_flush_eq(struct graw_encoder_state *eq, void *closure)
{
   /* send the buffer to the remote side for decoding - for now jdi */
   virgl_execbuffer(eq->fd, eq->buf, eq->buf_offset);
   eq->buf_offset = 0;
}

#define EQ_BUF_SIZE (16*1024)

struct graw_encoder_state *graw_encoder_init_queue(int fd)
{
   struct graw_encoder_state *eq;

   eq = calloc(1, sizeof(struct graw_encoder_state));
   if (!eq)
      return NULL;

   eq->buf = malloc(EQ_BUF_SIZE);
   if (!eq->buf){
      free(eq);
      return NULL;
   }
   eq->buf_total = EQ_BUF_SIZE;
   eq->buf_offset = 0;
   eq->fd = fd;
   return eq;
}

#endif

static XF86ModuleVersionInfo virgl_module_info =
{
    VIRGL_DRIVER_NAME,
    MODULEVENDORSTRING,
    MODINFOSTRING1,
    MODINFOSTRING2,
    XORG_VERSION_CURRENT,
    0, 0, 0,
    ABI_CLASS_VIDEODRV,
    ABI_VIDEODRV_VERSION,
    MOD_CLASS_VIDEODRV,
    { 0, 0, 0, 0 }
};

enum virgl_class
{
    CHIP_VIRGL_1,
};

static const struct pci_id_match virgl_device_match[] = {
    {
	PCI_VENDOR_RED_HAT, PCI_CHIP_VIRGL_3D, PCI_MATCH_ANY, PCI_MATCH_ANY,
	0x00000000, 0x00000000, CHIP_VIRGL_1
    },
    
    { 0 },
};
static SymTabRec virglChips[] = {
    { PCI_CHIP_VIRGL_3D, "QXL 3D", },
    { -1, NULL }
};

#ifndef XSERVER_LIBPCIACCESS
static PciChipsets virglPciChips[] = {
    { PCI_CHIP_VIRGL_3D, PCI_CHIP_VIRGL_3D, RES_SHARED_VGA },
    { -1, -1, RES_UNDEFINED }
};
#endif

static const OptionInfoRec *
virgl_available_options (int chipid, int busid)
{
  return NULL;
}

static void
virgl_identify (int flags)
{
    xf86PrintChipsets ("virgl", "Driver for virtual graphics", virglChips);
}

static void
virgl_init_scrn (ScrnInfoPtr pScrn)
{
    pScrn->driverVersion    = 0;
    pScrn->driverName       = VIRGL_DRIVER_NAME;
    pScrn->name             = VIRGL_DRIVER_NAME;

    pScrn->PreInit          = virgl_pre_init_kms;
    pScrn->ScreenInit       = virgl_screen_init_kms;
    pScrn->EnterVT          = virgl_enter_vt_kms;
    pScrn->LeaveVT          = virgl_leave_vt_kms;

    pScrn->ValidMode        = NULL;
}

static char *
CreatePCIBusID(const struct pci_device *dev)
{
    char *busID;

    if (asprintf(&busID, "pci:%04x:%02x:%02x.%d",
                 dev->domain, dev->bus, dev->dev, dev->func) == -1)
        return NULL;

    return busID;
}

static Bool virgl_kernel_mode_enabled(ScrnInfoPtr pScrn, struct pci_device *pci_dev)
{
    char *busIdString;
    int ret;

    busIdString = CreatePCIBusID(pci_dev);
    ret = drmCheckModesettingSupported(busIdString);
    free(busIdString);
    if (ret) {
      xf86DrvMsgVerb(pScrn->scrnIndex, X_INFO, 0,
		   "[KMS] drm report modesetting isn't supported.\n");
	return FALSE;
    }

    xf86DrvMsgVerb(pScrn->scrnIndex, X_INFO, 0,
		   "[KMS] Kernel modesetting enabled.\n");
    return TRUE;
}

static Bool
virgl_pci_probe (DriverPtr drv, int entity, struct pci_device *dev, intptr_t match)
{
    virgl_screen_t *virgl;
    ScrnInfoPtr   pScrn = xf86ConfigPciEntity (NULL, 0, entity, NULL, NULL,
                                               NULL, NULL, NULL, NULL);
    Bool kms = FALSE;
    
    if (!pScrn)
	return FALSE;

    if (!dev)
      return FALSE;

    
    if (virgl_kernel_mode_enabled(pScrn, dev))
      kms = TRUE;

    if (!kms)
      return FALSE;

    if (!pScrn->driverPrivate)
	pScrn->driverPrivate = xnfcalloc (sizeof (virgl_screen_t), 1);
    virgl = pScrn->driverPrivate;
    virgl->pci = dev;
    
    virgl_init_scrn (pScrn);
    
    return TRUE;
}


static DriverRec virgl_driver = {
    0,
    VIRGL_DRIVER_NAME,
    virgl_identify,
    NULL,
    virgl_available_options,
    NULL,
    0,
    NULL,
    virgl_device_match,
    virgl_pci_probe
};

static pointer
virgl_setup (pointer module, pointer opts, int *errmaj, int *errmin)
{
    static Bool loaded = FALSE;
    
    if (!loaded)
    {
	loaded = TRUE;
	xf86AddDriver (&virgl_driver, module, HaveDriverFuncs);
	return (void *)1;
    }
    else
    {
	if (errmaj)
	    *errmaj = LDR_ONCEONLY;
	
	return NULL;
    }
}

_X_EXPORT XF86ModuleData
virglModuleData
= {
    &virgl_module_info,
    virgl_setup,
    NULL
};
