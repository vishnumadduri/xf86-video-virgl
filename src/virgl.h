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

#ifndef VIRGL_H
#define VIRGL_H

#include <stdint.h>

#include "compiler.h"
#include "xf86.h"
#if GET_ABI_MAJOR(ABI_VIDEODRV_VERSION) < 6
#include "xf86Resources.h"
#endif
#include "xf86Cursor.h"
#include "xf86_OSproc.h"
#ifdef XV
#include "xf86xv.h"
#endif
#include "xf86Crtc.h"
#include "shadow.h"
#include "micmap.h"
#include "uxa/uxa.h"

#include "list.h"
#ifdef XSERVER_PCIACCESS
#include "pciaccess.h"
#endif
#include "xf86platformBus.h"
#include "fb.h"
#include "vgaHW.h"

#include "virgl_drmmode.h"

#include "compat-api.h"
#define hidden _X_HIDDEN

#define VIRGL_NAME		"virgl"
#define VIRGL_DRIVER_NAME		"virgl"
#define PCI_VENDOR_RED_HAT	0x1b36
#define PCI_VENDOR_VIRTIO	0x1af4

#define PCI_CHIP_VIRGL_3D	0x3d
#define PCI_CHIP_VIRTIO_3D	0x1010
 
typedef struct _virgl_screen_t virgl_screen_t;

struct virgl_bo;

struct virgl_surface_t
{
    virgl_screen_t *virgl;

    pixman_image_t *	host_image;

    uxa_access_t	access_type;
    RegionRec		access_region;
    PixmapPtr		pixmap;

    struct virgl_bo *bo;

    union
    {
	struct virgl_surface_t *copy_src;
    } u;

};

typedef struct virgl_surface_t virgl_surface_t;

/*
 * Config Options
 */

enum {
    OPTION_COUNT,
};

/*
 * for relocations
 * dst_bo + dst_offset are the bo and offset into which the reloc is being written,
 * src_bo is the bo who's offset is being relocated.
 */
struct virgl_bo_funcs {
    void *(*bo_map)(struct virgl_bo *bo);
    void (*bo_unmap)(struct virgl_bo *bo);
    void (*bo_decref)(virgl_screen_t *virgl, struct virgl_bo *bo);
    virgl_surface_t *(*create_surface)(virgl_screen_t *virgl, int width,
				     int height, int bpp, uint32_t usage_hint);
    void (*destroy_surface)(virgl_surface_t *surf);

  /* surface create / destroy */
};
    
void virgl_kms_setup_funcs(virgl_screen_t *virgl);

#define MAX_RELOCS 96
#include "drm/virtgpu_drm.h"

struct virgl_cmd_stream {
  struct virgl_bo *reloc_bo[MAX_RELOCS];
  int n_reloc_bos;
};

struct graw_encoder_state {
   uint32_t *buf;
   uint32_t buf_total;
   uint32_t buf_offset;

   /* for testing purposes */
   uint32_t buf_read_offset;

   void (*flush)(struct graw_encoder_state *state, void *closure);
   void *closure;
   int fd;
};

struct _virgl_screen_t
{
    int                         device_primary;
    struct virgl_bo *             primary_bo;

    int				virtual_x;
    int				virtual_y;

    /* not the same as the heads mode for #head > 1 or virtual != head size */
    virgl_surface_t *		primary;

    EntityInfoPtr		entity;

    int                         num_heads;
    xf86CrtcPtr *               crtcs;
    xf86OutputPtr *             outputs;

#ifdef XSERVER_LIBPCIACCESS
    struct pci_device *		pci;
#else
    pciVideoPtr			pci;
    PCITAG			pci_tag;
#endif
    struct xf86_platform_device *platdev;
    vgaRegRec                   vgaRegs;

    uxa_driver_t *		uxa;
    
    CreateScreenResourcesProcPtr create_screen_resources;
    CloseScreenProcPtr		close_screen;
    CreateGCProcPtr		create_gc;
    CopyWindowProcPtr		copy_window;
    
    int16_t			cur_x;
    int16_t			cur_y;
    int16_t			hot_x;
    int16_t			hot_y;
    
    ScrnInfoPtr			pScrn;

    struct xorg_list ums_bos;
    struct virgl_bo_funcs *bo_funcs;

    Bool kms_enabled;

    drmmode_rec drmmode;
    int drm_fd;
    char *drm_device_name;
    /* kernel command stream */
    struct virgl_cmd_stream cmds;


    DamagePtr damage;
    ScreenBlockHandlerProcPtr BlockHandler;
    struct graw_encoder_state *gr_enc;

