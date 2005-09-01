#ifndef __DFU_H__
#define __DFU_H__

#include <usb.h>

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

struct dfu_status {
    unsigned char bStatus;
    unsigned int  bwPollTimeout;
    unsigned char bState;
    unsigned char iString;
};

void dfu_init( const int timeout );
void dfu_debug( const int level );
int dfu_detach( struct usb_dev_handle *device,
                const unsigned short interface,
                const unsigned short timeout );
int dfu_download( struct usb_dev_handle *device,
                  const unsigned short interface,
                  const unsigned short length,
                  char* data );
int dfu_upload( struct usb_dev_handle *device,
                const unsigned short interface,
                const unsigned short length,
                char* data );
int dfu_get_status( struct usb_dev_handle *device,
                    const unsigned short interface,
                    struct dfu_status *status );
int dfu_clear_status( struct usb_dev_handle *device,
                      const unsigned short interface );
int dfu_get_state( struct usb_dev_handle *device,
                   const unsigned short interface );
int dfu_abort( struct usb_dev_handle *device,
               const unsigned short interface );

char* dfu_state_to_string( int state );
#endif
