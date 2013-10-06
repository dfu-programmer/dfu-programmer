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

enum atmel_memory_unit_enum { mem_flash, mem_eeprom, mem_security, mem_config,
    mem_boot, mem_sig, mem_user, mem_ram, mem_ext0, mem_ext1, mem_ext2,
    mem_ext3, mem_ext, mem_ext5, mem_ext6, mem_ext7, mem_extdf };

#define ATMEL_MEM_UNIT_NAMES "flash", "eeprom", "security", "config", \
    "bootloader", "signature", "user", "int_ram", "ext_cs0", "ext_cs1", \
    "ext_cs2", "ext_cs3", "ext_cs4", "ext_cs5", "ext_cs6", "ext_cs7", "ext_df"

// ________  P R O T O T Y P E S  _______________________________
static int32_t atmel_read_command( dfu_device_t *device,
                                   const uint8_t data0,
                                   const uint8_t data1 );
/* returns 0 - 255 on success, < 0 otherwise */

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

static int32_t __atmel_read_page( dfu_device_t *device,
                                  const uint32_t start,
                                  const uint32_t end,
                                  uint8_t* buffer,
                                  const dfu_bool eeprom );

static int32_t __atmel_blank_page_check( dfu_device_t *device,
                                         const uint32_t start,
                                         const uint32_t end );
/* use to check if a certain address range on the current page is blank
 * it assumes current page has previously been selected.
 * returns 0 if the page is blank
 * returns the first non-blank address + 1 if not blank (no zero!)
 * returns a negative number if the blank check fails
 */

