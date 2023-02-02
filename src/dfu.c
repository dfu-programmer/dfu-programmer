/*
 * dfu-programmer
 *
 * $Id$
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <errno.h>
#include <libusb-1.0/libusb.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "dfu.h"
#include "util.h"

// cSpell:words DNBUSY

/* DFU commands */
#define DFU_DETACH    0
#define DFU_DNLOAD    1
#define DFU_UPLOAD    2
#define DFU_GETSTATUS 3
#define DFU_CLRSTATUS 4
#define DFU_GETSTATE  5
#define DFU_ABORT     6

#define USB_CLASS_APP_SPECIFIC 0xfe
#define DFU_SUBCLASS           0x01

/* Wait for 20 seconds before a timeout since erasing/flashing can take some time.
 * The longest erase cycle is for the AT32UC3A0512-TA automotive part,
 * which needs a timeout of at least 19 seconds to erase the whole flash. */
#define DFU_TIMEOUT 20000

/* Time (in ms) for the device to wait for the usb reset after being told to detach
 * before the giving up going into dfu mode. */
#define DFU_DETACH_TIMEOUT 1000

#define DFU_DEBUG_THRESHOLD         100
#define DFU_TRACE_THRESHOLD         200
#define DFU_MESSAGE_DEBUG_THRESHOLD 300

#define DEBUG(...)     dfu_debug (__FILE__, __FUNCTION__, __LINE__, DFU_DEBUG_THRESHOLD, __VA_ARGS__)
#define TRACE(...)     dfu_debug (__FILE__, __FUNCTION__, __LINE__, DFU_TRACE_THRESHOLD, __VA_ARGS__)
#define MSG_DEBUG(...) dfu_debug (__FILE__, __FUNCTION__, __LINE__, DFU_MESSAGE_DEBUG_THRESHOLD, __VA_ARGS__)

// ________  P R O T O T Y P E S  _______________________________
static int32_t dfu_find_interface (struct libusb_device *device, const bool honor_interfaceclass,
                                   const uint8_t bNumConfigurations);
/*  Used to find the dfu interface for a device if there is one.
 *
 *  device - the device to search
 *  honor_interfaceclass - if the actual interface class information
 *                         should be checked, or ignored (bug in device DFU code)
 *
 *  returns the interface number if found, < 0 otherwise
 */

static int32_t dfu_make_idle (dfu_device_t *device, const bool initial_abort);
/*  Gets the device into the dfuIDLE state if possible.
 *
 *  device    - the dfu device to communicate with
 *
 *  returns 0 on success, 1 if device was reset, error otherwise
 */

static int32_t dfu_transfer_out (dfu_device_t *device, uint8_t request, const int32_t value, uint8_t *data,
                                 const size_t length);

static int32_t dfu_transfer_in (dfu_device_t *device, uint8_t request, const int32_t value, uint8_t *data,
                                const size_t length);

static void dfu_msg_response_output (const char *function, const int32_t result);
/*  Used to output the response from our USB request in a human readable
 *  form.
 *
 *  function - the calling function to output on behalf of
 *  result   - the result to interpret
 */

// ________  F U N C T I O N S  _______________________________
void
dfu_set_transaction_num (dfu_device_t *device, uint16_t newNum) {
    TRACE ("%s( %u )\n", __FUNCTION__, newNum);
    device->transaction = newNum;
    DEBUG ("wValue set to %d\n", device->transaction);
}

uint16_t
dfu_get_transaction_num (dfu_device_t *device) {
    TRACE ("%s( %u )\n", __FUNCTION__);
    return device->transaction;
}

int32_t
dfu_detach (dfu_device_t *device, const int32_t timeout) {
    int32_t result;

    TRACE ("%s( %p, %d )\n", __FUNCTION__, device, timeout);

    if ((NULL == device) || (NULL == device->handle) || (timeout < 0)) {
        DEBUG ("Invalid parameter\n");
        return -1;
    }

    result = dfu_transfer_out (device, DFU_DETACH, timeout, NULL, 0);

    dfu_msg_response_output (__FUNCTION__, result);

    return result;
}

