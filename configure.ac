#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([dfu-programmer],[1.1.0],[https://github.com/dfu-programmer/dfu-programmer/issues],[],[https://dfu-programmer.github.io/])
AC_CONFIG_AUX_DIR([m4])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([src/atmel.c])
AM_INIT_AUTOMAKE([foreign -Wall subdir-objects])
AC_CONFIG_HEADERS([src/config.h])

AM_MAINTAINER_MODE

# Enable silent rules if available
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

# Checks for programs.
AC_PROG_CC

# Checks for header files.
AC_CHECK_HEADERS([stddef.h stdint.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_MEMCMP

# Checks for libusb. 1.0.22 is the oldest release with libusb_set_option(),
# which dfu-programmer uses. Prefer pkg-config, which knows where the headers
# are (e.g. Homebrew on Apple Silicon), and fall back to a plain link test.
m4_ifdef([PKG_CHECK_MODULES],
  [PKG_CHECK_MODULES([LIBUSB], [libusb-1.0 >= 1.0.22],
    [CPPFLAGS="$CPPFLAGS $LIBUSB_CFLAGS"
     LIBS="$LIBUSB_LIBS $LIBS"],
    [AC_MSG_NOTICE([libusb-1.0 >= 1.0.22 not found with pkg-config, trying without it])])])
AC_SEARCH_LIBS([libusb_set_option], [usb-1.0], [],
  [AC_MSG_ERROR([libusb 1.0.22 or newer not found])])
# The sources include <libusb-1.0/libusb.h>, or <libusb.h> when the include
# path already points into the libusb-1.0 directory (as pkg-config's does).
LIBUSB_INCLUDE_FLAGS=""
AC_CHECK_HEADER([libusb-1.0/libusb.h], [],
  [AC_CHECK_HEADER([libusb.h], [LIBUSB_INCLUDE_FLAGS="-DDFU_LIBUSB_FLAT_INCLUDE"],
    [AC_MSG_ERROR([libusb.h not found])])])
AC_SUBST([LIBUSB_INCLUDE_FLAGS])

# Optional: turn compiler warnings into errors (used by CI).
AC_ARG_ENABLE([werror],
  [AS_HELP_STRING([--enable-werror], [treat compiler warnings as errors])],
  [], [enable_werror=no])
AS_IF([test "x$enable_werror" = "xyes"], [WERROR_CFLAGS="-Werror"], [WERROR_CFLAGS=""])
AC_SUBST([WERROR_CFLAGS])

AC_CONFIG_FILES([dfu-programmer.spec Makefile docs/Makefile src/Makefile])
AC_OUTPUT