// ________  F U N C T I O N S  _______________________________
static int32_t atmel_read_command( dfu_device_t *device,
                                   const uint8_t data0,
                                   const uint8_t data1 ) {
    if( NULL == device ) {
        DEBUG( "invalid arguments.\n" );
        return -1;
    }

    if( GRP_AVR32 & device->type ) {
        //We need to talk to configuration memory.  It comes
        //in two varieties in this chip.  data0 is the command to
        //select it. Data1 is the byte of that group we want

        uint8_t command[4] = { 0x06, 0x03, 0x00, data0 };

        if( 4 != dfu_download(device, 4, command) ) {
            DEBUG( "dfu_download failed.\n" );
            return -1;
        }

        int32_t result;
        uint8_t buffer[1];
        result = __atmel_read_page( device, data1, data1+1, buffer, false );
        if( 1 != result ) {
            return -5;
        }

        return (0xff & buffer[0]);

    } else {
        uint8_t command[3] = { 0x05, 0x00, 0x00 };
        uint8_t data[1]    = { 0x00 };
        dfu_status_t status;

        command[1] = data0;
        command[2] = data1;

        TRACE( "%s( %p, 0x%02x, 0x%02x )\n", __FUNCTION__, device, data0, data1 );

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
}

int32_t atmel_read_fuses( dfu_device_t *device,
                           atmel_avr32_fuses_t *info ) {
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

    int32_t result;
    uint8_t buffer[32];
    int i;
    result = __atmel_read_page( device, 0, 32, buffer, false );
    if( 32 != result ) {
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

static int32_t __atmel_read_page( dfu_device_t *device,
                                  const uint32_t start,
                                  const uint32_t end,
                                  uint8_t* buffer,
                                  const dfu_bool eeprom ) {
    uint8_t command[6] = { 0x03, 0x00, 0x00, 0x00, 0x00, 0x00 };
    uint32_t current_start;
    size_t size;
    uint32_t mini_page;
    int32_t result;

    TRACE( "%s( %p, %u, %u, %p, %s )\n", __FUNCTION__, device, start, end,
           buffer, ((true == eeprom) ? "true" : "false") );

    // AVR/8051 requires 0x02 here to read eeprom, AVR32/XMEGA requires 0x00.
    if( true == eeprom && (GRP_AVR & device->type) ) {
        command[1] = 0x02;
    }

    current_start = start;
    size = end - current_start;
    // Just to be safe, let's limit the transfer size
    for( mini_page = 0; 0 < size; mini_page++ ) {
        if( ATMEL_MAX_TRANSFER_SIZE < size ) {
            size = ATMEL_MAX_TRANSFER_SIZE;
        }
        command[2] = 0xff & (current_start >> 8);
        command[3] = 0xff & current_start;
        command[4] = 0xff & ((current_start + size - 1)>> 8);
        command[5] = 0xff & (current_start + size - 1);

        if( 6 != dfu_download(device, 6, command) ) {
            DEBUG( "dfu_download failed\n" );
            return -1;
        }

        result = dfu_upload( device, size, buffer );
        if( result < 0) {
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

        buffer += size;
        current_start += size;

        if( current_start < end ) {
            size = end - current_start;
        } else {
            size = 0;
        }
    }

    return (end - start);
}

int32_t atmel_read_flash( dfu_device_t *device,
                          const uint32_t start,
                          const uint32_t end,
                          uint8_t* buffer,
                          const size_t buffer_len,
                          const dfu_bool eeprom,
                          const dfu_bool user ) {
    uint16_t page = 0;
    uint32_t current_start;
    size_t size;

    TRACE( "%s( %p, 0x%08x, 0x%08x, %p, %u, %s )\n", __FUNCTION__, device,
           start, end, buffer, buffer_len, ((true == eeprom) ? "true" : "false") );

    if( (NULL == buffer) || (start >= end) || (NULL == device) ) {
        DEBUG( "invalid arguments.\n" );
        return -1;
    }

    if( (end - start) > buffer_len ) {
        DEBUG( "buffer (%d bytes) isn't large enough, %d bytes needed.\n",
                buffer_len, (end - start));
        return -2;
    }

    /* For the AVR32/XMEGA chips, select the flash space. */
    if( GRP_AVR32 & device->type ) {
        if( user == true ) {
            if( 0 != atmel_select_memory_unit(device, mem_user) ) {
                return -3;
            }
        } else {
            if( 0 != atmel_select_memory_unit(device, mem_flash) ) {
                return -3;
            }
        }
    }

    current_start = start;
    if( end > ATMEL_64KB_PAGE ) {
        size = ATMEL_64KB_PAGE - start;
    } else {
        size = end;
    }
    for( page = 0; 0 < size; page++ ) {
        int32_t result;
        if( size > ATMEL_64KB_PAGE ) {
            size = ATMEL_64KB_PAGE;
        }
        if( user == false ) {
            if( 0 != atmel_select_page(device, page) ) {
                return -4;
            }
        }

        result = __atmel_read_page( device, current_start, (current_start + size), buffer, eeprom );
        if( size != result ) {
            return -5;
        }

        /* Move the buffer forward. */
        buffer += size;

        current_start += size;

        if( current_start < end ) {
            size = end - current_start;
        } else {
            size = 0;
        }
    }

    return (end - start);
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

static void atmel_flash_prepair_buffer( int16_t *buffer, const size_t size,
                                        const size_t page_size ) {
    int16_t *page;

    TRACE( "%s( %p, %u, %u )\n", __FUNCTION__, buffer, size, page_size );

    for( page = buffer;
         &page[page_size] < &buffer[size];
         page = &page[page_size] )
    {
        int32_t i;

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
                if( (page[i] < 0) || (UINT8_MAX < page[i]) ) {
                    /* Invalid memory value. */
                    page[i] = 0;
                }
            }
        }
    }
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

    dfu_clear_status( device );
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
    result = __atmel_read_page( device, 0, 1, buffer, false );
    if( 1 != result ) {
        return -2;
    }

    return( (0 == buffer[0]) ? ATMEL_SECURE_OFF : ATMEL_SECURE_ON );
}

int32_t atmel_flash( dfu_device_t *device,
                     int16_t *buffer,
                     const uint32_t start,
                     const uint32_t end,
                     const size_t page_size,
                     const dfu_bool eeprom ) {
    uint32_t first = 0;
    int32_t sent = 0;       // total bytes that have been sent
    uint8_t mem_page = 0;   // tracks the current memory page
    int32_t result = 0;     // result storage for many function calls
    size_t size = end - start + 1;  // total size of the program

    TRACE( "%s( %p, %p, %u, %u, %u, %s )\n", __FUNCTION__, device, buffer,
           start, end, page_size, ((true == eeprom) ? "true" : "false") );

    DEBUG("Flash available from 0x%X (p. %u) to 0x%X (p. %u), 0x%X bytes.\n",
            start, start / ATMEL_64KB_PAGE,
            end, end / ATMEL_64KB_PAGE,
            end - start + 1); // bytes are inclusive so +1

    if( (NULL == device) || (NULL == buffer) || (end < start) ) {
        DEBUG( "invalid arguments.\n" );
        return -1;
    }

    if( ADC_8051 != device->type ) {
        if( GRP_AVR32 & device->type ) {
            /* Select FLASH memory */
            if( 0 != atmel_select_memory_unit(device, mem_flash) ) {
                return -2;
            }
        }

        /* Select Page 0 */
        mem_page = start / ATMEL_64KB_PAGE;
        result = atmel_select_page( device, mem_page );
        if( result < 0 ) {
            DEBUG( "error selecting the page: %d\n", result );
            return -3;
        }
    } else {
        atmel_flash_prepair_buffer( &buffer[start], size, page_size );
    }

    first = start;

    // loop through all data segments (not all hex is continuous)
    while( 1 )
    {
        uint32_t last = 0;
        int32_t length;
        // FIXME : why are these getting re-initialized each time?

        /* Find the next valid character to start sending from */
        for( ; first <= end; first++ ) {
            if( (0 <= buffer[first]) && (buffer[first] <= UINT8_MAX) ) {
                /* We found a valid value. */
                break;
            }
        }

        /* We didn't find anything to flash. */
        if( first > end ) {
            // go directly to end of END OF PROGRAM loop
            break;
        }

        /* Find the last character in this valid block to send. */
        for( last = first; last <= end; last++ ) {
            if( (buffer[last] < 0) || (UINT8_MAX < buffer[last]) ) {
                break;
            }
        }

recheck_page:
        /* Make sure any writes align with the memory page boudary. */
        if( (ATMEL_64KB_PAGE * (1 + mem_page)) <= last ) {
            if( first < (ATMEL_64KB_PAGE * (1 + mem_page)) ) {
                last = ATMEL_64KB_PAGE * (1 + mem_page);
            } else {
                int32_t result;

                mem_page++;
                result = atmel_select_page( device, mem_page );
                if( result < 0 ) {
                    DEBUG( "error selecting the page: %d\n", result );
                    return -3;
                }
                goto recheck_page;
            }
        }

        length = last - first;

        DEBUG( "valid block length: %d, (%d - %d)\n", length, first, last );

        while( 0 < length ) {
            int32_t result;
// FIXME : should not initialize on each loop iteration.. just set
// TODO : add a blank check here before writing to make sure page is blank

            if( ATMEL_MAX_TRANSFER_SIZE < length ) {
                length = ATMEL_MAX_TRANSFER_SIZE;
            }

            result = __atmel_flash_page( device, &(buffer[first]),
                    ((ATMEL_64KB_PAGE - 1) & first),
                    ((ATMEL_64KB_PAGE - 1) & first) + length - 1, eeprom );

            if( result != 0 ) {
                DEBUG( "error flashing the block: %d\n", result );
                return -4;
            }

            first += length;
            sent += length;

            DEBUG( "Next first: %d\n", first );
            length = last - first;
            DEBUG( "valid block length: %d\n", length );
        }
        DEBUG( "sent: %d, first: %u last: %u\n", sent, first, last );
    }

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
