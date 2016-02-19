bin_PROGRAMS = dfu-programmer
dfu_programmer_SOURCES = main.c arguments.c arguments.h atmel.c atmel.h \
                         commands.c commands.h dfu.c dfu.h dfu-bool.h \
                         dfu-device.h intel_hex.c intel_hex.h stm32.c stm32.h \
                         util.c util.h

AM_CFLAGS = $(LIBUSB_CFLAGS) $(LIBUSB1_CFLAGS)

dfu_programmer_LDADD = $(LIBUSB_LIBS) $(LIBUSB1_LIBS)
