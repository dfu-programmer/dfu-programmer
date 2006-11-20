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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <stdarg.h>
#include <usb.h>
#include <errno.h>
#include "dfu.h"
#include "util.h"

/* DFU commands */
#define DFU_DETACH      0
#define DFU_DNLOAD      1
#define DFU_UPLOAD      2
#define DFU_GETSTATUS   3
#define DFU_CLRSTATUS   4
#define DFU_GETSTATE    5
#define DFU_ABORT       6

/* Wait for 10 seconds before a timeout since erasing/flashing can take some time. */
#define DFU_TIMEOUT 10000

#define DFU_DEBUG_THRESHOLD 100

#define DEBUG(...)  dfu_debug( __FILE__, __FUNCTION__, __LINE__, \
                               DFU_DEBUG_THRESHOLD, __VA_ARGS__ )

static unsigned short transaction = 0;

static void dfu_msg_response_output( const char *function, const int result );


/*
 *  DFU_DETACH Request (DFU Spec 1.0, Section 5.1)
 *
 *  device    - the usb_dev_handle to communicate with
 *  interface - the interface to communicate with
 *  timeout   - the timeout in ms the USB device should wait for a pending
 *              USB reset before giving up and terminating the operation
 *
 *  returns 0 or < 0 on error
 */
int dfu_detach( struct usb_dev_handle *device,
                const unsigned short interface,
                const unsigned short timeout )
{
    int result;

    result = usb_control_msg( device,
          /* bmRequestType */ USB_ENDPOINT_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
          /* bRequest      */ DFU_DETACH,
          /* wValue        */ timeout,
          /* wIndex        */ interface,
          /* Data          */ NULL,
          /* wLength       */ 0,
                              DFU_TIMEOUT );

    dfu_msg_response_output( __FUNCTION__, result );

    return result;
}


/*
 *  DFU_DNLOAD Request (DFU Spec 1.0, Section 6.1.1)
 *
 *  device    - the usb_dev_handle to communicate with
 *  interface - the interface to communicate with
 *  length    - the total number of bytes to transfer to the USB
 *              device - must be less than wTransferSize
 *  data      - the data to transfer
 *
 *  returns the number of bytes written or < 0 on error
 */
int dfu_download( struct usb_dev_handle *device,
                  const unsigned short interface,
                  const unsigned short length,
                  char* data )
{
    int result;

    /* Sanity checks */
    if( (0 != length) && (NULL == data) ) {
        DEBUG( "data was NULL, but length != 0\n" );
        return -1;
    }

    if( (0 == length) && (NULL != data) ) {
        DEBUG( "data was not NULL, but length == 0\n" );
        return -2;
    }

    result = usb_control_msg( device,
          /* bmRequestType */ USB_ENDPOINT_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
          /* bRequest      */ DFU_DNLOAD,
          /* wValue        */ transaction++,
          /* wIndex        */ interface,
          /* Data          */ data,
          /* wLength       */ length,
                              DFU_TIMEOUT );

    dfu_msg_response_output( __FUNCTION__, result );

    return result;
}


/*
 *  DFU_UPLOAD Request (DFU Spec 1.0, Section 6.2)
 *
 *  device    - the usb_dev_handle to communicate with
 *  interface - the interface to communicate with
 *  length    - the maximum number of bytes to receive from the USB
 *              device - must be less than wTransferSize
 *  data      - the buffer to put the received data in
 *
 *  returns the number of bytes received or < 0 on error
 */
int dfu_upload( struct usb_dev_handle *device,
                const unsigned short interface,
                const unsigned short length,
                char* data )
{
    int result;

    /* Sanity checks */
    if( (0 == length) || (NULL == data) ) {
        DEBUG( "data was NULL, or length is 0\n" );
        return -1;
    }

    result = usb_control_msg( device,
          /* bmRequestType */ USB_ENDPOINT_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
          /* bRequest      */ DFU_UPLOAD,
          /* wValue        */ transaction++,
          /* wIndex        */ interface,
          /* Data          */ data,
          /* wLength       */ length,
                              DFU_TIMEOUT );

    dfu_msg_response_output( __FUNCTION__, result );

    return result;
}


