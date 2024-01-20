#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT([dfu-programmer],[1.1.0],[https://github.com/dfu-programmer/dfu-programmer/issues],[],[https://dfu-programmer.github.io/])
AC_CONFIG_AUX_DIR(m4)
AC_CONFIG_SRCDIR([src/atmel.c])
AM_INIT_AUTOMAKE
AC_CONFIG_HEADERS([src/config.h])

AM_MAINTAINER_MODE

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
#AC_CHECK_FUNC([memset], :, [AC_CHECK_LIB([libc], [libc])])

# Checks for libusb.
AC_SEARCH_LIBS(libusb_init, usb-1.0,, [AC_MSG_ERROR([libusb 1.0 not found])])

AC_CONFIG_FILES(dfu-programmer.spec Makefile docs/Makefile src/Makefile)
AC_OUTPUT
