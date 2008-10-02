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

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>

#include "dfu-bool.h"
#include "dfu-device.h"
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

static int32_t atmel_flash_block( dfu_device_t *device,
                                  int16_t *buffer,
                                  const uint32_t base_address,
                                  const size_t length,
                                  const dfu_bool eeprom );
static int32_t atmel_select_page( dfu_device_t *device,
                                  const uint8_t mem_page );

/* returns 0 - 255 on success, < 0 otherwise */
static int32_t atmel_read_command( dfu_device_t *device,
                                   const uint8_t data0,
                                   const uint8_t data1 )
{
    uint8_t command[3] = { 0x05, 0x00, 0x00 };
    uint8_t data[1]    = { 0x00 };
    dfu_status_t status;

    command[1] = data0;
    command[2] = data1;

    if( 3 != dfu_download(device, 3, command) ) {
        DEBUG( "dfu_download failed\n" );
        return -1;
    }

    if( 0 != dfu_get_status(device, &status) ) {
        DEBUG( "dfu_get_status failed\n" );
        return -2;
    }

    if( DFU_STATUS_OK != status.bStatus ) {
        DEBUG( "status(%s) was not OK.\n",
               dfu_status_to_string(status.bStatus) );
        return -3;
    }

    if( 1 != dfu_upload(device, 1, data) ) {
        DEBUG( "dfu_upload failed\n" );
        return -4;
    }

    return (0xff & data[0]);
}


/*
 *  This reads in all of the configuration and Manufacturer Information
 *  into the atmel_device_info data structure for easier use later.
 *
 *  device    - the usb_dev_handle to communicate with
 *  info      - the data structure to populate
 *
 *  returns 0 if successful, < 0 if not
 */
int32_t atmel_read_config( dfu_device_t *device,
                           atmel_device_info_t *info )
{
    typedef struct {
        uint8_t data0;
        uint8_t data1;
        uint8_t device_map;
        size_t  offset;
    } atmel_read_config_t;
#   define DM_8051  0x01
#   define DM_AVR   0x02

    /* These commands are documented in Appendix A of the
     * "AT89C5131A USB Bootloader Datasheet" or
     * "AT90usb128x/AT90usb64x USB DFU Bootloader Datasheet"
     */
    static const atmel_read_config_t data[] = {
        { 0x00, 0x00, (DM_8051 | DM_AVR), offsetof(atmel_device_info_t, bootloaderVersion) },
        { 0x00, 0x01, (DM_8051 | DM_AVR), offsetof(atmel_device_info_t, bootID1)           },
        { 0x00, 0x02, (DM_8051 | DM_AVR), offsetof(atmel_device_info_t, bootID2)           },
        { 0x01, 0x30, (DM_8051 | DM_AVR), offsetof(atmel_device_info_t, manufacturerCode)  },
        { 0x01, 0x31, (DM_8051 | DM_AVR), offsetof(atmel_device_info_t, familyCode)        },
        { 0x01, 0x60, (DM_8051 | DM_AVR), offsetof(atmel_device_info_t, productName)       },
        { 0x01, 0x61, (DM_8051 | DM_AVR), offsetof(atmel_device_info_t, productRevision)   },
        { 0x01, 0x00, DM_8051,            offsetof(atmel_device_info_t, bsb)               },
        { 0x01, 0x01, DM_8051,            offsetof(atmel_device_info_t, sbv)               },
        { 0x01, 0x05, DM_8051,            offsetof(atmel_device_info_t, ssb)               },
        { 0x01, 0x06, DM_8051,            offsetof(atmel_device_info_t, eb)                },
        { 0x02, 0x00, DM_8051,            offsetof(atmel_device_info_t, hsb)               }
    };

    int32_t result;
    int32_t retVal = 0;
    int32_t i = 0;

    for( i = 0; i < sizeof(data)/sizeof(atmel_read_config_t); i++ ) {
        atmel_read_config_t *row = (atmel_read_config_t*) &data[i];

        if( ((DM_8051 & row->device_map) && (adc_8051 == device->type)) ||
            ((DM_AVR & row->device_map) && (adc_AVR == device->type)) )
        {
            int16_t *ptr = row->offset + (void *) info;

            result = atmel_read_command( device, row->data0, row->data1 );
            if( result < 0 ) {
                retVal = result;
            }
            *ptr = result;
        }
    }

    return retVal;
}


/*
 *
 *  device    - the usb_dev_handle to communicate with
 *  mode      - the mode to use when erasing flash
 *              ATMEL_ERASE_BLOCK_0
 *              ATMEL_ERASE_BLOCK_1
 *              ATMEL_ERASE_BLOCK_2
 *              ATMEL_ERASE_BLOCK_3
 *              ATMEL_ERASE_ALL
 *
 *  returns status DFU_STATUS_OK if ok, anything else on error
 */
