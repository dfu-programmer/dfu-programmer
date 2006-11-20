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
#include <string.h>
#include <stddef.h>
#include <errno.h>
#include <usb.h>

#include "config.h"
#include "arguments.h"
#include "dfu.h"
#include "atmel.h"
#include "util.h"


/*
 * Atmel's firmware doesn't export a DFU descriptor in its config
 * descriptor, so we have to guess about parameters listed there.
 * We use 3KB for wTransferSize (MAX_TRANSFER_SIZE).
 */

#define ATMEL_MAX_TRANSFER_SIZE     0x0c00
#define ATMEL_MAX_FLASH_BUFFER_SIZE (ATMEL_MAX_TRANSFER_SIZE + 0x30)

#define ATMEL_DEBUG_THRESHOLD   50

#define DEBUG(...)  dfu_debug( __FILE__, __FUNCTION__, __LINE__, \
                               ATMEL_DEBUG_THRESHOLD, __VA_ARGS__ )

/* returns 0 - 255 on success, < 0 otherwise */
static int atmel_read_command( struct usb_dev_handle *device,
                               const int interface,
                               const char data0,
                               const char data1 )
{
    char command[3] = { 0x05, 0x00, 0x00 };
    char data[1]    = { 0x00 };
    struct dfu_status status;

    command[1] = data0;
    command[2] = data1;

    if( 0 != dfu_make_idle(device, interface) ) {
        DEBUG( "dfu_make_idle failed\n" );
        return -1;
    }

    if( 3 != dfu_download(device, interface, 3, command) ) {
        DEBUG( "dfu_download failed\n" );
        return -2;
    }

    if( 0 != dfu_get_status(device, interface, &status) ) {
        DEBUG( "dfu_get_status failed\n" );
        return -3;
    }

    if( DFU_STATUS_OK != status.bStatus ) {
        DEBUG( "status(%s) was not OK.\n",
               dfu_state_to_string(status.bStatus) );
        return -4;
    }

    if( 1 != dfu_upload(device, interface, 1, data) ) {
        DEBUG( "dfu_upload failed\n" );
        return -5;
    }

    return (0xff & data[0]);
}


/*
 *  This reads in all of the configuration and Manufacturer Information
 *  into the atmel_device_info data structure for easier use later.
 *
 *  device    - the usb_dev_handle to communicate with
 *  interface - the interface to communicate with
 *  info      - the data structure to populate
 *
 *  returns 0 if successful, < 0 if not
 */
int atmel_read_config_8051( struct usb_dev_handle *device,
                            const int interface,
                            struct atmel_device_info *info )
{
    int result;
    int retVal = 0;
    int i = 0;
    short *ptr = (short *) info;

    /* These commands are documented in Appendix A of the
     * "AT89C5131A USB Bootloader Datasheet"
     */
    static const char data[24] = { 0x00, 0x00,
                                   0x00, 0x01,
                                   0x00, 0x02,
             
                                   0x01, 0x00,
                                   0x01, 0x01,
                                   0x01, 0x05,
                                   0x01, 0x06,
                                   0x01, 0x30,
                                   0x01, 0x31,
                                   0x01, 0x60,
                                   0x01, 0x61,

                                   0x02, 0x00 };

    for( i = 0; i < 24; i += 2 ) {
        result = atmel_read_command( device, interface, data[i], data[i+1] );
        if( result < 0 ) {
            retVal = result;
        }
        *ptr = result;
        ptr++;
    }

    return retVal;
}


/*
 *  This reads in all of the configuration and Manufacturer Information
 *  into the atmel_device_info data structure for easier use later.
 *
 *  device    - the usb_dev_handle to communicate with
 *  interface - the interface to communicate with
 *  info      - the data structure to populate
 *
 *  returns 0 if successful, < 0 if not
 */
int atmel_read_config_avr( struct usb_dev_handle *device,
                           const int interface,
                           struct atmel_device_info *info )
{
    int result;
    int retVal = 0;
    int i = 0;

    /* These commands are documented in Appendix A of the
     * "AT90usb128x/AT90usb64x USB DFU Bootloader Datasheet"
     */
    static const struct config_info {
        unsigned char   data0, data1;
        unsigned        offset;
    } data[] = {
        { 0x0, 0x0,  offsetof(struct atmel_device_info, bootloaderVersion ), },
        { 0x0, 0x1,  offsetof(struct atmel_device_info, bootID1 ),           },
        { 0x0, 0x2,  offsetof(struct atmel_device_info, bootID2 ),           },
        { 0x1, 0x30, offsetof(struct atmel_device_info, manufacturerCode ),  },
        { 0x1, 0x31, offsetof(struct atmel_device_info, familyCode ),        },
        { 0x1, 0x60, offsetof(struct atmel_device_info, productName ),       },
        { 0x1, 0x61, offsetof(struct atmel_device_info, productRevision ),   },
    };