/*
 *  DFU_GETSTATUS Request (DFU Spec 1.0, Section 6.1.2)
 *
 *  device    - the usb_dev_handle to communicate with
 *  interface - the interface to communicate with
 *  status    - the data structure to be populated with the results
 *
 *  return the 0 if successful or < 0 on an error
 */
int dfu_get_status( struct usb_dev_handle *device,
                    const unsigned short interface,
                    struct dfu_status *status )
{
    char buffer[6];
    int result;

    /* Initialize the status data structure */
    status->bStatus       = DFU_STATUS_ERROR_UNKNOWN;
    status->bwPollTimeout = 0;
    status->bState        = STATE_DFU_ERROR;
    status->iString       = 0;

    result = usb_control_msg( device,
          /* bmRequestType */ USB_ENDPOINT_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
          /* bRequest      */ DFU_GETSTATUS,
          /* wValue        */ 0,
          /* wIndex        */ interface,
          /* Data          */ buffer,
          /* wLength       */ 6,
                              DFU_TIMEOUT );

    dfu_msg_response_output( __FUNCTION__, result );

    if( 6 == result ) {
        status->bStatus = buffer[0];
        status->bwPollTimeout = ((0xff & buffer[3]) << 16) |
                                ((0xff & buffer[2]) << 8)  |
                                (0xff & buffer[1]);

        status->bState  = buffer[4];
        status->iString = buffer[5];

        DEBUG( "==============================\n" );
        DEBUG( "status->bStatus: %s (0x%02x)\n",
               dfu_status_to_string(status->bStatus), status->bStatus );
        DEBUG( "status->bwPollTimeout: 0x%04x\n", status->bwPollTimeout );
        DEBUG( "status->bState: %s (0x%02x)\n",
               dfu_state_to_string(status->bState), status->bState );
        DEBUG( "status->iString: 0x%02x\n", status->iString );
        DEBUG( "------------------------------\n" );
    } else {
        if( 0 < result ) {
            /* There was an error, we didn't get the entire message. */
            DEBUG( "result: %d\n", result );
            return -1;
        }
    }

    return 0;
}


/*
 *  DFU_CLRSTATUS Request (DFU Spec 1.0, Section 6.1.3)
 *
 *  device    - the usb_dev_handle to communicate with
 *  interface - the interface to communicate with
 *
 *  return 0 or < 0 on an error
 */
int dfu_clear_status( struct usb_dev_handle *device,
                      const unsigned short interface )
{
    int result;

    result = usb_control_msg( device,
          /* bmRequestType */ USB_ENDPOINT_OUT| USB_TYPE_CLASS | USB_RECIP_INTERFACE,
          /* bRequest      */ DFU_CLRSTATUS,
          /* wValue        */ 0,
          /* wIndex        */ interface,
          /* Data          */ NULL,
          /* wLength       */ 0,
                              DFU_TIMEOUT );

    dfu_msg_response_output( __FUNCTION__, result );

    return result;
}


/*
 *  DFU_GETSTATE Request (DFU Spec 1.0, Section 6.1.5)
 *
 *  device    - the usb_dev_handle to communicate with
 *  interface - the interface to communicate with
 *  length    - the maximum number of bytes to receive from the USB
 *              device - must be less than wTransferSize
 *  data      - the buffer to put the received data in
 *
 *  returns the state or < 0 on error
 */
int dfu_get_state( struct usb_dev_handle *device,
                   const unsigned short interface )
{
    int result;
    char buffer[1];

    result = usb_control_msg( device,
          /* bmRequestType */ USB_ENDPOINT_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
          /* bRequest      */ DFU_GETSTATE,
          /* wValue        */ 0,
          /* wIndex        */ interface,
          /* Data          */ buffer,
          /* wLength       */ 1,
                              DFU_TIMEOUT );

    dfu_msg_response_output( __FUNCTION__, result );

    /* Return the error if there is one. */
    if( result < 1 ) {
        return result;
    }

    /* Return the state. */
    return buffer[0];
}


