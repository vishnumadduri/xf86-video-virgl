#  Copyright 2008 Red Hat, Inc.
#
#  Permission is hereby granted, free of charge, to any person obtaining a
#  copy of this software and associated documentation files (the "Software"),
#  to deal in the Software without restriction, including without limitation
#  on the rights to use, copy, modify, merge, publish, distribute, sub
#  license, and/or sell copies of the Software, and to permit persons to whom
#  the Software is furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice (including the next
#  paragraph) shall be included in all copies or substantial portions of the
#  Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
#  THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
#  IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
#  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


# this is obnoxious:
# -module lets us name the module exactly how we want
# -avoid-version prevents gratuitous .0.0.0 version numbers on the end
# _ladir passes a dummy rpath to libtool so the thing will actually link
# TODO: -nostdlib/-Bstatic/-lgcc platform magic, not installing the .a, etc.

SUBDIRS=uxa

AM_CFLAGS = $(XORG_CFLAGS) $(PCIACCESS_CFLAGS) $(CWARNFLAGS) $(DRM_CFLAGS)

virgl_drv_la_LTLIBRARIES = virgl_drv.la
virgl_drv_la_LDFLAGS = -module -avoid-version
virgl_drv_ladir = @moduledir@/drivers

virgl_drv_la_LIBADD = uxa/libuxa.la

virgl_drv_la_SOURCES =				\
	virgl.h					\
	virgl_uxa.c			\
	virgl_kms.c			\
	virgl_drmmode.c			\
	virgl_drmmode.h			\
	virgl_dri2.c 			\
	virgl_surface.c 			\
	compat-api.h 