int32_t atmel_erase_flash( dfu_device_t *device,
                           const uint8_t mode )
{
    uint8_t command[3] = { 0x04, 0x00, 0x00 };
    dfu_status_t status;
    int32_t i;

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

    if( 3 != dfu_download(device, 3, command) ) {
        DEBUG( "dfu_download failed\n" );
        return -2;
    }

    /* It looks like it can take a while to erase the chip.
     * We will try for 10 seconds before giving up.
     */
    for( i = 0; i < 10; i++ ) {
        if( 0 == dfu_get_status(device, &status) ) {
            return status.bStatus;
        }
    }

    return -3;
}


int32_t atmel_set_config( dfu_device_t *device,
                          const uint8_t property,
                          const uint8_t value )
{
    uint8_t command[4] = { 0x04, 0x01, 0x00, 0x00 };
    dfu_status_t status;

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

    if( 4 != dfu_download(device, 4, command) ) {
        DEBUG( "dfu_download failed\n" );
        return -2;
    }

    if( 0 != dfu_get_status(device, &status) ) {
        DEBUG( "dfu_get_status failed\n" );
        return -3;
    }

    if( DFU_STATUS_ERROR_WRITE == status.bStatus ) {
        fprintf( stderr, "Device is write protected.\n" );
    }

    return status.bStatus;
}