/*
 *  DFU_ABORT Request (DFU Spec 1.0, Section 6.1.4)
 *
 *  device    - the usb_dev_handle to communicate with
 *  interface - the interface to communicate with
 *
 *  returns 0 or < 0 on an error
 */
int dfu_abort( struct usb_dev_handle *device,
               const unsigned short interface )
{
    int result;

    result = usb_control_msg( device,
          /* bmRequestType */ USB_ENDPOINT_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
          /* bRequest      */ DFU_ABORT,
          /* wValue        */ 0,
          /* wIndex        */ interface,
          /* Data          */ NULL,
          /* wLength       */ 0,
                              DFU_TIMEOUT );

    dfu_msg_response_output( __FUNCTION__, result );

    return result;
}


/*
 *  Gets the device into the dfuIDLE state if possible.
 *
 *  device - the usb_dev_handle to communicate with
 *  interface - the interface to communicate with
 *
 *  returns 0 on success or < 0 on an error
 */
int dfu_make_idle( struct usb_dev_handle *device,
                   const unsigned short interface )
{
    struct dfu_status status;
    int result;
    int retries = 4;

    while( 0 < retries ) {
        result = dfu_get_status( device, interface, &status );
        if( 0 != result ) {
            dfu_clear_status( device, interface );
            continue;
        }

        switch( status.bState ) {
            case STATE_DFU_IDLE:
                DEBUG( "status.bState: STATE_DFU_IDLE\n" );
                if( DFU_STATUS_OK == status.bStatus ) {
                    return 0;
                }

                /* We need the device to have the DFU_STATUS_OK status. */
                dfu_clear_status( device, interface );
                break;

            case STATE_DFU_DOWNLOAD_SYNC:   /* abort -> idle */
            case STATE_DFU_DOWNLOAD_IDLE:   /* abort -> idle */
            case STATE_DFU_MANIFEST_SYNC:   /* abort -> idle */
            case STATE_DFU_UPLOAD_IDLE:     /* abort -> idle */
            case STATE_DFU_DOWNLOAD_BUSY:   /* abort -> error */
            case STATE_DFU_MANIFEST:        /* abort -> error */
                DEBUG( "status.bState: mess\n" );
                dfu_abort( device, interface );
                break;

            case STATE_DFU_ERROR:
                DEBUG( "status.bState: STATE_DFU_ERROR\n" );
                dfu_clear_status( device, interface );
                break;

            case STATE_APP_IDLE:
                DEBUG( "appIDLE to dfuIDLE transition not supported.\n" );
                return -1;

            case STATE_APP_DETACH:
                DEBUG( "appDETACH to dfuIDLE transition not supported.\n" );
                return -1;

            case STATE_DFU_MANIFEST_WAIT_RESET:
                DEBUG( "dfuMANIFEST-WAIT-RESET to dfuIDLE transition not supported.\n" );
                return -1;
        }

        retries--;
    }

    DEBUG( "Not able to transition the device into the dfuIDLE state.\n" );
    return -2;
}


/*
 *  Used to convert the DFU state to a string.
 *
 *  state - the state to convert
 *
 *  returns the state name or "unknown state"
 */
char* dfu_state_to_string( const int state )
{
    char *message = "unknown state";

    switch( state ) {
        case STATE_APP_IDLE:
            message = "appIDLE";
            break;
        case STATE_APP_DETACH:
            message = "appDETACH";
            break;
        case STATE_DFU_IDLE:
            message = "dfuIDLE";
            break;
        case STATE_DFU_DOWNLOAD_SYNC:
            message = "dfuDNLOAD-SYNC";
            break;
        case STATE_DFU_DOWNLOAD_BUSY:
            message = "dfuDNBUSY";
            break;
        case STATE_DFU_DOWNLOAD_IDLE:
            message = "dfuDNLOAD-IDLE";
            break;
        case STATE_DFU_MANIFEST_SYNC:
            message = "dfuMANIFEST-SYNC";
            break;
        case STATE_DFU_MANIFEST:
            message = "dfuMANIFEST";
            break;
        case STATE_DFU_MANIFEST_WAIT_RESET:
            message = "dfuMANIFEST-WAIT-RESET";
            break;
        case STATE_DFU_UPLOAD_IDLE:
            message = "dfuUPLOAD-IDLE";
            break;
        case STATE_DFU_ERROR:
            message = "dfuERROR";
            break;
    }

    return message;
}