int32_t
dfu_download (dfu_device_t *device, const size_t length, uint8_t *data) {
    int32_t result;

    TRACE ("%s( %p, %u, %p )\n", __FUNCTION__, device, length, data);

    /* Sanity checks */
    if ((NULL == device) || (NULL == device->handle)) {
        DEBUG ("Invalid parameter\n");
        return -1;
    }

    if ((0 != length) && (NULL == data)) {
        DEBUG ("data was NULL, but length != 0\n");
        return -2;
    }

    if ((0 == length) && (NULL != data)) {
        DEBUG ("data was not NULL, but length == 0\n");
        return -3;
    }

    {
        size_t i;
        for (i = 0; i < length; i++) { MSG_DEBUG ("Message: m[%u] = 0x%02x\n", i, data[i]); }
    }

    result = dfu_transfer_out (device, DFU_DNLOAD, device->transaction++, data, length);

    dfu_msg_response_output (__FUNCTION__, result);

    return result;
}

int32_t
dfu_upload (dfu_device_t *device, const size_t length, uint8_t *data) {
    int32_t result;

    TRACE ("%s( %p, %u, %p )\n", __FUNCTION__, device, length, data);

    /* Sanity checks */
    if ((NULL == device) || (NULL == device->handle)) {
        DEBUG ("Invalid parameter\n");
        return -1;
    }

    if ((0 == length) || (NULL == data)) {
        DEBUG ("data was NULL, or length is 0\n");
        return -2;
    }

    result = dfu_transfer_in (device, DFU_UPLOAD, device->transaction++, data, length);

    dfu_msg_response_output (__FUNCTION__, result);

    return result;
}

int32_t
dfu_get_status (dfu_device_t *device, dfu_status_t *status) {
    uint8_t buffer[6];
    int32_t result;

    TRACE ("%s( %p, %p )\n", __FUNCTION__, device, status);

    if ((NULL == device) || (NULL == device->handle)) {
        DEBUG ("Invalid parameter\n");
        return -1;
    }

    /* Initialize the status data structure */
    status->bStatus = DFU_STATUS_ERROR_UNKNOWN;
    status->bwPollTimeout = 0;
    status->bState = STATE_DFU_ERROR;
    status->iString = 0;

    result = dfu_transfer_in (device, DFU_GETSTATUS, 0, buffer, sizeof (buffer));

    dfu_msg_response_output (__FUNCTION__, result);

    if (6 == result) {
        status->bStatus = buffer[0];
        status->bwPollTimeout = ((0xff & buffer[3]) << 16) | ((0xff & buffer[2]) << 8) | (0xff & buffer[1]);

        status->bState = buffer[4];
        status->iString = buffer[5];

        DEBUG ("==============================\n");
        DEBUG ("status->bStatus: %s (0x%02x)\n", dfu_status_to_string (status->bStatus), status->bStatus);
        DEBUG ("status->bwPollTimeout: 0x%04x ms\n", status->bwPollTimeout);
        DEBUG ("status->bState: %s (0x%02x)\n", dfu_state_to_string (status->bState), status->bState);
        DEBUG ("status->iString: 0x%02x\n", status->iString);
        DEBUG ("------------------------------\n");
    } else {
        if (0 < result) {
            /* There was an error, we didn't get the entire message. */
            DEBUG ("result: %d\n", result);
            return -2;
        }
    }

    return 0;
}

int32_t
dfu_clear_status (dfu_device_t *device) {
    int32_t result;

    TRACE ("%s( %p )\n", __FUNCTION__, device);

    if ((NULL == device) || (NULL == device->handle)) {
        DEBUG ("Invalid parameter\n");
        return -1;
    }

    result = dfu_transfer_out (device, DFU_CLRSTATUS, 0, NULL, 0);

    dfu_msg_response_output (__FUNCTION__, result);

    return result;
}

int32_t
dfu_get_state (dfu_device_t *device) {
    int32_t result;
    uint8_t buffer[1];

    TRACE ("%s( %p )\n", __FUNCTION__, device);

    if ((NULL == device) || (NULL == device->handle)) {
        DEBUG ("Invalid parameter\n");
        return -1;
    }

    result = dfu_transfer_in (device, DFU_GETSTATE, 0, buffer, sizeof (buffer));

    dfu_msg_response_output (__FUNCTION__, result);

    /* Return the error if there is one. */
    if (result < 1) { return result; }

    /* Return the state. */
    return buffer[0];
}

int32_t
dfu_abort (dfu_device_t *device) {
    int32_t result;

    TRACE ("%s( %p )\n", __FUNCTION__, device);

    if ((NULL == device) || (NULL == device->handle)) {
        DEBUG ("Invalid parameter\n");
        return -1;
    }

    result = dfu_transfer_out (device, DFU_ABORT, 0, NULL, 0);

    dfu_msg_response_output (__FUNCTION__, result);

    return result;
}

