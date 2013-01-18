#ifndef __DFU_DEVICE_H__
#define __DFU_DEVICE_H__

#if HAVE_CONFIG_H
# include <config.h>
#endif
#include <stdint.h>
#ifdef HAVE_LIBUSB_1_0
#include <libusb.h>
#else
#include <usb.h>
#endif

// Atmel device classes are now defined with one bit per class.
// This simplifies checking in functions which handle more than one class.
#define ADC_8051    (1<<0)
#define ADC_AVR     (1<<1)
#define ADC_AVR32   (1<<2)
#define ADC_XMEGA   (1<<3)

// Most commands fall into one of 2 groups.
#define GRP_AVR32   (ADC_AVR32 | ADC_XMEGA)
#define GRP_AVR     (ADC_AVR | ADC_8051)

typedef unsigned atmel_device_class_t;

typedef struct {
#ifdef HAVE_LIBUSB_1_0
    struct libusb_device_handle *handle;
#else
    struct usb_dev_handle *handle;
#endif
    int32_t interface;
    atmel_device_class_t type;
} dfu_device_t;

#endif