/*
 *  Used to convert the DFU status to a string.
 *
 *  status - the status to convert
 *
 *  returns the status name or "unknown status"
 */
char* dfu_status_to_string( const int status )
{
    char *message = "unknown status";

    switch( status ) {
        case DFU_STATUS_OK:
            message = "OK";
            break;
        case DFU_STATUS_ERROR_TARGET:
            message = "errTARGET";
            break;
        case DFU_STATUS_ERROR_FILE:
            message = "errFILE";
            break;
        case DFU_STATUS_ERROR_WRITE:
            message = "errWRITE";
            break;
        case DFU_STATUS_ERROR_ERASE:
            message = "errERASE";
            break;
        case DFU_STATUS_ERROR_CHECK_ERASED:
            message = "errCHECK_ERASED";
            break;
        case DFU_STATUS_ERROR_PROG:
            message = "errPROG";
            break;
        case DFU_STATUS_ERROR_VERIFY:
            message = "errVERIFY";
            break;
        case DFU_STATUS_ERROR_ADDRESS:
            message = "errADDRESS";
            break;
        case DFU_STATUS_ERROR_NOTDONE:
            message = "errNOTDONE";
            break;
        case DFU_STATUS_ERROR_FIRMWARE:
            message = "errFIRMWARE";
            break;
        case DFU_STATUS_ERROR_VENDOR:
            message = "errVENDOR";
            break;
        case DFU_STATUS_ERROR_USBR:
            message = "errUSBR";
            break;
        case DFU_STATUS_ERROR_POR:
            message = "errPOR";
            break;
        case DFU_STATUS_ERROR_UNKNOWN:
            message = "errUNKNOWN";
            break;
        case DFU_STATUS_ERROR_STALLEDPKT:
            message = "errSTALLEDPKT";
            break;

    }

    return message;
}


/*
 *  Used to output the response from our USB request in a human reable
 *  form.
 *
 *  function - the calling function to output on behalf of
 *  result   - the result to interpret
 */
static void dfu_msg_response_output( const char *function, const int result )
{
    char *msg = NULL;

    if( 0 <= result ) {
        msg = "No error.";
    } else {
        switch( result ) {
            case -ENOENT:
                msg = "-ENOENT: URB was canceled by unlink_urb";
                break;
            case -EINPROGRESS:
                msg = "-EINPROGRESS: URB still pending, no results yet "
                      "(actually no error until now)";
                break;
            case -EPROTO:
                msg = "-EPROTO: a) Bitstuff error or b) Unknown USB error";
                break;
            case -EILSEQ:
                msg = "-EILSEQ: CRC mismatch";
                break;
            case -EPIPE:
                msg = "-EPIPE: a) Babble detect or b) Endpoint stalled";
                break;
            case -ETIMEDOUT:
                msg = "-ETIMEDOUT: Transfer timed out, NAK";
                break;
            case -ENODEV:
                msg = "-ENODEV: Device was removed";
                break;
            case -EREMOTEIO:
                msg = "-EREMOTEIO: Short packet detected";
                break;
            case -EXDEV:
                msg = "-EXDEV: ISO transfer only partially completed look at "
                      "individual frame status for details";
                break;
            case -EINVAL:
                msg = "-EINVAL: ISO madness, if this happens: Log off and go home";
                break;
            default:
                msg = "Unknown error";
                break;
        }

        DEBUG( "%s 0x%08x (%d)\n", msg, result, result );
    }
}