    Bool has_3d_accel;
};

void		    virgl_surface_set_pixmap (virgl_surface_t *surface,
					    PixmapPtr      pixmap);

/* UXA */
#if HAS_DEVPRIVATEKEYREC
extern DevPrivateKeyRec uxa_pixmap_index;
#else
extern int uxa_pixmap_index;
#endif
Bool
virgl_uxa_init (virgl_screen_t *virgl, ScreenPtr screen);

static inline virgl_surface_t *get_surface (PixmapPtr pixmap)
{
#if HAS_DEVPRIVATEKEYREC
    return dixGetPrivate(&pixmap->devPrivates, &uxa_pixmap_index);
#else
    return dixLookupPrivate(&pixmap->devPrivates, &uxa_pixmap_index);
#endif
}

static inline void set_surface (PixmapPtr pixmap, virgl_surface_t *surface)
{
    dixSetPrivate(&pixmap->devPrivates, &uxa_pixmap_index, surface);
}

Bool virgl_pre_init_common(ScrnInfoPtr pScrn);
Bool virgl_fb_init (virgl_screen_t *virgl, ScreenPtr pScreen);
Bool virgl_screen_init_kms(SCREEN_INIT_ARGS_DECL);
Bool virgl_enter_vt_kms (VT_FUNC_ARGS_DECL);
void virgl_leave_vt_kms (VT_FUNC_ARGS_DECL);
void virgl_set_screen_pixmap_header (ScreenPtr pScreen);
Bool virgl_resize_primary_to_virtual (virgl_screen_t *virgl);

Bool virgl_pre_init_kms(ScrnInfoPtr pScrn, int flags);
Bool virgl_kms_check_cap(virgl_screen_t *virgl, int cap);
uint32_t virgl_kms_bo_get_handle(struct virgl_bo *_bo);
uint32_t virgl_kms_bo_get_res_handle(struct virgl_bo *_bo);
int virgl_kms_get_kernel_name(struct virgl_bo *_bo, uint32_t *name);

int virgl_kms_3d_resource_migrate(struct virgl_surface_t *surf);
void virgl_kms_transfer_block(struct virgl_surface_t *surf,
			    int x1, int y1, int x2, int y2);
void virgl_kms_transfer_get_block(struct virgl_surface_t *surf,
				int x1, int y1, int x2, int y2);
struct virgl_bo *virgl_bo_create_primary_resource(virgl_screen_t *virgl, uint32_t width, uint32_t height, int32_t stride, uint32_t format, int flags);
int virgl_execbuffer(int fd, uint32_t *block, int ndw);
struct graw_encoder_state *graw_encoder_init_queue(int fd);
int graw_encode_resource_copy_region(struct graw_encoder_state *enc,
                                     uint32_t dst_res_handle,
                                     unsigned dst_level,
                                     unsigned dstx, unsigned dsty, unsigned dstz,
                                     uint32_t src_res_handle,
                                     unsigned src_level,
                                     const struct drm_virtgpu_3d_box *src_box);
int graw_encode_blit(struct graw_encoder_state *enc,
                     uint32_t dst_handle, uint32_t src_handle,
		     struct drm_virtgpu_3d_box *dbox,
		     struct drm_virtgpu_3d_box *sbox);

#ifdef WITH_CHECK_POINT
#define CHECK_POINT() ErrorF ("%s: %d  (%s)\n", __FILE__, __LINE__, __FUNCTION__);
#else
#define CHECK_POINT()
#endif

Bool virgl_dri2_init(ScreenPtr pScreen);
void virgl_dri2_fini(ScreenPtr pScreen);

void *              virgl_surface_get_host_bits(virgl_surface_t *surface);

virgl_surface_t *virgl_create_primary (virgl_screen_t *virgl, int bpp);
void virgl_get_formats (int bpp, pixman_format_code_t *pformat, uint32_t *virgl_format);
void virgl_flush(virgl_screen_t *virgl);
#define VIRGL_CREATE_PIXMAP_DRI2 0x10000000

struct virgl_bo *virgl_bo_create_argb_cursor_resource(virgl_screen_t *virgl,
							  uint32_t width, uint32_t height);

/* formats */
#define VIRGL_FORMAT_B8G8R8A8_UNORM 1
#define VIRGL_FORMAT_B8G8R8X8_UNORM 2

#define VIRGL_FORMAT_B5G6R5_UNORM   7
#define VIRGL_FORMAT_A8_UNORM       10

#endif // VIRGL_H

