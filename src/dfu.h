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

#ifndef __DFU_H__
#define __DFU_H__

#if HAVE_CONFIG_H
# include <config.h>
#endif
#ifdef HAVE_LIBUSB_1_0
#include <libusb.h>
#else
#include <usb.h>
#endif
#include <stdint.h>
#include <stddef.h>

#include "dfu-bool.h"
#include "dfu-device.h"

/* DFU states */
#define STATE_APP_IDLE                  0x00
#define STATE_APP_DETACH                0x01
#define STATE_DFU_IDLE                  0x02
#define STATE_DFU_DOWNLOAD_SYNC         0x03
#define STATE_DFU_DOWNLOAD_BUSY         0x04
#define STATE_DFU_DOWNLOAD_IDLE         0x05
#define STATE_DFU_MANIFEST_SYNC         0x06
#define STATE_DFU_MANIFEST              0x07
#define STATE_DFU_MANIFEST_WAIT_RESET   0x08
#define STATE_DFU_UPLOAD_IDLE           0x09
#define STATE_DFU_ERROR                 0x0a


/* DFU status */
#define DFU_STATUS_OK                   0x00
#define DFU_STATUS_ERROR_TARGET         0x01
#define DFU_STATUS_ERROR_FILE           0x02
#define DFU_STATUS_ERROR_WRITE          0x03
#define DFU_STATUS_ERROR_ERASE          0x04
#define DFU_STATUS_ERROR_CHECK_ERASED   0x05
#define DFU_STATUS_ERROR_PROG           0x06
#define DFU_STATUS_ERROR_VERIFY         0x07
#define DFU_STATUS_ERROR_ADDRESS        0x08
#define DFU_STATUS_ERROR_NOTDONE        0x09
#define DFU_STATUS_ERROR_FIRMWARE       0x0a
#define DFU_STATUS_ERROR_VENDOR         0x0b
#define DFU_STATUS_ERROR_USBR           0x0c
#define DFU_STATUS_ERROR_POR            0x0d
#define DFU_STATUS_ERROR_UNKNOWN        0x0e
#define DFU_STATUS_ERROR_STALLEDPKT     0x0f


/* This is based off of DFU_GETSTATUS
 *
 *  1 unsigned byte bStatus
 *  3 unsigned byte bwPollTimeout
 *  1 unsigned byte bState
 *  1 unsigned byte iString
*/

typedef struct {
    uint8_t bStatus;
    uint32_t bwPollTimeout;
    uint8_t bState;
    uint8_t iString;
} dfu_status_t;

void dfu_set_transaction_num( uint16_t newnum );
/* set / reset the wValue parameter to a given value. this number is
 * significant for stm32 device commands (see dfu-device.h)
 */

uint16_t dfu_get_transaction_num( void );
/* get the current transaction number (can be used to calculate address
 * offset for stm32 devices --- see dfu-device.h)
 */

int32_t dfu_detach( dfu_device_t *device, const int32_t timeout );
/*  DFU_DETACH Request (DFU Spec 1.0, Section 5.1)
 *
 *  device    - the dfu device to commmunicate with
 *  timeout   - the timeout in ms the USB device should wait for a pending
 *              USB reset before giving up and terminating the operation
 *
 *  returns 0 or < 0 on error
 */

int32_t dfu_download( dfu_device_t *device, const size_t length, uint8_t* data );
/*  DFU_DNLOAD Request (DFU Spec 1.0, Section 6.1.1)
 *
 *  device    - the dfu device to commmunicate with
 *  length    - the total number of bytes to transfer to the USB
 *              device - must be less than wTransferSize
 *  data      - the data to transfer
 *
 *  returns the number of bytes written or < 0 on error
 */

int32_t dfu_upload( dfu_device_t *device, const size_t length, uint8_t* data );
/*  DFU_UPLOAD Request (DFU Spec 1.0, Section 6.2)
 *
 *  device    - the dfu device to commmunicate with
 *  length    - the maximum number of bytes to receive from the USB
 *              device - must be less than wTransferSize
 *  data      - the buffer to put the received data in
 *
 *  returns the number of bytes received or < 0 on error
 */

int32_t dfu_get_status( dfu_device_t *device, dfu_status_t *status );
/*  DFU_GETSTATUS Request (DFU Spec 1.0, Section 6.1.2)
 *
 *  device    - the dfu device to commmunicate with
 *  status    - the data structure to be populated with the results
 *
 *  return the 0 if successful or < 0 on an error
 */

int32_t dfu_clear_status( dfu_device_t *device );
/*  DFU_CLRSTATUS Request (DFU Spec 1.0, Section 6.1.3)
 *
 *  device    - the dfu device to commmunicate with
 *
 *  return 0 or < 0 on an error
 */

int32_t dfu_get_state( dfu_device_t *device );
/*  DFU_GETSTATE Request (DFU Spec 1.0, Section 6.1.5)
 *
 *  device    - the dfu device to commmunicate with
 *
 *  returns the state or < 0 on error
 */

int32_t dfu_abort( dfu_device_t *device );
/*  DFU_ABORT Request (DFU Spec 1.0, Section 6.1.4)
 *
 *  device    - the dfu device to commmunicate with
 *
 *  returns 0 or < 0 on an error
 */


#ifdef HAVE_LIBUSB_1_0
struct libusb_device
#else
struct usb_device
#endif
                     *dfu_device_init( const uint32_t vendor,
                                       const uint32_t product,
                                       const uint32_t bus,
                                       const uint32_t dev_addr,
                                       dfu_device_t *device,
                                       const dfu_bool initial_abort,
                                       const dfu_bool honor_interfaceclass );
/*  dfu_device_init is designed to find one of the usb devices which match
 *  the vendor and product parameters passed in.
 *
 *  vendor  - the vender number of the device to look for
 *  product - the product number of the device to look for
 *  [out] device - the dfu device to commmunicate with
 *
 *  return a pointer to the usb_device if found, or NULL otherwise
 */

char* dfu_status_to_string( const int32_t status );
/*  Used to convert the DFU status to a string.
 *
 *  status - the status to convert
 *
 *  returns the status name or "unknown status"
 */

char* dfu_state_to_string( const int32_t state );
/*  Used to convert the DFU state to a string.
 *
 *  state - the state to convert
 *
 *  returns the state name or "unknown state"
 */

#endif
