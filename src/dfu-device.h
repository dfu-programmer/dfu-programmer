#ifndef __DFU_DEVICE_H__
#define __DFU_DEVICE_H__

#include <stdint.h>
#include <usb.h>

typedef enum {
    device_class__8051,
    device_class__AVR,
    device_class__AVR32
} atmel_device_class_t;

typedef struct {
    struct usb_dev_handle *handle;
    int32_t interface;
    atmel_device_class_t type;
} dfu_device_t;

#endif
