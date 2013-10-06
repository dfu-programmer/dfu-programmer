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



/* Atmel's firmware doesn't export a DFU descriptor in its config
 * descriptor, so we have to guess about parameters listed there.
 * We use 3KB for wTransferSize (MAX_TRANSFER_SIZE).
 */
/* a 64kb page contains 0x10000 values (0 to 0xFFFF).  For the largest 512 kb
 * devices (2^19 bytes) there should be 8 pages.
 */
#define ATMEL_64KB_PAGE             0x10000
#define ATMEL_MAX_TRANSFER_SIZE     0x0400
#define ATMEL_MAX_FLASH_BUFFER_SIZE (ATMEL_MAX_TRANSFER_SIZE +              \
                                        ATMEL_AVR32_CONTROL_BLOCK_SIZE +    \
                                        ATMEL_AVR32_CONTROL_BLOCK_SIZE +    \
                                        ATMEL_FOOTER_SIZE)

#define ATMEL_FOOTER_SIZE               16
#define ATMEL_CONTROL_BLOCK_SIZE        32
#define ATMEL_AVR32_CONTROL_BLOCK_SIZE  64

#define ATMEL_DEBUG_THRESHOLD   50
#define ATMEL_TRACE_THRESHOLD   55

#define DEBUG(...)  dfu_debug( __FILE__, __FUNCTION__, __LINE__, \
                               ATMEL_DEBUG_THRESHOLD, __VA_ARGS__ )
#define TRACE(...)  dfu_debug( __FILE__, __FUNCTION__, __LINE__, \
                               ATMEL_TRACE_THRESHOLD, __VA_ARGS__ )

extern int debug;

// ________  P R O T O T Y P E S  _______________________________
static int32_t atmel_read_command( dfu_device_t *device,
                                   const uint8_t data0,
                                   const uint8_t data1 );
/* returns 0 - 255 on success, < 0 otherwise
 *
 * but what is it used for??
 */

static int32_t __atmel_flash_page( dfu_device_t *device,
                                  int16_t *buffer,
                                  const uint32_t start,
                                  const uint32_t end,
                                  const dfu_bool eeprom );
/* flash the contents of memory into a block of memory.  it is assumed that the
 * appropriate page has already been selected.  start and end are the start and
 * end addresses of the flash data.  returns 0 on success, positive dfu error
 * code if one is obtained, or negative if communitcation with device fails.
 */

static int32_t atmel_select_memory_unit( dfu_device_t *device,
        const uint8_t unit );
/* select a memory unit from the following list (enumerated)
 * flash, eeprom, security, configuration, bootloader, signature, user page
 */

static int32_t atmel_select_page( dfu_device_t *device,
                                  const uint16_t mem_page );
/* select a page in memory, numbering starts with 0, pages are
 * 64kb pages (0x10000 bytes)
 */

static int32_t __atmel_blank_page_check( dfu_device_t *device,
                                         const uint32_t start,
                                         const uint32_t end );
/* use to check if a certain address range on the current page is blank
 * it assumes current page has previously been selected.
 * returns 0 if the page is blank
 * returns the first non-blank address + 1 if not blank (no zero!)
 * returns a negative number if the blank check fails
 */

static int32_t atmel_flash_prep_buffer( int16_t *buffer, const size_t size,
                                        const size_t page_size );
/* prepare the buffer so that valid data fills each page that contains data.
 * unassigned data in buffer is given a value of 0xff (blank memory)
 * the buffer pointer must align with the beginning of a flash page
 * size is the number of bytes contained in the buffer
 * page_size is the size of the flash page
 * return 0 on success, -1 if assigning data would extend flash above size
 */

static int32_t __atmel_read_block( dfu_device_t *device,
                                   atmel_buffer_in_t *buin,
                                   const dfu_bool eeprom );
/* assumes block does not cross 64 b page boundaries and ideally alligs
 * with flash pages. appropriate memory type and 64kb page has already
 * been selected, max transfer size is not violated it updates the buffer
 * data between data_start and data_end
 */

// ________  F U N C T I O N S  _______________________________
static int32_t atmel_read_command( dfu_device_t *device,
                                   const uint8_t data0,
                                   const uint8_t data1 ) {
    atmel_buffer_in_t buin;
    uint8_t buffer[1];

    TRACE( "%s( %p, 0x%02x, 0x%02x )\n", __FUNCTION__, device, data0, data1 );

    // init the necessary parts of buin
    buin.data_start = data1;
    buin.data_end = data1;
    buin.data = buffer;

    if( NULL == device ) {
        DEBUG( "invalid arguments.\n" );
        return -1;
    }

    if( GRP_AVR32 & device->type ) {
        //We need to talk to configuration memory.  It comes
        //in two varieties in this chip.  data0 is the command to
        //select it. Data1 is the byte of that group we want

        if( 0 != atmel_select_memory_unit(device, mem_config) ) {
            return -3;
        }

        if( 0 != __atmel_read_block(device, &buin, false) ) {
            return -5;
        }

        return (0xff & buffer[0]);
    } else {
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
            dfu_clear_status( device );
            return -3;
        }

        if( 1 != dfu_upload(device, 1, data) ) {
            DEBUG( "dfu_upload failed\n" );
            return -4;
        }

        return (0xff & data[0]);
    }
}

int32_t atmel_validate_buffer(atmel_buffer_in_t *buin,
        atmel_buffer_out_t *bout) {
    int32_t i;

    DEBUG( "Starting program validation comparison.\n" );
    DEBUG( "Validating image from byte 0x%X to 0x%X.\n",
            bout->data_start, bout->data_end );

    for( i = bout->valid_start; i <= bout->valid_end; i++ ) {
        if(  bout->data[i] <= UINT8_MAX ) {
            // Memory should have been programmed here
            if( ((uint8_t) bout->data[i]) != buin->data[i] ) {
                if( i > bout->data_start ) {
                    DEBUG( "Image validates from: 0x%X to 0x%X.\n",
                            bout->data_start, i - 1 );
                }
                DEBUG( "Image did not validate at byte: 0x%X of 0x%X.\n",
                        i, bout->total_size );
                DEBUG( "Wanted 0x%02x but read 0x%02x.\n",
                        0xff & bout->data[i], buin->data[i] );
                return -1;
            }
        }
    }
    return 0;
}