    for( i = 0; i < 7; i++ ) {
        short *ptr = data[i].offset + (void *) info;
        result = atmel_read_command( device, interface, data[i].data0, data[i].data1 );
        if( result < 0 ) {
            retVal = result;
        }
        *ptr = result;
        ptr++;
    }

    return retVal;
}


/*
 *
 *  device    - the usb_dev_handle to communicate with
 *  interface - the interface to communicate with
 *  mode      - the mode to use when erasing flash
 *              ATMEL_ERASE_BLOCK_0
 *              ATMEL_ERASE_BLOCK_1
 *              ATMEL_ERASE_BLOCK_2
 *              ATMEL_ERASE_BLOCK_3
 *              ATMEL_ERASE_ALL
 *
 *  returns status DFU_STATUS_OK if ok, anything else on error
 */
int atmel_erase_flash( struct usb_dev_handle *device,
                       const int interface,
                       const unsigned char mode )
{
    char command[3] = { 0x04, 0x00, 0x00 };
    struct dfu_status status;
    int i;

    switch( mode ) {
        case ATMEL_ERASE_BLOCK_0:
            command[2] = 0x00;
            break;
        case ATMEL_ERASE_BLOCK_1:
            command[2] = 0x20;
            break;
        case ATMEL_ERASE_BLOCK_2:
            command[2] = 0x40;
            break;
        case ATMEL_ERASE_BLOCK_3:
            command[2] = 0x80;
            break;
        case ATMEL_ERASE_ALL:
            command[2] = 0xff;
            break;

        default:
            return -1;
    }

    if( 3 != dfu_download(device, interface, 3, command) ) {
        DEBUG( "dfu_download failed\n" );
        return -2;
    }

    /* It looks like it can take a while to erase the chip.
     * We will try for 10 seconds before giving up.
     */
    for( i = 0; i < 10; i++ ) {
        if( 0 == dfu_get_status(device, interface, &status) ) {
            return status.bStatus;
        }
    }

    return -3;
}


int atmel_set_config( struct usb_dev_handle *device,
                      const int interface,
                      const unsigned char property,
                      const unsigned char value )
{
    char command[4] = { 0x04, 0x01, 0x00, 0x00 };
    struct dfu_status status;

    switch( property ) {
        case ATMEL_SET_CONFIG_BSB:
            break;
        case ATMEL_SET_CONFIG_SBV:
            command[2] = 0x01;
            break;
        case ATMEL_SET_CONFIG_SSB:
            command[2] = 0x05;
            break;
        case ATMEL_SET_CONFIG_EB:
            command[2] = 0x06;
            break;
        case ATMEL_SET_CONFIG_HSB:
            command[1] = 0x02;
            break;
        default:
            return -1;
    }

    command[3] = value;

    if( 0 != dfu_make_idle(device, interface) ) {
        DEBUG( "dfu_make_idle failed\n" );
        return -2;
    }


    if( 4 != dfu_download(device, interface, 4, command) ) {
        DEBUG( "dfu_download failed\n" );
        return -3;
    }

    if( 0 != dfu_get_status(device, interface, &status) ) {
        DEBUG( "dfu_get_status failed\n" );
        return -4;
    }

    if( DFU_STATUS_ERROR_WRITE == status.bStatus ) {
        fprintf( stderr, "Device is write protected.\n" );
    }

    return status.bStatus;
}


