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
#
# Process this file with autoconf to produce a configure script

# Initialize Autoconf
AC_PREREQ([2.60])
AC_INIT([xf86-video-virgl],
        [0.1.0],
        [https://bugs.freedesktop.org/enter_bug.cgi?product=xorg],
        [xf86-video-virgl])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CANONICAL_HOST

# Initialize Automake
AM_INIT_AUTOMAKE([foreign dist-bzip2])
AM_MAINTAINER_MODE
AC_CONFIG_HEADERS([config.h])

# Require xorg-macros: XORG_DEFAULT_OPTIONS
m4_ifndef([XORG_MACROS_VERSION],
          [m4_fatal([you must install X.Org util-macros 1.4 or later (or the corresponding package from your distribution) before running autoconf/autogen])])
XORG_MACROS_VERSION(1.4)
XORG_DEFAULT_OPTIONS

# Initialize libtool
AC_SYS_LARGEFILE
AC_DISABLE_STATIC
AC_PROG_INSTALL
AC_PROG_LIBTOOL

AH_TOP([#include "xorg-server.h"])

# Define a configure option for an alternate module directory
AC_ARG_WITH(xorg-module-dir, [  --with-xorg-module-dir=DIR ],
                             [ moduledir="$withval" ],
                             [ moduledir="$libdir/xorg/modules" ])
AC_SUBST(moduledir)

# Store the list of server defined optional extensions in REQUIRED_MODULES
XORG_DRIVER_CHECK_EXT(RANDR, randrproto)
XORG_DRIVER_CHECK_EXT(RENDER, renderproto)
XORG_DRIVER_CHECK_EXT(XV, videoproto)
XORG_DRIVER_CHECK_EXT(XFreeXDGA, xf86dgaproto)

# Obtain compiler/linker options for the driver dependencies
PKG_CHECK_MODULES(XORG, [xorg-server >= 1.0.99.901] xproto fontsproto $REQUIRED_MODULES)


save_CFLAGS="$CFLAGS"
CFLAGS="$XORG_CFLAGS"
AC_CHECK_DECL(XSERVER_LIBPCIACCESS,
	      [XSERVER_LIBPCIACCESS=yes], [XSERVER_LIBPCIACCESS=no],
	      [#include "xorg-server.h"])
CFLAGS="$save_CFLAGS"

# Checks for libraries.
PKG_CHECK_MODULES(DRM, [libdrm >= 2.4.42])

AC_ARG_ENABLE(kms,
	      AS_HELP_STRING([--disable-kms],
	                     [Disable KMS support [[default=enabled]]]),		     [DRM_MODE="$enableval"],
	     [DRM_MODE=yes])

if test "x$XSERVER_LIBPCIACCESS" = xyes; then
    PKG_CHECK_MODULES([PCIACCESS], [pciaccess >= 0.10])
fi
AM_CONDITIONAL(XSERVER_LIBPCIACCESS, test "x$XSERVER_LIBPCIACCESS" = xyes)
save_CFLAGS="$CFLAGS"
CFLAGS="$DRM_CFLAGS $CFLAGS"
if test "x$DRM_MODE" = xyes; then
   	AC_CHECK_HEADER(xf86drmMode.h,[DRM_MODE=yes],[DRM_MODE=no],[#include <stdint.h>
#include <stdlib.h>])
	if test "x$DRM_MODE" = xyes; then
	   	AC_DEFINE(XF86DRM_MODE,1,[DRM kernel modesetting])
        else
                DRM_MODE=no
        fi
fi
CFLAGS="$save_CFLAGS"
AM_CONDITIONAL(DRM_MODE, test x$DRM_MODE = xyes)

# AC_CHECK_FILE is not supported when cross compiling
if test "$cross_compiling" = "no" ; then
    AC_CHECK_FILE(.git, [
        GIT_VERSION=`git log -1 --format=%h`
        AC_DEFINE_UNQUOTED([GIT_VERSION], ["$GIT_VERSION"], [Defined if building from git])
        ]
    )
fi

AC_CONFIG_FILES([
                Makefile
                src/Makefile
                src/uxa/Makefile
])
AC_OUTPUT

dnl ==========================================================================
echo "

        xf86-video-virgl $VERSION
        =====================

        prefix:                   ${prefix}
        c compiler:               ${CC}

        drm:                      ${DRM_CFLAGS}
        KMS:                      ${DRM_MODE}
"
