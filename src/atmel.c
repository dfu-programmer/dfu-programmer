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
#include <string.h>
#include <usb.h>
#include "dfu.h"
#include "atmel.h"

#define ATMEL_MAX_TRANSFER_SIZE     0x0400
#define ATMEL_MAX_FLASH_BUFFER_SIZE 0x0c30
#define ATMEL_MAX_FLASH_SIZE        0x0c00

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

    if( 3 != dfu_download(device, interface, 3, command) ) {
        fprintf( stderr, "%s: download failed.\n", __FUNCTION__ );
        return -1;
    }

    if( 6 != dfu_get_status(device, interface, &status) ) {
        fprintf( stderr, "%s: status failed.\n", __FUNCTION__ );
        return -2;
    }

    if( DFU_STATUS_OK != status.bStatus ) {
        fprintf( stderr, "%s: status(%s) was not OK.\n",
                 __FUNCTION__, dfu_state_to_string(status.bStatus) );
        return -3;
    }

    if( 1 != dfu_upload(device, interface, 1, data) ) {
        fprintf( stderr, "%s: upload failed.\n", __FUNCTION__ );
        return -4;
    }

    return (0xff & data[0]);
}


void atmel_init( int debug_level )
{
    if( debug_level >= 10 ) {
        dfu_debug( 1 );
    }

    dfu_init( 1000 );
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
int atmel_read_config( struct usb_dev_handle *device,
                       const int interface,
                       struct atmel_device_info *info )
{
    int result;
    int retVal = 0;
    int i = 0;
    char data[24] = { 0x00, 0x00,   /* See Appendix A of the AT89C51SND1         */
                      0x00, 0x01,   /* UBS Bootloader document for the meanings. */
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
    short *ptr = (short *) info;

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
            command[3] = 0x00;
            break;
        case ATMEL_ERASE_BLOCK_1:
            command[3] = 0x20;
            break;
        case ATMEL_ERASE_BLOCK_2:
            command[3] = 0x40;
            break;
        case ATMEL_ERASE_BLOCK_3:
            command[3] = 0x80;
            break;
        case ATMEL_ERASE_ALL:
            command[3] = 0xff;
            break;

        default:
            return -1;
    }

    if( 3 != dfu_download(device, interface, 3, command) ) {
        //fprintf( stderr, "%s: download failed.\n", __FUNCTION__ );
        return -2;
    }

    /* It looks like it can take a while to erase the chip.
     * We will try for 10 seconds before giving up.
     */
    for( i = 0; i < 10; i++ ) {
        if( 6 == dfu_get_status(device, interface, &status) ) {
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

    if( 4 != dfu_download(device, interface, 4, command) ) {
        //fprintf( stderr, "%s: download failed.\n", __FUNCTION__ );
        return -2;
    }

    if( 6 != dfu_get_status(device, interface, &status) ) {
        //fprintf( stderr, "%s: status failed.\n", __FUNCTION__ );
        return -3;
    }

    return status.bStatus;
}


/* Just to be safe, let's limit the transfer size */
int atmel_read_flash( struct usb_dev_handle *device,
                      const int interface,
                      const unsigned short start,
                      const unsigned short end,
                      char* buffer,
                      const int buffer_len )
{
    char command[6] = { 0x03, 0x00, 0x00, 0x00, 0x00, 0x00 };
    char *ptr = buffer;
    int length = end - start + 1; // Plus 1 because it is inclusive.
    int result;
    int rxStart, rxEnd, rxLength;

    if( (NULL == buffer) || (start >= end) ) {
        fprintf( stderr, "%s: invalid arguments.\n", __FUNCTION__ );
        return -1;
    }

    if( length > buffer_len ) {
        fprintf( stderr, "%s: the buffer isn't large enough.\n"
                         "It needs to be %d bytes or larger.\n",
                         __FUNCTION__, length );
        return -2;
    }

    rxStart = start;

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

        if( 6 != dfu_download(device, interface, 6, command) ) {
            fprintf( stderr, "%s: download failed.\n", __FUNCTION__ );
            return -3;
        }

        result = dfu_upload( device, interface, rxLength, ptr );

        if( result > 0 ) {
            rxStart = rxEnd;
            length -= result;
            ptr += result;
        } else {
            return result;
        }
    }

    return (end - start + 1);
}


int atmel_blank_check( struct usb_dev_handle *device,
                      const int interface,
                      const unsigned short start,
                      const unsigned short end )
{
    char command[6] = { 0x03, 0x01, 0x00, 0x00, 0x00, 0x00 };
    struct dfu_status status;
    int i;

    if( start >= end ) {
        fprintf( stderr, "%s: invalid arguments.\n", __FUNCTION__ );
        return -1;
    }

    command[2] = 0xff & (start >> 8);
    command[3] = 0xff & start;
    command[4] = 0xff & (end >> 8);
    command[5] = 0xff & end;

    if( 6 != dfu_download(device, interface, 6, command) ) {
        fprintf( stderr, "%s: download failed.\n", __FUNCTION__ );
        return -2;
    }

    /* It looks like it can take a while to erase the chip.
     * We will try for 10 seconds before giving up.
     */
    for( i = 0; i < 10; i++ ) {
        if( 6 == dfu_get_status(device, interface, &status) ) {
            return status.bStatus;
        }
    }

    return -3;
}


/* Not really sure how to test this one. */
int atmel_reset( struct usb_dev_handle *device,
                 const int interface )
{
    char command[3] = { 0x04, 0x03, 0x00 };

    if( 3 != dfu_download(device, interface, 3, command) ) {
        fprintf( stderr, "%s: download failed.\n", __FUNCTION__ );
        return -1;
    }

    /*
    if( 0 != dfu_download(device, interface, 0, NULL) ) {
        fprintf( stderr, "%s: download failed.\n", __FUNCTION__ );
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
        fprintf( stderr, "%s: download failed.\n", __FUNCTION__ );
        return -1;
    }

    if( 0 != dfu_download(device, interface, 0, NULL) ) {
        fprintf( stderr, "%s: download failed.\n", __FUNCTION__ );
        return -2;
    }

    return 0;
}


int atmel_flash( struct usb_dev_handle *device,
                 const int interface,
                 const unsigned short start,
                 const unsigned short end,
                 char* buffer )
{
    char data[ATMEL_MAX_FLASH_BUFFER_SIZE];
    int txStart = 0;
    int txEnd = 0;
    int data_length = 0;
    int message_length = 0;
    int length = end - start + 1;
    struct dfu_status status;

    if( (NULL == buffer) || (start >= end) ) {
        fprintf( stderr, "%s: invalid arguments.\n", __FUNCTION__ );
        return -1;
    }

    /* Initialize the buffer */
    memset( data, 0, ATMEL_MAX_FLASH_BUFFER_SIZE );
    data[0] = 0x01;

    txStart = start;

    while( length > 0 ) {

        data_length = length;
        if( length > ATMEL_MAX_FLASH_SIZE ) {
            data_length = ATMEL_MAX_FLASH_SIZE; 
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

        /* Copy the data */
        memcpy( &(data[0x20]), &(buffer[txStart]), (size_t) data_length );

        /* Set up the footers */
        /* I guess it doesn't care about the footers... */

        if( message_length != 
            dfu_download(device, interface, message_length, data) )
        {
            fprintf( stderr, "%s: download failed.\n", __FUNCTION__ );
            return -2;
        }

        /* check status */
        if( 6 != dfu_get_status(device, interface, &status) ) {
            fprintf( stderr, "%s: status failed.\n", __FUNCTION__ );
            return -3;
        }

        if( DFU_STATUS_OK != status.bStatus ) {
            fprintf( stderr, "%s: status(%s) was not OK.\n",
                     __FUNCTION__, dfu_state_to_string(status.bStatus) );
            return -4;
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
    fprintf( stream, "%18s: 0x%04x - %d\n", "Device BSB", info->bsb, info->bsb );
    fprintf( stream, "%18s: 0x%04x - %d\n", "Device SBV", info->sbv, info->sbv );
    fprintf( stream, "%18s: 0x%04x - %d\n", "Device SSB", info->ssb, info->ssb );
    fprintf( stream, "%18s: 0x%04x - %d\n", "Device EB", info->eb, info->eb );
    fprintf( stream, "%18s: 0x%04x - %d\n", "Manufacturer Code", info->manufacturerCode, info->manufacturerCode );
    fprintf( stream, "%18s: 0x%04x - %d\n", "Family Code", info->familyCode, info->familyCode );
    fprintf( stream, "%18s: 0x%04x - %d\n", "Product Name", info->productName, info->productName );
    fprintf( stream, "%18s: 0x%04x - %d\n", "Product Revision", info->productRevision, info->productRevision );
    fprintf( stream, "%18s: 0x%04x - %d\n", "HWB", info->hsb, info->hsb );
}