/* Just to be safe, let's limit the transfer size */
int atmel_read_flash( struct usb_dev_handle *device,
                      const int interface,
                      const u_int32_t start,
                      const u_int32_t end,
                      char* buffer,
                      const int buffer_len )
{
    char command[6] = { 0x03, 0x00, 0x00, 0x00, 0x00, 0x00 };
    char *ptr = buffer;
    int length = end - start + 1; /* + 1 because memory is 0 based */
    int result;
    int rxStart, rxEnd, rxLength;

    if( (NULL == buffer) || (start >= end) ) {
        DEBUG( "invalid arguments.\n" );
        return -1;
    }

    if( length > buffer_len ) {
        DEBUG( "buffer isn't large enough - bytes needed: %d.\n", length );
        return -2;
    }

    rxStart = start;

    DEBUG( "read %d bytes\n", length );

    if( 0 != dfu_make_idle(device, interface) ) {
        DEBUG( "dfu_make_idle failed\n" );
        return -3;
    }

    while( length > 0 ) {

        rxLength = length;
        if( length > ATMEL_MAX_TRANSFER_SIZE ) {
            rxLength = ATMEL_MAX_TRANSFER_SIZE;
        }

        /* Why do we subtract 1, then add it back?
         * Because the transfer is inclusive of the end address,
         * we want to be 1 less, but when the time comes to
         * say where to start reading next time we need to start
         * with the next byte... since this is inclusive.
         *
         * Example: 0x0000 -> 0x0001 actually transmits 2 bytes:
         * 0x0000 and 0x0001.
         */
        rxEnd = rxStart + rxLength - 1;

        command[2] = 0xff & (rxStart >> 8);
        command[3] = 0xff & rxStart;
        command[4] = 0xff & (rxEnd >> 8);
        command[5] = 0xff & rxEnd;

        rxEnd++;

        DEBUG( "%d bytes to %p, from MCU %06x\n", rxLength, ptr, rxStart );

        if( 6 != dfu_download(device, interface, 6, command) ) {
            DEBUG( "dfu_download failed\n" );
            return -4;
        }

        result = dfu_upload( device, interface, rxLength, ptr );

        if( result > 0 ) {
            rxStart = rxEnd;
            length -= result;
            ptr += result;
        } else {
            struct dfu_status status;

            DEBUG( "result: %d\n", result );
            if( 0 == dfu_get_status(device, interface, &status) ) {
                if( DFU_STATUS_ERROR_FILE == status.bStatus ) {
                    fprintf( stderr,
                             "The device is read protected.\n" );
                } else {
                    fprintf( stderr, "Unknown error.  Try enabling debug.\n" );
                }
            } else {
                fprintf( stderr, "Device is unresponsive.\n" );
            }

            return result;
        }
    }

    return (end - start + 1);
}


int atmel_blank_check( struct usb_dev_handle *device,
                      const int interface,
                      const u_int32_t start,
                      const u_int32_t end )
{
    char command[6] = { 0x03, 0x01, 0x00, 0x00, 0x00, 0x00 };
    struct dfu_status status;
    int i;

    if( start >= end ) {
        DEBUG( "invalid arguments.\n" );
        return -1;
    }

    command[2] = 0xff & (start >> 8);
    command[3] = 0xff & start;
    command[4] = 0xff & (end >> 8);
    command[5] = 0xff & end;

    if( 0 != dfu_make_idle(device, interface) ) {
        DEBUG( "dfu_make_idle failed\n" );
        return -2;
    }

    if( 6 != dfu_download(device, interface, 6, command) ) {
        DEBUG( "dfu_download failed.\n" );
        return -3;
    }

    /* It looks like it can take a while to erase the chip.
     * We will try for 10 seconds before giving up.
     */
    for( i = 0; i < 10; i++ ) {
        if( 0 == dfu_get_status(device, interface, &status) ) {
            return status.bStatus;
        }
    }

    DEBUG( "erase chip failed.\n" );
    return -4;
}


/* Not really sure how to test this one. */
int atmel_reset( struct usb_dev_handle *device,
                 const int interface )
{
    char command[3] = { 0x04, 0x03, 0x00 };

    if( 3 != dfu_download(device, interface, 3, command) ) {
        DEBUG( "dfu_download failed.\n" );
        return -1;
    }

    /*
    if( 0 != dfu_download(device, interface, 0, NULL) ) {
        return -2;
    }
    */

    return 0;
}


int atmel_start_app( struct usb_dev_handle *device,
                     const int interface )
{
    char command[5] = { 0x04, 0x03, 0x01, 0x00, 0x00 };

    if( 5 != dfu_download(device, interface, 5, command) ) {
        DEBUG( "dfu_download failed.\n" );
        return -1;
    }

    if( 0 != dfu_download(device, interface, 0, NULL) ) {
        DEBUG( "dfu_download failed.\n" );
        return -2;
    }

    return 0;
}


