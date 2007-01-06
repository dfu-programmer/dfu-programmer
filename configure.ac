#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
#AC_INIT(dfu-programmer, VERSION, BUG-REPORT-ADDRESS)
AC_INIT([dfu-programmer],[0.3.1])
AC_CONFIG_AUX_DIR(m4)
AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)
AC_CONFIG_SRCDIR([src/atmel.c])
AC_CONFIG_HEADER([config.h])

AM_MAINTAINER_MODE

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
AC_CHECK_LIB([usb], [usb_control_msg],,
                            [AC_MSG_ERROR([libusb not found])] )

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([usb.h stdlib.h string.h stdio.h errno.h stdarg.h stddef.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_MEMCMP
AC_CHECK_FUNCS([memset])

AC_CONFIG_FILES(dfu-programmer.spec Makefile docs/Makefile src/Makefile)
AC_OUTPUT