int32_t atmel_init_buffer_out(atmel_buffer_out_t *bout,
        size_t total_size, size_t page_size ) {
    uint32_t i;
    if ( !total_size || !page_size ) {
        DEBUG("What are you thinking... size must be > 0.\n");
        return -1;
    }

    bout->total_size = total_size;
    bout->page_size = page_size;
    bout->data_start = UINT32_MAX;      // invalid data start
    bout->data_end = 0;
    bout->valid_start = 0;
    bout->valid_end = total_size - 1;
    // allocate the memory
    bout->data = (uint16_t *) malloc( total_size * sizeof(uint16_t) );
    if( NULL == bout->data ) {
        DEBUG( "ERROR allocating 0x%X bytes of memory.\n",
                total_size * sizeof(uint16_t));
        return -2;
    }

    // initialize buffer to 0xFFFF (invalid / unassigned data)
    for( i = 0; i < bout->total_size; i++ ) {
        bout->data[i] = UINT16_MAX;
    }
    return 0;
}

int32_t atmel_init_buffer_in(atmel_buffer_in_t *buin, size_t total_size ) {
    if ( !total_size ) {
        DEBUG("What are you thinking... size must be > 0.\n");
        return -1;
    }

    buin->total_size = total_size;
    buin->data_start = UINT32_MAX;      // invalid data start
    buin->data_end = UINT32_MAX;        // invalid data end
    buin->valid_start = 0;
    buin->valid_end = total_size - 1;

    buin->data = (uint8_t *) malloc( total_size );
    if( NULL == buin->data ) {
        DEBUG( "ERROR allocating 0x%X bytes of memory.\n", total_size );
        return -2;
    }

    // initialize buffer to 0xFF (blank / unassigned data)
    memset( buin->data, UINT8_MAX, total_size );

    return 0;
}

int32_t atmel_read_fuses( dfu_device_t *device,
                           atmel_avr32_fuses_t *info ) {
    atmel_buffer_in_t buin;
    uint8_t buffer[32];
    int i;

    // init the necessary parts of buin
    buin.data_start = 0;
    buin.data_end = 31;
    buin.data = buffer;

    if( NULL == device ) {
        DEBUG( "invalid arguments.\n" );
        return -1;
    }

    if( GRP_AVR & device->type ) {
        DEBUG( "target does not support fuse operation.\n" );
        fprintf( stderr, "target does not support fuse operation.\n" );
        return -1;
    }

    if( 0 != atmel_select_memory_unit(device, mem_config) ) {
        return -3;
    }

    if( 0 != __atmel_read_block(device, &buin, false) ) {
            return -5;
    }

    info->lock = 0;
    for(i = 0; i < 16; i++) {
        info->lock = info->lock | (buffer[i] << i);
    }
    info->epfl = buffer[16];
    info->bootprot = (buffer[19] << 2) | (buffer[18] << 1) | (buffer[17] << 0);
    info->bodlevel = 0;
    for(i = 20; i < 26; i++) {
        info->bodlevel = info->bodlevel | (buffer[i] << (i-20));
    }
    info->bodhyst = buffer[26];
    info->boden = (buffer[28] << 1) | (buffer[27] << 0);
    info->isp_bod_en = buffer[29];
    info->isp_io_cond_en = buffer[30];
    info->isp_force = buffer[31];

    return 0;
}

int32_t atmel_read_config( dfu_device_t *device,
                           atmel_device_info_t *info ) {
    typedef struct {
        uint8_t data0;
        uint8_t data1;
        uint8_t device_map;
        size_t  offset;
    } atmel_read_config_t;

    /* These commands are documented in Appendix A of the
     * "AT89C5131A USB Bootloader Datasheet" or
     * "AT90usb128x/AT90usb64x USB DFU Bootloader Datasheet"
     */
    static const atmel_read_config_t data[] = {
        { 0x00, 0x00, (ADC_8051 | ADC_AVR), offsetof(atmel_device_info_t, bootloaderVersion) },
        { 0x04, 0x00, (ADC_AVR32),          offsetof(atmel_device_info_t, bootloaderVersion) },
        { 0x00, 0x01, (ADC_8051 | ADC_AVR), offsetof(atmel_device_info_t, bootID1)           },
        { 0x04, 0x01, (ADC_AVR32),          offsetof(atmel_device_info_t, bootID1)           },
        { 0x00, 0x02, (ADC_8051 | ADC_AVR), offsetof(atmel_device_info_t, bootID2)           },
        { 0x04, 0x02, (ADC_AVR32),          offsetof(atmel_device_info_t, bootID2)           },
        { 0x01, 0x30, (ADC_8051 | ADC_AVR), offsetof(atmel_device_info_t, manufacturerCode)  },
        { 0x05, 0x00, (ADC_AVR32),          offsetof(atmel_device_info_t, manufacturerCode)  },
        { 0x01, 0x31, (ADC_8051 | ADC_AVR), offsetof(atmel_device_info_t, familyCode)        },
        { 0x05, 0x01, (ADC_AVR32),          offsetof(atmel_device_info_t, familyCode)        },
        { 0x01, 0x60, (ADC_8051 | ADC_AVR), offsetof(atmel_device_info_t, productName)       },
        { 0x05, 0x02, (ADC_AVR32),          offsetof(atmel_device_info_t, productName)       },
        { 0x01, 0x61, (ADC_8051 | ADC_AVR), offsetof(atmel_device_info_t, productRevision)   },
        { 0x05, 0x03, (ADC_AVR32),          offsetof(atmel_device_info_t, productRevision)   },
        { 0x01, 0x00, ADC_8051,             offsetof(atmel_device_info_t, bsb)               },
        { 0x01, 0x01, ADC_8051,             offsetof(atmel_device_info_t, sbv)               },
        { 0x01, 0x05, ADC_8051,             offsetof(atmel_device_info_t, ssb)               },
        { 0x01, 0x06, ADC_8051,             offsetof(atmel_device_info_t, eb)                },
        { 0x02, 0x00, ADC_8051,             offsetof(atmel_device_info_t, hsb)               }
    };

    int32_t result;
    int32_t retVal = 0;
    int32_t i = 0;

    TRACE( "%s( %p, %p )\n", __FUNCTION__, device, info );

    if( NULL == device ) {
        DEBUG( "invalid arguments.\n" );
        return -1;
    }

    for( i = 0; i < sizeof(data)/sizeof(atmel_read_config_t); i++ ) {
        atmel_read_config_t *row = (atmel_read_config_t*) &data[i];

        if( row->device_map & device->type )
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

int32_t atmel_erase_flash( dfu_device_t *device,
                           const uint8_t mode ) {
    uint8_t command[3] = { 0x04, 0x00, 0x00 };
    dfu_status_t status;
    int32_t i;

    TRACE( "%s( %p, %d )\n", __FUNCTION__, device, mode );

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
            DEBUG ( "CMD_ERASE status: Erase Done.\n" );
            return status.bStatus;
        } else {
            DEBUG ( "CMD_ERASE status check %d returned nonzero.\n", i );
        }
    }

    return -3;
}