int atmel_flash( struct usb_dev_handle *device,
                 const int interface,
                 const u_int32_t start,
                 const u_int32_t end,
                 char* buffer )
{
    char data[ATMEL_MAX_FLASH_BUFFER_SIZE];
    int txStart = 0;
    int txEnd = 0;
    int data_length = 0;
    int message_length = 0;
    int length = end - start;
    struct dfu_status status;
    int result;

    if( (NULL == buffer) || (start >= end) ) {
        DEBUG( "invalid arguments.\n" );
        return -1;
    }

    /* Initialize the buffer */
    memset( data, 0, ATMEL_MAX_FLASH_BUFFER_SIZE );
    data[0] = 0x01;

    txStart = start;

    DEBUG( "write %d bytes\n", length );

    if( 0 != dfu_make_idle(device, interface) ) {
        DEBUG( "dfu_make_idle failed\n" );
        return -2;
    }

    while( length > 0 ) {

        data_length = length;
        if( length > ATMEL_MAX_TRANSFER_SIZE ) {
            data_length = ATMEL_MAX_TRANSFER_SIZE;
        }

        message_length = data_length + 0x30;

        /* Why do we subtract 1, then add it back?
         * Because the transfer is inclusive of the end address,
         * we want to be 1 less, but when the time comes to
         * say where to start reading next time we need to start
         * with the next byte... since this is inclusive.
         *
         * Example: 0x0000 -> 0x0001 actually transmits 2 bytes:
         * 0x0000 and 0x0001.
         */
        txEnd = txStart + data_length - 1;

        /* Set up the headers */
        data[2] = 0xff & (txStart >> 8);
        data[3] = 0xff & txStart;
        data[4] = 0xff & (txEnd >> 8);
        data[5] = 0xff & txEnd;

        txEnd++;

        DEBUG( "%d bytes to MCU %06x, from %p\n",
               data_length, txStart, buffer + txStart );

        /* Copy the data */
        memcpy( &(data[0x20]), &(buffer[txStart]), (size_t) data_length );

        /* Set up the footers */
        /* I guess it doesn't care about the footers... */

        result = dfu_download( device, interface, message_length, data );

        if( message_length != result ) {
            if( -EPIPE == result ) {
                /* The control pipe stalled - this is an error
                 * caused by the device saying "you can't do that"
                 * which means the device is write protected.
                 */
                fprintf( stderr, "Device is write protected.\n" );

                dfu_clear_status( device, interface );
            } else {
                DEBUG( "dfu_download failed. %d\n", result );
            }
            return -3;
        }

        /* check status */
        if( 0 != dfu_get_status(device, interface, &status) ) {
            DEBUG( "dfu_get_status failed.\n" );
            return -4;
        }

        if( DFU_STATUS_OK != status.bStatus ) {
            DEBUG( "status(%s) was not OK.\n",
                   dfu_state_to_string(status.bStatus) );
            return -5;
        }

        length -= data_length;
        txStart = txEnd;
    }

    return (end - start + 1);
}


void atmel_print_device_info( FILE *stream, struct atmel_device_info *info )
{
    fprintf( stream, "%18s: 0x%04x - %d\n", "Bootloader Version", info->bootloaderVersion, info->bootloaderVersion );
    fprintf( stream, "%18s: 0x%04x - %d\n", "Device boot ID 1", info->bootID1, info->bootID1 );
    fprintf( stream, "%18s: 0x%04x - %d\n", "Device boot ID 2", info->bootID2, info->bootID2 );

    if( /* device is 8051 based */ 0 ) {
        fprintf( stream, "%18s: 0x%04x - %d\n", "Device BSB", info->bsb, info->bsb );
        fprintf( stream, "%18s: 0x%04x - %d\n", "Device SBV", info->sbv, info->sbv );
        fprintf( stream, "%18s: 0x%04x - %d\n", "Device SSB", info->ssb, info->ssb );
        fprintf( stream, "%18s: 0x%04x - %d\n", "Device EB", info->eb, info->eb );
    }

    fprintf( stream, "%18s: 0x%04x - %d\n", "Manufacturer Code", info->manufacturerCode, info->manufacturerCode );
    fprintf( stream, "%18s: 0x%04x - %d\n", "Family Code", info->familyCode, info->familyCode );
    fprintf( stream, "%18s: 0x%04x - %d\n", "Product Name", info->productName, info->productName );
    fprintf( stream, "%18s: 0x%04x - %d\n", "Product Revision", info->productRevision, info->productRevision );
    fprintf( stream, "%18s: 0x%04x - %d\n", "HWB", info->hsb, info->hsb );
}