struct libusb_device *
dfu_device_init (const uint32_t vendor, const uint32_t product, const uint32_t bus_number,
                 const uint32_t device_address, dfu_device_t *dfu_device, const bool initial_abort,
                 const bool honor_interfaceclass, libusb_context *usb_context) {
    libusb_device **list;
    size_t i, deviceCount;
    int32_t retries = 4;

    TRACE ("%s( %u, %u, %p, %s, %s )\n", __FUNCTION__, vendor, product, dfu_device,
           ((true == initial_abort) ? "true" : "false"), ((true == honor_interfaceclass) ? "true" : "false"));

    DEBUG ("%s(%08x, %08x)\n", __FUNCTION__, vendor, product);

retry:
    deviceCount = libusb_get_device_list (usb_context, &list);

    for (i = 0; i < deviceCount; i++) {
        libusb_device *device = list[i];
        struct libusb_device_descriptor descriptor;

        if (libusb_get_device_descriptor (device, &descriptor)) {
            DEBUG ("Failed in libusb_get_device_descriptor\n");
            break;
        }

        DEBUG ("%2d: 0x%04x, 0x%04x\n", (int)i, descriptor.idVendor, descriptor.idProduct);

        if (vendor != descriptor.idVendor) continue;
        if (product != descriptor.idProduct) continue;

        if (bus_number != 0) {
            if (libusb_get_bus_number (device) != bus_number) continue;
            if (libusb_get_device_address (device) != device_address) continue;
        }

        int32_t tmp;
        DEBUG ("found device at USB:%d,%d\n", libusb_get_bus_number (device), libusb_get_device_address (device));
        // We found a device that looks like it matches...
        // Let's try to find the DFU interface, open the device and claim it.

        tmp = dfu_find_interface (device, honor_interfaceclass, descriptor.bNumConfigurations);

        if (tmp < 0) {
            /* The interface is invalid. */
            DEBUG ("Failed to find interface.\n");
            continue;
        }

        dfu_device->interface = tmp;

        DEBUG ("opening interface %d...\n", tmp);

        tmp = libusb_open (device, &dfu_device->handle);

        DEBUG ("returned %d...\n", tmp);

        if (tmp) {
            DEBUG ("failed to open device\n");
            continue;
        }

        DEBUG ("opened interface %d...\n", tmp);

        tmp = libusb_set_configuration (dfu_device->handle, 1);

        if (tmp) {
            DEBUG ("Failed to set configuration.\n");
            goto done;
        }

        DEBUG ("set configuration %d...\n", 1);

        tmp = libusb_claim_interface (dfu_device->handle, dfu_device->interface);

        if (tmp) {
            DEBUG ("Failed to claim the DFU interface.\n");
            goto done;
        }

        DEBUG ("claimed interface %d...\n", dfu_device->interface);

        tmp = dfu_make_idle (dfu_device, initial_abort);

        if (tmp == 0 || tmp == 1) {
            libusb_free_device_list (list, 1);
            if (!tmp) { return device; }

            retries--;
            goto retry;
        }

        DEBUG ("Failed to put the device in dfuIDLE mode.\n");
        libusb_release_interface (dfu_device->handle, dfu_device->interface);
        retries = 4;

    done:
        libusb_close (dfu_device->handle);
    }

    libusb_free_device_list (list, 1);
    dfu_device->handle = NULL;
    dfu_device->interface = 0;

    return NULL;
}

char *
dfu_state_to_string (const int32_t state) {
    char *message = "unknown state";

    switch (state) {
    case STATE_APP_IDLE: message = "appIDLE"; break;
    case STATE_APP_DETACH: message = "appDETACH"; break;
    case STATE_DFU_IDLE: message = "dfuIDLE"; break;
    case STATE_DFU_DOWNLOAD_SYNC: message = "dfuDNLOAD-SYNC"; break;
    case STATE_DFU_DOWNLOAD_BUSY: message = "dfuDNBUSY"; break;
    case STATE_DFU_DOWNLOAD_IDLE: message = "dfuDNLOAD-IDLE"; break;
    case STATE_DFU_MANIFEST_SYNC: message = "dfuMANIFEST-SYNC"; break;
    case STATE_DFU_MANIFEST: message = "dfuMANIFEST"; break;
    case STATE_DFU_MANIFEST_WAIT_RESET: message = "dfuMANIFEST-WAIT-RESET"; break;
    case STATE_DFU_UPLOAD_IDLE: message = "dfuUPLOAD-IDLE"; break;
    case STATE_DFU_ERROR: message = "dfuERROR"; break;
    }

    return message;
}