int32_t atmel_set_fuse( dfu_device_t *device,
                          const uint8_t property,
                          const uint32_t value ) {
    int32_t result;
    int16_t buffer[16];
    int32_t address;
    int8_t numbytes;
    int8_t i;

    if( NULL == device ) {
        DEBUG( "invalid arguments.\n" );
        return -1;
    }

    if( GRP_AVR & device->type ) {
       DEBUG( "target does not support fuse operation.\n" );
       fprintf( stderr, "target does not support fuse operation.\n" );
       return -1;
    }

    if( 0 != atmel_select_memory_unit(device, mem_config) ) {
        return -3;
    }

    switch( property ) {
        case set_lock:
            for( i = 0; i < 16; i++ ) {
                buffer[i] = value & (0x0001 << i);
            }
            numbytes = 16;
            address = 0;
            break;
        case set_epfl:
            buffer[0] = value & 0x0001;
            numbytes = 1;
            address = 16;
            break;
        case set_bootprot:
            buffer[0] = value & 0x0001;
            buffer[1] = value & 0x0002;
            buffer[2] = value & 0x0004;
            numbytes = 3;
            address = 17;
            break;
        case set_bodlevel:
#ifdef SUPPORT_SET_BOD_FUSES
            /* Enable at your own risk - this has not been tested &
             * may brick your device. */
            for(i = 20;i < 26; i++){
                buffer[i] = value & (0x0001 << (i-20));
            }
            numbytes = 6;
            address = 20;
            break;
#else
            DEBUG( "Setting BODLEVEL can break your chip. Operation not performed\n" );
            DEBUG( "Rebuild with the SUPPORT_SET_BOD_FUSES #define enabled if you really want to do this.\n" );
            fprintf( stderr, "Setting BODLEVEL can break your chip. Operation not performed.\n" );
            return -1;
#endif
        case set_bodhyst:
#ifdef SUPPORT_SET_BOD_FUSES
            /* Enable at your own risk - this has not been tested &
             * may brick your device. */
            buffer[0] = value & 0x0001;
            numbytes = 1;
            address = 26;
            break;
#else
            DEBUG("Setting BODHYST can break your chip. Operation not performed\n");
            DEBUG( "Rebuild with the SUPPORT_SET_BOD_FUSES #define enabled if you really want to do this.\n" );
            fprintf( stderr, "Setting BODHYST can break your chip. Operation not performed.\n");
            return -1;
#endif
        case set_boden:
#ifdef SUPPORT_SET_BOD_FUSES
            /* Enable at your own risk - this has not been tested &
             * may brick your device. */
            buffer[0] = value & 0x0001;
            buffer[1] = value & 0x0002;
            numbytes = 2;
            address = 27;
            break;
#else
            DEBUG( "Setting BODEN can break your chip. Operation not performed\n" );
            DEBUG( "Rebuild with the SUPPORT_SET_BOD_FUSES #define enabled if you really want to do this.\n" );
            fprintf( stderr, "Setting BODEN can break your chip. Operation not performed.\n" );
            return -1;
#endif
        case set_isp_bod_en:
#ifdef SUPPORT_SET_BOD_FUSES
            /* Enable at your own risk - this has not been tested &
             * may brick your device. */
            buffer[0] = value & 0x0001;
            numbytes = 1;
            address = 29;
            break;
#else
            DEBUG( "Setting ISP_BOD_EN can break your chip. Operation not performed\n" );
            DEBUG( "Rebuild with the SUPPORT_SET_BOD_FUSES #define enabled if you really want to do this.\n" );
            fprintf( stderr, "Setting ISP_BOD_EN can break your chip. Operation not performed.\n" );
            return -1;
#endif
        case set_isp_io_cond_en:
            buffer[0] = value & 0x0001;
            numbytes = 1;
            address = 30;
            break;
        case set_isp_force:
            buffer[0] = value & 0x0001;
            numbytes = 1;
            address = 31;
            break;
        default:
            DEBUG( "Fuse bits unrecognized\n" );
            fprintf( stderr, "Fuse bits unrecognized.\n" );
            return -2;
            break;
    }

    result = __atmel_flash_page( device, buffer, address,
            address + numbytes - 1, false );
    if(result != 0) {
        return -6;
    }
    return 0;
}

