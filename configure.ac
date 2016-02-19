#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT([dfu-programmer],[0.7.2],[],[],[https://github.com/dfu-programmer/dfu-programmer])
AC_CONFIG_AUX_DIR([m4])
AC_CONFIG_SRCDIR([src/atmel.c])
AC_CONFIG_HEADERS([src/config.h])

AM_INIT_AUTOMAKE()

# Enable silent rules if available
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AM_MAINTAINER_MODE([enable])

# Checks for programs.
AC_PROG_CC

# Checks for libusb
AC_ARG_ENABLE(old-libusb,
  AS_HELP_STRING([--enable-old-libusb],
    [use old libusb]),
    enable_old_libusb=$enableval,
    enable_old_libusb=no)

AS_IF([test x$enable_old_libusb = xyes], [
  # libusb >= 0.1.8 is required, as we need usb_interrupt_read()
  PKG_CHECK_MODULES([LIBUSB], [libusb >= 0.1.8])
    AC_DEFINE(HAVE_LIBUSB, 1, [Define to 1 if you have libusb.])
    AC_SUBST(LIBUSB_CFLAGS)
    AC_SUBST(LIBUSB_LIBS)
  ], [
  # If libusb-1.0 is enabled and available, prefer that to the old libusb
  PKG_CHECK_MODULES([LIBUSB1], [libusb-1.0 >= 1.0.0])
    AC_DEFINE(HAVE_LIBUSB_1_0, 1, [Define to 1 if you have libusb-1.0.])
    AC_SUBST(LIBUSB1_CFLAGS)
    AC_SUBST(LIBUSB1_LIBS)
  ])

# Checks for header files.
AC_HEADER_STDC

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_MEMCMP

AC_CONFIG_FILES(fedora/dfu-programmer.spec Makefile docs/Makefile src/Makefile)
AC_OUTPUT
