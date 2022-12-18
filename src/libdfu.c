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

#include <libusb-1.0/libusb.h>
#include <string.h>

#include "config.h"
#include "dfu-device.h"
#include "dfu.h"
#include "arguments.h"
#include "commands.h"
#include "atmel.h"
#include "libdfu.h"
#include "config.h"

// NOTE: Technically not thread safe... but since it's not changed when used as a library, it's safe.
int debug;

static const char *progname = PACKAGE;

int dfu_programmer(struct programmer_arguments * args)
{
    int retval = SUCCESS;
    dfu_device_t dfu_device;
    struct libusb_device *device = NULL;
    libusb_context *usbContext;

    memset(&dfu_device, 0, sizeof(dfu_device));

    if (libusb_init(&usbContext))
    {
        fprintf(stderr, "%s: can't init libusb.\n", progname);
        return DEVICE_ACCESS_ERROR;
    }

    if (debug >= 200)
    {
#if LIBUSB_API_VERSION >= 0x01000106
        libusb_set_option(usbContext, LIBUSB_OPTION_LOG_LEVEL, debug);
#else
        libusb_set_debug(usbContext, debug);
#endif
    }

    device = dfu_device_init(args->vendor_id, args->chip_id,
                                args->bus_id, args->device_address,
                                &dfu_device,
                                args->initial_abort,
                                args->honor_interfaceclass,
                                usbContext);

    if (NULL == device)
    {
        fprintf(stderr, "%s: no device present.\n", progname);
        retval = DEVICE_ACCESS_ERROR;
        goto error;
    }

    retval = execute_command(&dfu_device, args);

error:
    if (NULL != dfu_device.handle)
    {
        int rv;

        rv = libusb_release_interface(dfu_device.handle, dfu_device.interface);
        /* The RESET command sometimes causes the usb_release_interface command to fail.
           It is not obvious why this happens but it may be a glitch due to the hardware
           reset in the attached device. In any event, since reset causes a USB detach
           this should not matter, so there is no point in raising an alarm.
        */
        if (0 != rv && !(com_launch == args->command &&
                         args->com_launch_config.noreset == 0))
        {
            fprintf(stderr, "%s: failed to release interface %d.\n",
                    progname, dfu_device.interface);
            retval = DEVICE_ACCESS_ERROR;
        }
    }

    if (NULL != dfu_device.handle)
    {
        libusb_close(dfu_device.handle);
    }

    libusb_exit(usbContext);

    return retval;
}