int32_t atmel_set_config( dfu_device_t *device,
                          const uint8_t property,
                          const uint8_t value ) {
    uint8_t command[4] = { 0x04, 0x01, 0x00, 0x00 };
    dfu_status_t status;

    TRACE( "%s( %p, %d, 0x%02x )\n", __FUNCTION__, device, property, value );

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

static uint32_t __print_progress( atmel_buffer_in_t *buin, uint32_t progress ) {
    while ( ((buin->data_end - buin->valid_start + 1) * 32) > progress ) {
        fprintf( stderr, ">" );
        progress += buin->valid_end - buin->valid_start + 1;
    }
    return progress;
}

static int32_t __atmel_read_block( dfu_device_t *device,
                                   atmel_buffer_in_t *buin,
                                   const dfu_bool eeprom ) {
    uint8_t command[6] = { 0x03, 0x00, 0x00, 0x00, 0x00, 0x00 };
    int32_t result;

    if( buin->data_end < buin->data_start ) {
        // this would cause a problem bc read length could be way off
        DEBUG("ERROR: start address is after end address.\n");
        return -1;
    } else if( buin->data_end - buin->data_start + 1 > ATMEL_MAX_TRANSFER_SIZE ) {
        // this could cause a read problem
        DEBUG("ERROR: transfer size must not exceed %d.\n",
                ATMEL_MAX_TRANSFER_SIZE );
        return -1;
    }

    // AVR/8051 requires 0x02 here to read eeprom, XMEGA requires 0x00.
    if( true == eeprom && (GRP_AVR & device->type) ) {
        command[1] = 0x02;
    }

    command[2] = 0xff & (buin->data_start >> 8);
    command[3] = 0xff & (buin->data_start);
    command[4] = 0xff & (buin->data_end >> 8);
    command[5] = 0xff & (buin->data_end);

    if( 6 != dfu_download(device, 6, command) ) {
        DEBUG( "dfu_download failed\n" );
        return -1;
    }

    result = dfu_upload( device, buin->data_end - buin->data_start + 1,
                                &buin->data[buin->data_start] );
    if( result < 0) {
        dfu_status_t status;

        DEBUG( "dfu_upload result: %d\n", result );
        if( 0 == dfu_get_status(device, &status) ) {
            if( DFU_STATUS_ERROR_FILE == status.bStatus ) {
                fprintf( stderr,
                            "The device is read protected.\n" );
            } else {
                fprintf( stderr, "Unknown error. Try enabling debug.\n" );
            }
        } else {
            fprintf( stderr, "Device is unresponsive.\n" );
        }
        dfu_clear_status( device );

        return result;
    }

    return 0;
}

int32_t atmel_read_flash( dfu_device_t *device,
                          atmel_buffer_in_t *buin,
                          const uint8_t mem_segment,
                          const dfu_bool quiet ) {
    uint8_t mem_page = 0;           // tracks the current memory page
    uint32_t progress = 0;          // used to indicate progress
    int32_t result = 0;


    TRACE( "%s( %p, %p, %u, %s )\n", __FUNCTION__, device, buin,
            mem_segment, ((true == quiet) ? "true" : "false"));

    if( (NULL == buin) || (NULL == device) ) {
        DEBUG( "invalid arguments.\n" );
        return -1;
    } else if ( mem_segment != mem_flash &&
                mem_segment != mem_user &&
                mem_segment != mem_eeprom ) {
        DEBUG( "Invalid memory segment %d to read.\n", mem_segment );
        return -1;
    }

    // For the AVR32/XMEGA chips, select the flash space. (safe for all parts)
    if( 0 != atmel_select_memory_unit(device, mem_segment) ) {
        return -3;
    }

    if( !quiet && !(debug > ATMEL_DEBUG_THRESHOLD) ) {
        fprintf( stderr, "[================================]\n" );
        fprintf( stderr, "[" );
    }

    // select the first memory page (should be safe for user == true)
    buin->data_start = buin->valid_start;
    mem_page = buin->data_start / ATMEL_64KB_PAGE;
    if ( mem_segment != mem_user ) {
        if( 0 != (result = atmel_select_page( device, mem_page )) ) {
            DEBUG( "ERROR selecting 64kB page %d.\n", result );
            if ( !quiet && !(debug > ATMEL_DEBUG_THRESHOLD) )
                fprintf( stderr, " X\n.");
            return -3;
        }
    }

    while (buin->data_start <= buin->valid_end) {
        // ensure the memory page is correct
        if ( buin->data_start / ATMEL_64KB_PAGE != mem_page ) {
            mem_page = buin->data_start / ATMEL_64KB_PAGE;
            if( 0 != (result = atmel_select_page( device, mem_page )) ) {
                DEBUG( "ERROR selecting 64kB page %d.\n", result );
                if ( !quiet && !(debug > ATMEL_DEBUG_THRESHOLD) )
                    fprintf( stderr, " X\n.");
                return -3;
            }
            // check if the entire page is blank ()
//            if( 0 == __atmel_blank_page_check( device,
//                        0xFFFF & buin->data_start, 0xFFFF) ) {
//                // the entire page is blank..
//                buin->data_start += ATMEL_64KB_PAGE;
//                buin->data_end = ATMEL_64KB_PAGE * mem_page - 1;
//                if ( !quiet && !(debug > ATMEL_DEBUG_THRESHOLD)) {
//                    // display progress in 32 increments (if not hidden)
//                    progress = __print_progress( buin, progress );
//                }
//                // the buffer is initialized with 0xFF so do nothing
//                continue;
//            }
        }

        // find end value for the current transfer
        buin->data_end = buin->data_start + ATMEL_MAX_TRANSFER_SIZE - 1;
        if ( buin->data_end / ATMEL_64KB_PAGE > mem_page ) {
            buin->data_end = ATMEL_64KB_PAGE * mem_page - 1;
        }
        if ( buin->data_end > buin->valid_end ) {
            buin->data_end = buin->valid_end;
        }

        if( 0 != (result = __atmel_read_block(device, buin,
                    mem_segment == mem_eeprom ? 1 : 0)) ) {
            DEBUG( "Error reading block 0x%X to 0x%X: err %d.\n",
                    buin->data_start, buin->data_end, result );
            if ( !quiet && !(debug > ATMEL_DEBUG_THRESHOLD) )
                fprintf( stderr, " X\n.");
            return -5;
        }

        buin->data_start = buin->data_end + 1;

        if ( !quiet && !(debug > ATMEL_DEBUG_THRESHOLD)) {
            // display progress in 32 increments (if not hidden)
            progress = __print_progress( buin, progress );
        }
    }
    if ( !quiet && !(debug > ATMEL_DEBUG_THRESHOLD) )
        fprintf( stderr, "]\n" );
    return 0;
}

static int32_t __atmel_blank_page_check( dfu_device_t *device,
                                             const uint32_t start,
                                             const uint32_t end ) {
    uint8_t command[6] = { 0x03, 0x01, 0x00, 0x00, 0x00, 0x00 };
    dfu_status_t status;

    TRACE( "%s( %p, 0x%08x, 0x%08x )\n", __FUNCTION__, device, start, end );

    if( (NULL == device) ) {
        DEBUG( "ERROR: Invalid arguments, device pointer is NULL.\n" );
        return -1;
    } else if ( start > end ) {
        DEBUG( "ERROR: End address 0x%X before start address 0x%X.\n",
                end, start );
        return -1;
    } else if ( end >= ATMEL_64KB_PAGE ) {
        DEBUG( "ERROR: Address out of 64kb (0x10000) byte page range.\n",
                end );
        return -1;
    }

    command[2] = 0xff & (start >> 8);
    command[3] = 0xff & start;
    command[4] = 0xff & (end >> 8);
    command[5] = 0xff & end;

    if( 6 != dfu_download(device, 6, command) ) {
        DEBUG( "__atmel_blank_page_check DFU_DNLOAD failed.\n" );
        return -2;
    }

    if( 0 != dfu_get_status(device, &status) ) {
        DEBUG( "__atmel_blank_page_check DFU_GETSTATUS failed.\n" );
        return -3;
    }

    // check status and proceed accordingly
    if( DFU_STATUS_OK == status.bStatus ) {
        DEBUG( "Region is bank.\n" );
    } else if ( DFU_STATUS_ERROR_CHECK_ERASED == status.bStatus ) {
        // need to DFU upload to get the address
        DEBUG( "Region is NOT bank.\n" );
        uint8_t addr[2] = { 0x00, 0x00 };
        int32_t retval = 0;
        if ( 2 != dfu_upload(device, 2, addr) ) {
            DEBUG( "__atmel_blank_page_check DFU_UPLOAD failed.\n" );
            return -4;
        } else {
            retval = (int32_t) ( ( ((uint16_t) addr[0]) << 8 ) + addr[1] );
            DEBUG( " First non-blank address in region is 0x%X.\n", retval );
            return retval + 1;
        }
    } else {
        DEBUG( "Error: status (%s) was not OK.\n",
            dfu_status_to_string(status.bStatus) );
        if ( STATE_DFU_ERROR == status.bState ) {
            dfu_clear_status( device );
        }
        return -4;
    }
    return 0;
}

int32_t atmel_blank_check( dfu_device_t *device,
                           const uint32_t start,
                           const uint32_t end ) {
    int32_t result;                     // blank_page_check_result
    uint32_t blank_upto = start;        // up to is not inclusive
    uint32_t check_until;               // end address of page check
    uint16_t current_page;              // 64kb page number

    TRACE( "%s( %p, 0x%08X, 0x%08X )\n", __FUNCTION__, device, start, end );

    if( (NULL == device) ) {
        DEBUG( "ERROR: Invalid arguments, device pointer is NULL.\n" );
        return -1;
    } else if ( start > end ) {
        DEBUG( "ERROR: End address 0x%X before start address 0x%X.\n",
                end, start );
        return -1;
    }

    // safe to call this with any type of device
    if( 0 != atmel_select_memory_unit(device, mem_flash) ) {
        return -2;
    }

    do {
        // want to have checks align with pages
        current_page = blank_upto / ATMEL_64KB_PAGE;
        check_until = ( current_page + 1 ) * ATMEL_64KB_PAGE - 1;
        check_until = check_until > end ? end : check_until;

        // safe to call with any type of device (just bc end address is
        // below 0x10000 doesn't mean you are definitely on page 0)
        if ( 0 != atmel_select_page(device, current_page) ) {
            DEBUG ("page select error.\n");
            return -3;
        }

        // send the 'page' address, not absolute address
        result = __atmel_blank_page_check( device,
                blank_upto % ATMEL_64KB_PAGE,
                check_until % ATMEL_64KB_PAGE );

        if ( result == 0 ) {
            DEBUG ( "Flash blank from 0x%X to 0x%X.\n",
                    start, check_until );
            blank_upto = check_until + 1;
        } else if ( result > 0 ) {
            blank_upto = result - 1 + ATMEL_64KB_PAGE * current_page;
            DEBUG ( "Flash NOT blank beginning at 0x%X.\n", blank_upto );
            return blank_upto + 1;
        } else {
            DEBUG ( "Blank check fail err %d. Flash status unknown.\n", result );
            return result;
        }
    } while ( blank_upto < end );
    return 0;
}

int32_t atmel_start_app_reset( dfu_device_t *device ) {
    uint8_t command[3] = { 0x04, 0x03, 0x00 };

    TRACE( "%s( %p )\n", __FUNCTION__, device );

    if( 3 != dfu_download(device, 3, command) ) {
        DEBUG( "dfu_download failed.\n" );
        return -1;
    }

    if( 0 != dfu_download(device, 0, NULL) ) {
        DEBUG( "dfu_download failed.\n" );
        return -2;
    }

    return 0;
}

int32_t atmel_start_app_noreset( dfu_device_t *device ) {
    uint8_t command[5] = { 0x04, 0x03, 0x01, 0x00, 0x00 };

    TRACE( "%s( %p )\n", __FUNCTION__, device );

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

static int32_t atmel_select_memory_unit( dfu_device_t *device,
                                         const uint8_t unit ) {
    TRACE( "%s( %p, %d )\n", __FUNCTION__, device, unit );

    uint8_t command[4] = { 0x06, 0x03, 0x00, (0xFF & unit) };
    dfu_status_t status;
    char *mem_names[] = { ATMEL_MEM_UNIT_NAMES };

    // input parameter checks
    if( NULL == device) {
        DEBUG ( "ERROR: Device pointer is NULL.\n" );
        return -1;
    }

    // check compatiblity with various devices
    if( !(GRP_AVR32 & device->type) ) {
        DEBUG( "Ignore Select Memory Unit for non GRP_AVR32 device.\n" );
        return 0;
    } else if ( (ADC_AVR32 & device->type) && !( unit == mem_flash ||
                                                 unit == mem_security ||
                                                 unit == mem_config ||
                                                 unit == mem_boot ||
                                                 unit == mem_sig ||
                                                 unit == mem_user ) ) {
        DEBUG( "%d is not a valid memory unit for AVR32 devices.\n", unit );
        fprintf( stderr, "Invalid Memory Unit Selection.\n" );
        return -1;
    } else if ( unit > mem_extdf ) {
        DEBUG( "Valid Memory Units 0 to 0x%X, not 0x%X.\n", mem_extdf, unit );
        fprintf( stderr, "Invalid Memory Unit Selection.\n" );
        return -1;
    }

    // select memory unit               below is OK bc unit < len(mem_names)
    DEBUG( "Selecting %s memory unit.\n", mem_names[unit] );
    if( 4 != dfu_download(device, 4, command) ) {
        DEBUG( "atmel_select_memory_unit 0x%02X dfu_download failed.\n", unit );
        return -2;
    }

    // check that memory section was selected
    if( 0 != dfu_get_status(device, &status) ) {
        DEBUG( "DFU_GETSTATUS failed after atmel_select_memory_unit.\n" );
        return -3;
    }

    // if error, report and clear
    if( DFU_STATUS_OK != status.bStatus ) {
        DEBUG( "Error: status (%s) was not OK.\n",
            dfu_status_to_string(status.bStatus) );
        if ( STATE_DFU_ERROR == status.bState ) {
            dfu_clear_status( device );
        }
        return -4;
    }

    return 0;
}

static int32_t atmel_select_page( dfu_device_t *device,
                                  const uint16_t mem_page ) {
    TRACE( "%s( %p, %u )\n", __FUNCTION__, device, mem_page );
    dfu_status_t status;

    if( NULL == device ) {
        DEBUG ( "ERROR: Device pointer is NULL.\n" );
        return -2;
    }

    if ( ADC_8051 & device->type ) {
        DEBUG( "Select page not implemented for 8051 device, ignoring.\n" );
        return 0;
    }

    DEBUG( "Selecting page %d, address 0x%X.\n",
            mem_page, ATMEL_64KB_PAGE * mem_page );

    if( GRP_AVR32 & device->type ) {
        uint8_t command[5] = { 0x06, 0x03, 0x01, 0x00, 0x00 };
        command[3] = 0xff & (mem_page >> 8);
        command[4] = 0xff & mem_page;

        if( 5 != dfu_download(device, 5, command) ) {
            DEBUG( "atmel_select_page DFU_DNLOAD failed.\n" );
            return -1;
        }
    } else if( ADC_AVR == device->type ) {      // AVR but not 8051
        uint8_t command[4] = { 0x06, 0x03, 0x00, 0x00 };
        command[3] = 0xff & mem_page;

        if( 4 != dfu_download(device, 4, command) ) {
            DEBUG( "atmel_select_page DFU_DNLOAD failed.\n" );
            return -1;
        }
    }

    // check that page number was set
    if( 0 != dfu_get_status(device, &status) ) {
        DEBUG( "atmel_select_page DFU_GETSTATUS failed.\n" );
        return -3;
    }

    // if error, report and clear
    if( DFU_STATUS_OK != status.bStatus ) {
        DEBUG( "Error: status (%s) was not OK.\n",
            dfu_status_to_string(status.bStatus) );
        if ( STATE_DFU_ERROR == status.bState ) {
            dfu_clear_status( device );
        }
        return -4;
    }

    return 0;
}

static int32_t atmel_flash_prep_buffer( int16_t *buffer, const size_t size,
                                        const size_t page_size ) {
    int16_t *page;
    int32_t i;

    TRACE( "%s( %p, %u, %u )\n", __FUNCTION__, buffer, size, page_size );

    // increment pointer by page_size * sizeof(int16) until page_start >= end
    for( page = buffer; page < &buffer[size]; page = &page[page_size] ) {
        // check if there is valid data on this page
        for( i = 0; i < page_size; i++ ) {
            if( (0 <= page[i]) && (page[i] <= UINT8_MAX) ) {
                /* We found a valid value. */
                break;
            }
        }

        if( page_size != i ) {
            /* There was valid data in the block & we need to make
             * sure there is no unassigned data.  */
            for( i = 0; i < page_size; i++ ) {
                if ( &page[i] > &buffer[size] ) {
                    DEBUG("ERROR: Page with valid data extends beyond buffer.\n");
                    return -1;
                }
                if( (page[i] < 0) || (UINT8_MAX < page[i]) ) {
                    /* Invalid memory value. */
                    page[i] = 0xff;     // 0xff is 'unwritten'
                }
            }
        }
    }
    return 0;
}

int32_t atmel_user( dfu_device_t *device,
                    int16_t *buffer,
                    const size_t page_size ) {
    int32_t result = 0;
    TRACE( "%s( %p, %p, %u)\n", __FUNCTION__, device, buffer, page_size);

    if( (NULL == buffer) || (page_size <= 0) ) {
        DEBUG( "invalid arguments.\n" );
        return -1;
    }

    /* Select USER page */
    if( 0 != atmel_select_memory_unit(device, mem_user) ) {
        DEBUG( "User Page Memory NOT selected.\n" );
        return -2;
    } else {
        DEBUG( "User Page memory selected.\n" );
    }

    //The user block is one flash page, so we'll just do it all in a block.
    result = __atmel_flash_page( device, buffer, 0, page_size - 1, 0 );

    if( result != 0 ) {
        DEBUG( "error flashing the block: %d\n", result );
        return -4;
    }

    return 0;
}

int32_t atmel_secure( dfu_device_t *device ) {
    int32_t result = 0;
    int16_t buffer[1];
    TRACE( "%s( %p )\n", __FUNCTION__, device );

    /* Select SECURITY page */
    uint8_t command[4] = { 0x06, 0x03, 0x00, 0x02 };
    if( 4 != dfu_download(device, 4, command) ) {
        DEBUG( "dfu_download failed.\n" );
        return -2;
    }

    // The security block is a single byte, so we'll just do it all in a block.
    buffer[0] = 0x01;   // Non-zero to set security fuse.
    result = __atmel_flash_page( device, buffer, 0, 0, false );

    if( result != 0 ) {
        DEBUG( "error flashing security fuse: %d\n", result );
        return -4;
    }

    return 0;
}

int32_t atmel_getsecure( dfu_device_t *device ) {
    int32_t result = 0;
    uint8_t buffer[1];
    TRACE( "%s( %p )\n", __FUNCTION__, device );

    atmel_buffer_in_t buin;

    // init the necessary parts of buin
    buin.data_start = 0;
    buin.data_end = 0;
    buin.data = buffer;

    dfu_clear_status( device );

    // TODO : Probably should use selelect_memory_unit command here
    /* Select SECURITY page */
    uint8_t command[4] = { 0x06, 0x03, 0x00, 0x02 };
    result = dfu_download(device, 4, command);
    if( 4 != result ) {
        if( -EIO == result ) {
            /* This also happens on most access attempts
             * when the security bit is set. It may be a bug
             * in the bootloader itself.
             */
            return ATMEL_SECURE_MAYBE;
        } else {
            DEBUG( "dfu_download failed.\n" );
            return -1;
        }
    }

    // The security block is a single byte, so we'll just do it all in a block.
    if( 0 != __atmel_read_block(device, &buin, false) ) {
            return -2;
    }

    return( (0 == buffer[0]) ? ATMEL_SECURE_OFF : ATMEL_SECURE_ON );
}

int32_t atmel_flash( dfu_device_t *device,
                     int16_t *buffer,
                     const uint32_t start,
                     const uint32_t end,
                     const size_t page_size,
                     const dfu_bool eeprom,
                     dfu_bool hide_progress ) {
    uint32_t first;         // buffer location of the first valid data
    uint32_t last;          // buffer location of the last valid data
    uint32_t curaddr;       // the current address in the buffer
    uint32_t upto;          // end location for each data block
    uint32_t progress=0;    // keep record of sent progress as bytes * 32
    uint8_t mem_page = 0;   // tracks the current memory page
    int32_t result = 0;     // result storage for many function calls

    TRACE( "%s( %p, %p, %u, %u, %u, %s )\n", __FUNCTION__, device, buffer,
           start, end, page_size, ((true == eeprom) ? "true" : "false") );

    // check arguments
    if( (NULL == device) || (NULL == buffer) ) {
        DEBUG( "ERROR: Invalid arguments, device/buffer pointer is NULL.\n" );
        return -1;
    } else if ( start > end ) {
        DEBUG( "ERROR: End address 0x%X before start address 0x%X.\n",
                end, start );
        return -1;
    }

    // for each page with data, fill unassigned values on the page with 0xFF
    // buffer[0] always aligns with a flash page boundary irrespecitve of start
    result = atmel_flash_prep_buffer( buffer, end + 1, page_size );
    if( 0 != result ) {
        // error message is in atmel_flash_prep_buffer
        return -2;
    }

    // determine the limits of where actual data resides in the buffer
    first = 0x80000000;     // max memory is 0x80000 so OK until mem > 2GB
    for( curaddr = 0; curaddr < end; curaddr++ ) {
        if( (0 <= buffer[curaddr]) && (buffer[curaddr] <= 0xFF) ) {
            last = curaddr;
            if (first == 0x80000000) first = curaddr;
        }
    }

    // debug info about data limits
    DEBUG("Flash available from 0x%X to 0x%X (64kB p. %u to %u), 0x%X bytes.\n",
            start, end, start / ATMEL_64KB_PAGE, end / ATMEL_64KB_PAGE,
            end - start + 1); // bytes are inclusive so +1
    DEBUG("Data start @ 0x%X: 64kB p %u; %uB p 0x%X + 0x%X offset.\n",
            first, first / ATMEL_64KB_PAGE,
            page_size, first / page_size, first % page_size);
    DEBUG("Data end @ 0x%X: 64kB p %u; %uB p 0x%X + 0x%X offset.\n",
            last, last / ATMEL_64KB_PAGE,
            page_size, last / page_size, last % page_size);
    DEBUG("Totals: 0x%X bytes, %u %uB pages, %u 64kB byte pages.\n",
            last - first + 1,
            last / page_size + first / page_size + 1, page_size,
            last / ATMEL_64KB_PAGE - first / ATMEL_64KB_PAGE + 1 );

    if( first < start ) {
        // the bootloader region should end at a flash page boundary, this
        // error will occur if that is not the case and there is program data
        // too close to the bootloader region.
        DEBUG( "ERROR: Data exists on a page overlapping the bootloader.\n" );
        return -2;
    }

    // select eeprom/flash as the desired memory target, safe for non GRP_AVR32
    mem_page = eeprom ? mem_eeprom : mem_flash;
    if( 0 != atmel_select_memory_unit(device, mem_page) ) {
        DEBUG ("Error Selecting Memory.\n");
        return -2;
    }

    // TODO : move the text out of here.. only show hte progress bar. the text
    // should all go above, then you don't need the 'if'

    if( !hide_progress && !(debug > ATMEL_DEBUG_THRESHOLD) ) {
        fprintf( stderr, "Programming 0x%X bytes...\n", last - first + 1 );
        fprintf( stderr, "[================================]\n" );
        fprintf( stderr, "[" );
    } else if (!hide_progress) {
        fprintf( stderr, "Programming 0x%X bytes...\n", last - first + 1 );
    }

    // program the data
    curaddr = first;                // start could also be used
    mem_page = curaddr / ATMEL_64KB_PAGE;
    if( 0 != (result = atmel_select_page( device, mem_page )) ) {
        DEBUG( "ERROR selecting 64kB page %d.\n", result );
        return -3;
    }

    while (curaddr <= last) {       // end could also be used
        // select the memory page if needed (safe for non GRP_AVR32)
        if ( curaddr / ATMEL_64KB_PAGE != mem_page ) {
            mem_page = curaddr / ATMEL_64KB_PAGE;
            if( 0 != (result = atmel_select_page( device, mem_page )) ) {
                DEBUG( "ERROR selecting 64kB page %d.\n", result );
                return -3;
            }
        }

        // find end address for data section to write
        for(upto = curaddr; upto <= last; upto++) {
            // check if the current value is valid
            if( (0 > buffer[upto]) || (buffer[curaddr] > 0xff) ) break;
            // check if the current data packet is too big
            if( (upto - curaddr + 1) > ATMEL_MAX_TRANSFER_SIZE ) break;
            // check if the current data value is outside of the 64kB flash page
            if( upto / ATMEL_64KB_PAGE - mem_page ) break;
        }
        upto--; // upto was one step beyond the last data value to flash

        // write the data
        DEBUG("Program data block: 0x%X to 0x%X (p. %u), 0x%X bytes.\n",
                curaddr, upto, upto / ATMEL_64KB_PAGE, upto - curaddr + 1);
//        if ( curaddr % page_size ) {
//            DEBUG("NOTE: this would be faster if writes aligned with page boundaries.\n");
//        }
        result = __atmel_flash_page( device, &(buffer[curaddr]),
                curaddr % ATMEL_64KB_PAGE, upto % ATMEL_64KB_PAGE, eeprom);
        if( 0 != result ) {
            DEBUG( "Error flashing the block: err %d.\n", result );
            if ( !hide_progress && !(debug > ATMEL_DEBUG_THRESHOLD) )
                fprintf( stderr, " X\n.");
            return -4;
        }

        // incrment curaddr to the next valid address
        for(curaddr = upto + 1; curaddr <= last; curaddr++) {
            if( (0 <= buffer[upto]) || (buffer[curaddr] <= 0xff) ) break;
        } // curaddr is now on the first valid data for the next segment

        // display progress in 32 increments (if not hidden)
        while ( ((upto - first + 1) * 32) > progress ) {
            if ( !hide_progress && !(debug > ATMEL_DEBUG_THRESHOLD) )
                fprintf( stderr, ">" );
            progress += last - first + 1;
        }
    }
    if ( !hide_progress && !(debug > ATMEL_DEBUG_THRESHOLD) )
        fprintf( stderr, "]\n" );

    return 0;
}

static void atmel_flash_populate_footer( uint8_t *message, uint8_t *footer,
                                         const uint16_t vendorId,
                                         const uint16_t productId,
                                         const uint16_t bcdFirmware ) {
    int32_t crc;

    TRACE( "%s( %p, %p, %u, %u, %u )\n", __FUNCTION__, message, footer,
           vendorId, productId, bcdFirmware );

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
                                         const uint32_t start,
                                         const uint32_t end,
                                         const dfu_bool eeprom ) {

    TRACE( "%s( %p, 0x%X, 0x%X, %s )\n", __FUNCTION__, header, start,
           end, ((true == eeprom) ? "true" : "false") );

    if( NULL == header ) {
        return;
    }

    /* Command Identifier */
    header[0] = 0x01;   /* ld_prog_start */

    /* data[0] */
    header[1] = ((true == eeprom) ? 0x01 : 0x00);

    /* start_address */
    header[2] = 0xff & (start >> 8);
    header[3] = 0xff & start;

    /* end_address */
    header[4] = 0xff & (end >> 8);
    header[5] = 0xff & end;
}

static int32_t __atmel_flash_page( dfu_device_t *device,
                                  int16_t *buffer,
                                  const uint32_t start,
                                  const uint32_t end,
                                  const dfu_bool eeprom ) {
    // from doc7618, AT90 / ATmega app note protocol:
    const size_t length = end - start + 1;
    uint8_t message[ATMEL_MAX_FLASH_BUFFER_SIZE];
    uint8_t *header;
    uint8_t *data;
    uint8_t *footer;
    size_t message_length;
    int32_t result;
    dfu_status_t status;
    int32_t i;
    size_t control_block_size;  /* USB control block size */
    size_t alignment;

    TRACE( "%s( %p, %p, %u, %u, %s )\n", __FUNCTION__, device, buffer,
           start, end, ((true == eeprom) ? "true" : "false") );

    // check input args
    if( (NULL == device) || (NULL == buffer) ) {
        DEBUG( "ERROR: Invalid arguments, device/buffer pointer is NULL.\n" );
        return -1;
    } else if ( start > end ) {
        DEBUG( "ERROR: End address 0x%X before start address 0x%X.\n",
                end, start );
        return -1;
    } else if ( end >= ATMEL_64KB_PAGE ) {
        DEBUG( "ERROR: Address out of 64kb (0x10000) byte page range.\n",
                end );
        return -1;
    } else if ( length > ATMEL_MAX_TRANSFER_SIZE ) {
        DEBUG( "ERROR: 0x%X byte message > MAX TRANSFER SIZE (0x%X).\n",
                length, ATMEL_MAX_TRANSFER_SIZE );
        return -1;
    }

    // 0 out the message
    memset( message, 0, ATMEL_MAX_FLASH_BUFFER_SIZE );

    if( GRP_AVR32 & device->type ) {
        control_block_size = ATMEL_AVR32_CONTROL_BLOCK_SIZE;
        alignment = start % ATMEL_AVR32_CONTROL_BLOCK_SIZE;
    } else {
        control_block_size = ATMEL_CONTROL_BLOCK_SIZE;
        alignment = 0;
    }

    header = &message[0];
    data   = &message[control_block_size + alignment];
    footer = &data[length];

    atmel_flash_populate_header( header, start, end, eeprom );

    DEBUG( "0x%X bytes to MCU at 0x%06X\n", length, start );

    // Copy the data
    for( i = 0; i < end - start + 1; i++ ) {
        data[i] = (uint8_t) buffer[i];
    }

    atmel_flash_populate_footer( message, footer, 0xffff, 0xffff, 0xffff );

    message_length = ((size_t) (footer - header)) + ATMEL_FOOTER_SIZE;
    DEBUG( "Message length: %d bytes.\n", message_length );

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
            DEBUG( "atmel_flash: flash data dfu_download failed.\n" );
            DEBUG( "Expected message length of %d, got %d.\n",
                    message_length, result );
        }
        return -2;
    }

    // check status
    if( 0 != dfu_get_status(device, &status) ) {
        DEBUG( "dfu_get_status failed.\n" );
        return -3;
    }

    if( DFU_STATUS_OK == status.bStatus ) {
        DEBUG( "Page write success.\n" );
    } else {
        DEBUG( "Page write not unsuccessful (err %s).\n",
               dfu_status_to_string(status.bStatus) );
        if ( STATE_DFU_ERROR == status.bState ) {
            dfu_clear_status( device );
        }
        return (int32_t) status.bStatus;
    }
    return 0;
}

void atmel_print_device_info( FILE *stream, atmel_device_info_t *info ) {
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