char *
dfu_status_to_string (const int32_t status) {
    char *message = "unknown status";

    switch (status) {
    case DFU_STATUS_OK: message = "OK"; break;
    case DFU_STATUS_ERROR_TARGET: message = "errTARGET"; break;
    case DFU_STATUS_ERROR_FILE: message = "errFILE"; break;
    case DFU_STATUS_ERROR_WRITE: message = "errWRITE"; break;
    case DFU_STATUS_ERROR_ERASE: message = "errERASE"; break;
    case DFU_STATUS_ERROR_CHECK_ERASED: message = "errCHECK_ERASED"; break;
    case DFU_STATUS_ERROR_PROG: message = "errPROG"; break;
    case DFU_STATUS_ERROR_VERIFY: message = "errVERIFY"; break;
    case DFU_STATUS_ERROR_ADDRESS: message = "errADDRESS"; break;
    case DFU_STATUS_ERROR_NOTDONE: message = "errNOTDONE"; break;
    case DFU_STATUS_ERROR_FIRMWARE: message = "errFIRMWARE"; break;
    case DFU_STATUS_ERROR_VENDOR: message = "errVENDOR"; break;
    case DFU_STATUS_ERROR_USBR: message = "errUSBR"; break;
    case DFU_STATUS_ERROR_POR: message = "errPOR"; break;
    case DFU_STATUS_ERROR_UNKNOWN: message = "errUNKNOWN"; break;
    case DFU_STATUS_ERROR_STALLEDPKT: message = "errSTALLEDPKT"; break;
    }
    return message;
}

static int32_t
dfu_find_interface (struct libusb_device *device, const bool honor_interfaceclass, const uint8_t bNumConfigurations) {
    int32_t c, i, s;

    TRACE ("%s()\n", __FUNCTION__);

    /* Loop through all of the configurations */
    for (c = 0; c < bNumConfigurations; c++) {
        struct libusb_config_descriptor *config;

        if (libusb_get_config_descriptor (device, c, &config)) {
            DEBUG ("can't get_config_descriptor: %d\n", c);
            return -1;
        }
        DEBUG ("config %d: MaxPower=%d*2 mA\n", c, config->MaxPower);

        /* Loop through all of the interfaces */
        for (i = 0; i < config->bNumInterfaces; i++) {
            struct libusb_interface interface;

            interface = config->interface[i];
            DEBUG ("interface %d\n", i);

            /* Loop through all of the settings */
            for (s = 0; s < interface.num_altsetting; s++) {
                struct libusb_interface_descriptor setting;

                setting = interface.altsetting[s];
                DEBUG ("setting %d: class:%d, subclass %d, protocol:%d\n", s, setting.bInterfaceClass,
                       setting.bInterfaceSubClass, setting.bInterfaceProtocol);

                if (honor_interfaceclass) {
                    /* Check if the interface is a DFU interface */
                    if ((USB_CLASS_APP_SPECIFIC == setting.bInterfaceClass)
                        && (DFU_SUBCLASS == setting.bInterfaceSubClass)) {
                        DEBUG ("Found DFU Interface: %d\n", setting.bInterfaceNumber);
                        return setting.bInterfaceNumber;
                    }
                } else {
                    /* If there is a bug in the DFU firmware, return the first
                     * found interface. */
                    DEBUG ("Found DFU Interface: %d\n", setting.bInterfaceNumber);
                    return setting.bInterfaceNumber;
                }
            }
        }

        libusb_free_config_descriptor (config);
    }

    return -1;
}