/* Just to be safe, let's limit the transfer size */
int32_t atmel_read_flash( dfu_device_t *device,
                          const uint32_t start,
                          const uint32_t end,
                          uint8_t* buffer,
                          const size_t buffer_len,
                          const dfu_bool eeprom )
{
    uint8_t command[6] = { 0x03, 0x00, 0x00, 0x00, 0x00, 0x00 };
    uint8_t *ptr = buffer;
    int32_t length = end - start + 1; /* + 1 because memory is 0 based */
    int32_t result;
    int32_t rxStart, rxEnd, rxLength;
    uint8_t mem_page = 0;

    if( (NULL == buffer) || (start >= end) ) {
        DEBUG( "invalid arguments.\n" );
        return -1;
    }

    if( length > buffer_len ) {
        DEBUG( "buffer isn't large enough - bytes needed: %d : %d.\n", length, buffer_len );
        return -2;
    }

    if( true == eeprom ) {
        command[1] = 0x02;
    }

    rxStart = start;

    DEBUG( "read %d bytes\n", length );

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

        /* Make sure any reads align with the memory page boudary. */
        if( rxEnd > (UINT16_MAX * (1 + mem_page)) ) {
            if( rxStart <= (UINT16_MAX * (1 + mem_page)) ) {
                rxEnd = UINT16_MAX * (1 + mem_page);
                rxLength = rxEnd - rxStart + 1;
            } else {
                mem_page++;

                result = atmel_select_page( device, mem_page );
                if( result < 0) {
                    DEBUG( "error selecting the page: %d\n", result );
                    return -3;
                }
            }
        }

        command[2] = 0xff & (rxStart >> 8);
        command[3] = 0xff & rxStart;
        command[4] = 0xff & (rxEnd >> 8);
        command[5] = 0xff & rxEnd;

        rxEnd++;

        DEBUG( "%d bytes to %p, from MCU %06x\n", rxLength, ptr, rxStart );

        if( 6 != dfu_download(device, 6, command) ) {
            DEBUG( "dfu_download failed\n" );
            return -4;
        }

        result = dfu_upload( device, rxLength, ptr );

        if( result > 0 ) {
            rxStart = rxEnd;
            length -= result;
            ptr += result;
        } else {
            dfu_status_t status;

            DEBUG( "result: %d\n", result );
            if( 0 == dfu_get_status(device, &status) ) {
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

    if( mem_page > 0 ) {
        result = atmel_select_page( device, 0 );
        if( result < 0) {
            DEBUG( "error selecting the page: %d\n", result );
            return -5;
        }
    }

    return (end - start + 1);
}


int32_t atmel_blank_check( dfu_device_t *device,
                           const uint32_t start,
                           const uint32_t end )
{
    uint8_t command[6] = { 0x03, 0x01, 0x00, 0x00, 0x00, 0x00 };
    dfu_status_t status;
    int32_t i;

    if( start >= end ) {
        DEBUG( "invalid arguments.\n" );
        return -1;
    }

    command[2] = 0xff & (start >> 8);
    command[3] = 0xff & start;
    command[4] = 0xff & (end >> 8);
    command[5] = 0xff & end;

    if( 6 != dfu_download(device, 6, command) ) {
        DEBUG( "dfu_download failed.\n" );
        return -2;
    }

    /* It looks like it can take a while to erase the chip.
     * We will try for 10 seconds before giving up.
     */
    for( i = 0; i < 10; i++ ) {
        if( 0 == dfu_get_status(device, &status) ) {
            return status.bStatus;
        }
    }

    DEBUG( "erase chip failed.\n" );
    return -3;
}


/* Not really sure how to test this one. */
int32_t atmel_reset( dfu_device_t *device )
{
    uint8_t command[3] = { 0x04, 0x03, 0x00 };

    if( 3 != dfu_download(device, 3, command) ) {
        DEBUG( "dfu_download failed.\n" );
        return -1;
    }

    /*
    if( 0 != dfu_download(device, 0, NULL) ) {
        return -2;
    }
    */

    return 0;
}


int32_t atmel_start_app( dfu_device_t *device )
{
    uint8_t command[5] = { 0x04, 0x03, 0x01, 0x00, 0x00 };

    if( 5 != dfu_download(device, 5, command) ) {
        DEBUG( "dfu_download failed.\n" );
        return -1;
    }

    if( 0 != dfu_download(device, 0, NULL) ) {
        DEBUG( "dfu_download failed.\n" );
        return -2;
    }

    return 0;
}


static int32_t atmel_select_page( dfu_device_t *device,
                                  const uint8_t mem_page )
{
    uint8_t command[4] = { 0x06, 0x03, 0x00, 0x00 };

    command[3] = (char) mem_page;

    if( 4 != dfu_download(device, 4, command) ) {
        DEBUG( "dfu_download failed.\n" );
        return -1;
    }

    return 0;
}


static void atmel_flash_prepair_buffer( int16_t *buffer, const size_t size,
                                        const size_t page_size )
{
    int16_t *page;

    for( page = buffer;
         &page[page_size] < &buffer[size];
         page = &page[page_size] )
    {
        int32_t i;

        for( i = 0; i < page_size; i++ ) {
            if( (0 <= page[i]) && (page[i] < UINT8_MAX) ) {
                /* We found a valid value. */
                break;
            }
        }

        if( page_size != i ) {
            /* There was valid data in the block & we need to make
             * sure there is no unassigned data.  */
            for( i = 0; i < page_size; i++ ) {
                if( (page[i] < 0) || (UINT8_MAX < page[i]) ) {
                    /* Invalid memory value. */
                    page[i] = 0;
                }
            }
        }
    }
}

int32_t atmel_flash( dfu_device_t *device,
                     int16_t *buffer,
                     const size_t size,
                     const size_t page_size,
                     const dfu_bool eeprom )
{
    uint32_t start = 0;
    int32_t sent = 0;
    uint8_t mem_page = 0;

    if( (NULL == buffer) || (size == 0) ) {
        DEBUG( "invalid arguments.\n" );
        return -1;
    }

    atmel_flash_prepair_buffer( buffer, size, page_size );

    while( 1 ) {
        uint32_t end;
        int32_t length;

        /* Find the next valid character to start sending from */
        for( ; start < size; start++ ) {
            if( (0 <= buffer[start]) && (buffer[start] < UINT8_MAX) ) {
                /* We found a valid value. */
                break;
            }
        }

        if( start == size ) {
            goto done;
        }

        /* Find the last character in this valid block to send. */
        for( end = start; end < size; end++ ) {
            if( (buffer[end] < 0) || (UINT8_MAX < buffer[end]) ) {
                break;
            }
        }

        /* Make sure any writes align with the memory page boudary. */
        if( end > (UINT16_MAX * (1 + mem_page)) ) {
            if( start <= (UINT16_MAX * (1 + mem_page)) ) {
                end = UINT16_MAX + 1;
            } else {
                int32_t result;

                mem_page++;
                result = atmel_select_page( device, mem_page );
                if( result < 0 ) {
                    DEBUG( "error selecting the page: %d\n", result );
                    return -3;
                }
            }
        }

        length = end - start;
        DEBUG( "valid block length: %d, (%d - %d)\n", length, start, end );

        do {
            int32_t result;

            if( ATMEL_MAX_TRANSFER_SIZE < length ) {
                length = ATMEL_MAX_TRANSFER_SIZE;
            }

            result = atmel_flash_block( device, &(buffer[start]),
                                        (UINT16_MAX & start), length, eeprom );

            if( result < 0 ) {
                DEBUG( "error flashing the block: %d\n", result );
                return -4;
            }

            start += result;
            sent += result;

            DEBUG( "Next start: %d\n", start );
            length = end - start;
            DEBUG( "valid block length: %d\n", length );
        } while( 0 < length );
        DEBUG( "sent %d\n", sent );
    }

done:
    if( mem_page > 0 ) {
        int32_t result = atmel_select_page( device, 0 );
        if( result < 0) {
            DEBUG( "error selecting the page: %d\n", result );
            return -5;
        }
    }

    return sent;
}


static void atmel_flash_populate_footer( uint8_t *message, uint8_t *footer,
                                         const uint16_t vendorId,
                                         const uint16_t productId,
                                         const uint16_t bcdFirmware )
{
    int32_t crc;

    if( (NULL == message) || (NULL == footer) ) {
        return;
    }

    /* TODO: Calculate the message CRC */
    crc = 0;

    /* CRC 4 bytes */
    footer[0] = 0xff & (crc >> 24);
    footer[1] = 0xff & (crc >> 16);
    footer[2] = 0xff & (crc >> 8);
    footer[3] = 0xff & crc;

    /* Length of DFU suffix - always 16. */
    footer[4] = 16;

    /* ucdfuSignature - fixed 'DFU'. */
    footer[5] = 'D';
    footer[6] = 'F';
    footer[7] = 'U';

    /* BCD DFU specification number (1.1)*/
    footer[8] = 0x01;
    footer[9] = 0x10;

    /* Vendor ID or 0xFFFF */
    footer[10] = 0xff & (vendorId >> 8);
    footer[11] = 0xff & vendorId;

    /* Product ID or 0xFFFF */
    footer[12] = 0xff & (productId >> 8);
    footer[13] = 0xff & productId;

    /* BCD Firmware release number or 0xFFFF */
    footer[14] = 0xff & (bcdFirmware >> 8);
    footer[15] = 0xff & bcdFirmware;
}

static void atmel_flash_populate_header( uint8_t *header,
                                         const uint32_t start_address,
                                         const size_t length, const dfu_bool eeprom )
{
    uint16_t end;

    if( NULL == header ) {
        return;
    }

    /* If we send 1 byte @ 0x0000, the end address will also be 0x0000 */
    end = start_address + (length - 1);

    /* Command Identifier */
    header[0] = 0x01;   /* ld_prog_start */

    /* data[0] */
    header[1] = ((true == eeprom) ? 0x01 : 0x00);

    /* start_address */
    header[2] = 0xff & (start_address >> 8);
    header[3] = 0xff & start_address;

    /* end_address */
    header[4] = 0xff & (end >> 8);
    header[5] = 0xff & end;
}

static int32_t atmel_flash_block( dfu_device_t *device,
                                  int16_t *buffer,
                                  const uint32_t base_address,
                                  const size_t length,
                                  const dfu_bool eeprom )
                              
{
    uint8_t message[ATMEL_MAX_FLASH_BUFFER_SIZE];
    uint8_t *header;
    uint8_t *data;
    uint8_t *footer;
    size_t message_length;
    int32_t result;
    dfu_status_t status;
    int32_t i;

    if( (NULL == buffer) || (ATMEL_MAX_TRANSFER_SIZE < length) ) {
        DEBUG( "invalid arguments.\n" );
        return -1;
    }

    /* 0 out the message. */
    memset( message, 0, ATMEL_MAX_FLASH_BUFFER_SIZE );

    message_length = length + 0x30;

    DEBUG( "message length: %d\n", message_length );

    header = &message[0];
    data   = &message[0x20];
    footer = &message[0x20 + length];

    atmel_flash_populate_header( header, base_address, length, eeprom );

    DEBUG( "%d bytes to MCU %06x\n", length, base_address );

    /* Copy the data */
    for( i = 0; i < length; i++ ) {
        data[i] = (uint8_t) buffer[i];
    }

    atmel_flash_populate_footer( message, footer, 0xffff, 0xffff, 0xffff );

    result = dfu_download( device, message_length, message );

    if( message_length != result ) {
        if( -EPIPE == result ) {
            /* The control pipe stalled - this is an error
             * caused by the device saying "you can't do that"
             * which means the device is write protected.
             */
            fprintf( stderr, "Device is write protected.\n" );

            dfu_clear_status( device );
        } else {
            DEBUG( "dfu_download failed. %d\n", result );
        }
        return -2;
    }

    /* check status */
    if( 0 != dfu_get_status(device, &status) ) {
        DEBUG( "dfu_get_status failed.\n" );
        return -3;
    }

    if( DFU_STATUS_OK != status.bStatus ) {
        DEBUG( "status(%s) was not OK.\n",
               dfu_status_to_string(status.bStatus) );
        return -4;
    }

    return (int32_t) length;
}


void atmel_print_device_info( FILE *stream, atmel_device_info_t *info )
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