static int32_t
dfu_make_idle (dfu_device_t *device, const bool initial_abort) {
    dfu_status_t status;
    int32_t retries = 4;

    if (true == initial_abort) { dfu_abort (device); }

    while (0 < retries) {
        if (0 != dfu_get_status (device, &status)) {
            dfu_clear_status (device);
            continue;
        }

        DEBUG ("State: %s (%d)\n", dfu_state_to_string (status.bState), status.bState);

        switch (status.bState) {
        case STATE_DFU_IDLE:
            if (DFU_STATUS_OK == status.bStatus) { return 0; }

            /* We need the device to have the DFU_STATUS_OK status. */
            dfu_clear_status (device);
            break;

        case STATE_DFU_DOWNLOAD_SYNC: /* abort -> idle */
        case STATE_DFU_DOWNLOAD_IDLE: /* abort -> idle */
        case STATE_DFU_MANIFEST_SYNC: /* abort -> idle */
        case STATE_DFU_UPLOAD_IDLE:   /* abort -> idle */
        case STATE_DFU_DOWNLOAD_BUSY: /* abort -> error */
        case STATE_DFU_MANIFEST: /* abort -> error */ dfu_abort (device); break;

        case STATE_DFU_ERROR: dfu_clear_status (device); break;

        case STATE_APP_IDLE: dfu_detach (device, DFU_DETACH_TIMEOUT); break;

        case STATE_APP_DETACH:
        case STATE_DFU_MANIFEST_WAIT_RESET:
            DEBUG ("Resetting the device\n");
            libusb_reset_device (device->handle);
            return 1;
        }

        retries--;
    }

    DEBUG ("Not able to transition the device into the dfuIDLE state.\n");
    return -2;
}

static int32_t
dfu_transfer_out (dfu_device_t *device, uint8_t request, const int32_t value, uint8_t *data, const size_t length) {
    return libusb_control_transfer (device->handle,
                                    /* bmRequestType */ LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_CLASS
                                        | LIBUSB_RECIPIENT_INTERFACE,
                                    /* bRequest      */ request,
                                    /* wValue        */ value,
                                    /* wIndex        */ device->interface,
                                    /* Data          */ data,
                                    /* wLength       */ length, DFU_TIMEOUT);
}

static int32_t
dfu_transfer_in (dfu_device_t *device, uint8_t request, const int32_t value, uint8_t *data, const size_t length) {
    return libusb_control_transfer (device->handle,
                                    /* bmRequestType */ LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_CLASS
                                        | LIBUSB_RECIPIENT_INTERFACE,
                                    /* bRequest      */ request,
                                    /* wValue        */ value,
                                    /* wIndex        */ device->interface,
                                    /* Data          */ data,
                                    /* wLength       */ length, DFU_TIMEOUT);
}

static void
dfu_msg_response_output (const char *function, const int32_t result) {
    char *msg = NULL;

    if (0 <= result) {
        msg = "No error.";
    } else {
        switch (result) {
        case LIBUSB_ERROR_IO: msg = "LIBUSB_ERROR_IO: Input/output error."; break;
        case LIBUSB_ERROR_INVALID_PARAM: msg = "LIBUSB_ERROR_INVALID_PARAM: Invalid parameter."; break;
        case LIBUSB_ERROR_ACCESS: msg = "LIBUSB_ERROR_ACCESS: Access denied (insufficient permissions)"; break;
        case LIBUSB_ERROR_NO_DEVICE:
            msg = "LIBUSB_ERROR_NO_DEVICE: No such device (it may have been disconnected)";
            break;
        case LIBUSB_ERROR_NOT_FOUND: msg = "LIBUSB_ERROR_NOT_FOUND: Entity not found."; break;
        case LIBUSB_ERROR_BUSY: msg = "LIBUSB_ERROR_BUSY: Resource busy."; break;
        case LIBUSB_ERROR_TIMEOUT: msg = "LIBUSB_ERROR_TIMEOUT: Operation timed out."; break;
        case LIBUSB_ERROR_OVERFLOW: msg = "LIBUSB_ERROR_OVERFLOW: Overflow."; break;
        case LIBUSB_ERROR_PIPE: msg = "LIBUSB_ERROR_PIPE: Pipe error."; break;
        case LIBUSB_ERROR_INTERRUPTED:
            msg = "LIBUSB_ERROR_INTERRUPTED: System call interrupted (perhaps due to signal)";
            break;
        case LIBUSB_ERROR_NO_MEM: msg = "LIBUSB_ERROR_NO_MEM: Insufficient memory."; break;
        case LIBUSB_ERROR_NOT_SUPPORTED:
            msg = "LIBUSB_ERROR_NOT_SUPPORTED: Operation not supported or unimplemented on this platform.";
            break;
        case LIBUSB_ERROR_OTHER: msg = "LIBUSB_ERROR_OTHER: Other error."; break;

        default: msg = "Unknown error"; break;
        }
        DEBUG ("%s ERR: %s 0x%08x (%d)\n", function, msg, result, result);
    }
}

#ifdef malloc
#undef malloc
void *
rpl_malloc (size_t n) {
    if (0 == n) { n = 1; }

    return malloc (n);
}
#endif
