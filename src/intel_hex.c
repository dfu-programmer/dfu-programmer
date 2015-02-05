/*
 * dfu-programmer
 *
 * intel_hex.c
 *
 * This reads in a .hex file (Intel format), creates an array representing
 * memory, populates the array with the data from the .hex file, and
 * returns the array.
 *
 * This implementation is based completely on San Bergmans description
 * of this file format, last updated on 23 August, 2005.
 *
 * http://www.sbprojects.com
 * In the "Knowledge Base" section.
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

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "intel_hex.h"
#include "util.h"

struct intel_record {
    uint8_t count;      // single byte count
    uint8_t type;       // single byte type
    uint16_t address;   // two byte address
    uint8_t checksum;   // single byte checksum
    uint8_t data[256];
};

#define IHEX_COLS 16
#define IHEX_64KB_PAGE 0x10000


#define IHEX_DEBUG_THRESHOLD    50
#define IHEX_TRACE_THRESHOLD    55

#define DEBUG(...)  dfu_debug( __FILE__, __FUNCTION__, __LINE__, \
                               IHEX_DEBUG_THRESHOLD, __VA_ARGS__ )
#define TRACE(...)  dfu_debug( __FILE__, __FUNCTION__, __LINE__, \
                               IHEX_TRACE_THRESHOLD, __VA_ARGS__ )

// ________  P R O T O T Y P E S  _______________________________
static int intel_validate_checksum( struct intel_record *record );
/*  This walks over the record and ensures that the checksum is
 *  correct for the record.
 *
 *  returns 0 if checksum validates, anything else on error
 */

static int32_t ihex_make_line( struct intel_record *record, char *str );
/* provide record typea list of values, they are converted into a line and put at str
 * values are 2 address bytes, the record type and any values. A checksum
 * is added to the end and the line length is prepended.
 */

static int32_t ihex_make_checksum( struct intel_record *record );
/* make a checksum using the values in record, place it in the record and
 * return 0
 */

static int32_t ihex_make_record_04_offset( uint32_t offset, char *str );
/* make an 04 record type offset, where the desired offset address is found by
 * multiplying value by 0x1000 (or the 64kb page size).  Therefore, the given
 * offset value must be a clean multiple of 0x10000.  the offset value
 * is placed in the buffer at str
 */

//static int32_t ihex_process_record( struct intel_record *record,
//        const uint8_t job, uint32_t next_address );
/* call this function when a record needs to be processed (specify w/ job)
 * and a new blank record needs to be created
 */

static void ihex_clear_record( struct intel_record *record, uint32_t address );
/* wipes out a record (resets it to zero)
 */


// ________  F U N C T I O N S  _______________________________
static int intel_validate_checksum( struct intel_record *record ) {
    int i = 0;
    int checksum = 0;

    checksum = record->count + record->type + record->checksum +
                        (record->address >> 8) + (0xff & record->address);

    for( i = 0; i < record->count; i++ ) {
        checksum += record->data[i];
    }

    return (0xff & checksum);
}

static int intel_validate_line( struct intel_record *record ) {
    /* Validate the checksum */
    if( 0 != intel_validate_checksum(record) ) {
        DEBUG( "Checksum error.\n" );
        return -1;
    }

    /* Validate the type */
    switch( record->type ) {
        /* Intel 1 format, for up to 64K length (types 0, 1) */
        case 0:                             /* data record */
            /* Nothing to do. */
            break;

        case 1:                             /* EOF record */
            if( 0 != record->count ) {
                DEBUG( "EOF record error.\n" );
                return -2;
            }
            break;

        /* Intel 2 format, when 20 bit addresses are needed (types 2, 3, 4) */
        case 2:                             /* extended address record */
            if(    (0 != record->address)
                || (2 != record->count)
                || (record->data[1] != (0xf8 & record->data[1])) )
            {
                DEBUG( "Intel2 Format Error.\n" );
                return -3;
            }
            break;

        case 3:                             /* start address record */
            /* just ignore these records (could verify addr == 0) */
            break;

        case 4:                             /* extended linear address record */
            if( (0 != record->address) || (2 != record->count) ) {
                DEBUG( "Extended Linear Address Record Format Error" );
                return -4;
            }
            break;

        case 5:                             /* start linear address record */
            if( (0 != record->address) || (4 != record->count) ) {
                return -6;
            }
            break;

        default:
            fprintf( stderr, "Unsupported type. %d\n", record->type );
            /* Type 5 and other types are unsupported. */
            return -5;
    }

    return 0;
}

static int intel_read_data( FILE *fp, struct intel_record *record ) {
    int i;
    int c;
    int status;
    char buffer[10];
    int addr_upper;
    int addr_lower;
    int count;
    int type;
    int data;
    int checksum;

    /* read in the ':bbaaaarr'
     *   bb - byte count
     * aaaa - the address in memory
     *   rr - record type
     */

    if( NULL == fgets(buffer, 10, fp) )                     return -1;
    status = sscanf( buffer, ":%02x%02x%02x%02x", &count,
                     &addr_upper, &addr_lower, &type );
    if( 4 != status )                                       return -2;

    record->count =   (uint8_t)  count;
    record->address = (uint16_t) (addr_upper << 8 | addr_lower);
    record->type =    (uint8_t)  type;

    /* Read the data */
    for( i = 0; i < record->count; i++ ) {
        if( NULL == fgets(buffer, 3, fp) )                  return -3;
        if( 1 != sscanf(buffer, "%02x", &data) )            return -4;

        record->data[i] = (uint8_t) (0xff & data);
    }

    /* Read the checksum */
    if( NULL == fgets(buffer, 3, fp) )                      return -5;
    if( 1 != sscanf(buffer, "%02x", &checksum) )            return -6;

    /* Chomp the [\r]\n */
    c = fgetc( fp );
    if( '\r' == c ) {
        c = fgetc( fp );
    }
    if( '\n' != c ) {
        DEBUG( "Error: end of line != \\n.\n" );            return -7;
    }
    record->checksum = (uint8_t) checksum;

    return 0;
}

static void intel_invalid_addr_warning(uint32_t line_count, uint32_t address,
        uint32_t target_offset, size_t total_size) {
    DEBUG("Valid address region from 0x%X to 0x%X.\n",
        target_offset, target_offset + total_size - 1);
    fprintf( stderr,
        "WARNING (line %u): 0x%02x address outside valid region,\n",
        line_count, address);
    fprintf( stderr,
            " suppressing additional address error messages.\n" );
}

int32_t intel_process_data( intel_buffer_out_t *bout, char value,
        uint32_t target_offset, uint32_t address) {
    /* NOTE : there are some hex program files that contain data in the user
     * page, which is outside of 'valid' memory.  In this situation, the hex
     * file is processed and used as normal with a warning message containing
     * the first line with an invalid address.
     */
    uint32_t raddress;   // relative address = address - target offset

    // The Atmel flash page starts at address 0x8000 0000, we need to ignore
    //             stm32 flash page starts at 0x0800 0000
    // that bit
    target_offset &= 0x7fffffff;
    address &= 0x7fffffff;

    if( (address < target_offset) ||
        (address > target_offset + bout->info.total_size - 1) ) {
        DEBUG( "Address 0x%X is outside valid range 0x%X to 0x%X.\n",
                address, target_offset,
                target_offset + bout->info.total_size - 1 );
        return -1;
    } else {
        raddress = address - target_offset;
        // address >= target_offset so unsigned '-' is OK
        bout->data[raddress] = (uint16_t) (0xff & value);
        // update data limits
        if( raddress < bout->info.data_start ) {
            bout->info.data_start = raddress;
        }
        if( raddress > bout->info.data_end ) {
            bout->info.data_end = raddress;
        }
    }
    return 0;
}

int32_t intel_hex_to_buffer( char *filename, intel_buffer_out_t *bout,
                             uint32_t target_offset, dfu_bool quiet ) {
    FILE *fp = NULL;
    struct intel_record record;
    // unsigned int count, type, checksum, address; char data[256]
    uint32_t address = 0;       // line address for intel_hex
    uint32_t address_offset = 0;// offset address from intel_hex
    uint32_t line_count = 1;                // used for debugging hex file
    int32_t  invalid_address_count = 0;     // used for error checking
    int32_t retval;             // return value
    int i = 0;

    if ( (0 >= bout->info.total_size) ) {
        DEBUG( "Must provide valid memory size in bout.\n" );
        retval = -1;
        goto error;
    }

    if (NULL == filename) {
        if( !quiet ) fprintf( stderr, "Invalid filename.\n" );
        retval = -2;
        goto error;
    }

    if( 0 == strcmp("STDIN", filename) ) {
        fp = stdin;
    } else {
        fp = fopen( filename, "r" );
        if( NULL == fp ) {
            if( !quiet ) fprintf( stderr, "Error opening %s\n", filename );
            retval = -3;
            goto error;
        }
    }

    // iterate through ihex file and assign values to memory and user
    do {
        // read the data
        if( 0 != intel_read_data(fp, &record) ) {
            if( !quiet )
                fprintf( stderr, "Error reading line %u.\n", line_count );
            retval = -4;
            goto error;
        } else if ( 0 != intel_validate_line( &record ) ) {
            if( !quiet )
                fprintf( stderr, "Error: Line %u does not validate.\n", line_count );
            retval = -5;
            goto error;
        } else
            line_count++;

        // process the data
        switch( record.type ) {
            case 0:
                for( address = address_offset + ((uint32_t) record.address),
                        i = 0; i < record.count; i++, address++ ) {
                    if ( 0 != intel_process_data(bout, record.data[i],
                                target_offset, address) ) {
                        // address was invalid
                        if ( !invalid_address_count ) {
                            intel_invalid_addr_warning(line_count, address,
                                    target_offset, bout->info.total_size );
                        }
                        invalid_address_count++;
                    }
                }
                break;
            case 2:             // 0x1238 -> 0x00012380
                address_offset = (((uint32_t) record.data[0]) << 12) |
                                  ((uint32_t) record.data[1]) << 4;
                address_offset = (0x7fffffff & address_offset);
                DEBUG( "Address offset set to 0x%x.\n", address_offset );
                break;
            case 4:             // 0x1234 -> 0x12340000
                address_offset = (((uint32_t) record.data[0]) << 24) |
                                  ((uint32_t) record.data[1]) << 16;
                address_offset = (0x7fffffff & address_offset);
                DEBUG( "Address offset set to 0x%x.\n", address_offset );
                break;
            case 5:             // 0x12345678 -> 0x12345678
                address_offset = (((uint32_t) record.data[0]) << 24) |
                                 (((uint32_t) record.data[1]) << 16) |
                                 (((uint32_t) record.data[2]) <<  8) |
                                  ((uint32_t) record.data[3]);
                /* Note: In AVR32 memory map, FLASH starts at 0x80000000, but
                 * the ISP places this memory at 0. The hex file will use
                 * 0x8..., so mask off that bit. */
                address_offset = (0x7fffffff & address_offset);
                DEBUG( "Address offset set to 0x%x.\n", address_offset );
                break;
        }
    } while( (1 != record.type) );

    if ( invalid_address_count ) {
        if( !quiet )
            fprintf( stderr, "Total of 0x%X bytes in invalid addressed.\n",
                    invalid_address_count );
    }

    retval = invalid_address_count;

error:
    if( NULL != fp ) {
        fclose( fp );
        fp = NULL;
    }

    if( retval & !quiet ) {
        fprintf( stderr, "See --debug=%u or greater for more information.\n",
                IHEX_DEBUG_THRESHOLD + 1 );
    }

    return retval;
}

// ___ CONVERT TO INTEL HEX __________________________
static void ihex_clear_record( struct intel_record *record, uint32_t address ) {
    record->count = 0;
    record->address = ((uint16_t) (address % 0xffff));
    record->type = 0;
    record->data[0] = 0;
    record->checksum = 0;
}

static int32_t ihex_make_checksum( struct intel_record *record ) {
    uint16_t sum = 0;
    uint8_t i;
    sum += record->count;
    sum += record->type;
    sum += (record->address & 0xff) + (0xff & (record->address >> 8));
    for( i = 0; i < record->count; i++ ) {
        sum += record->data[i];
    }
    record->checksum = ((uint8_t) (0xff & (0x100 - (0xff & sum))));
    return 0;
}

static int32_t ihex_make_line( struct intel_record *record, char *str ) {
    uint8_t i;
    if( record->type > 5 ) {
        DEBUG( "Record type 0x%X unknown.\n", record->type );
        return -1;
    } else if( record->count > IHEX_COLS ) {
        DEBUG( "Each line must have no more than 16 values.\n" );
        return -1;
    }

    if( record->count == 0 ) {
        // if there is no data set the string to empty, return
        *str = 0;
        return 0;
    }

    ihex_make_checksum(record);          // make the checksum

    sprintf( str, ":%02X%04X%02X",      // ':bbaaaarr'
            record->count, record->address, record->type );

    for ( i = 0; i < record->count; i++ ) {
        sprintf( str + 9 + 2*i, "%02X", record->data[i] );
    }
    sprintf( str + 9 + 2*i, "%02X", record->checksum );

    return 0;
}

static int32_t ihex_make_record_04_offset( uint32_t offset, char *str ) {
    struct intel_record record;
    if ( offset & 0xffff ) {
        DEBUG( "ihex 04 type offset must be divisible by 0x%X, not 0x%X.\n",
                0x10000, offset );
        return -1;
    }
    record.type = 4;
    record.count = 2;
    record.address = 0;
    record.data[0] = (uint8_t) (0xff & (offset >> 24));
    record.data[1] = (uint8_t) (0xff & (offset >> 16));
    return ihex_make_line( &record, str );
}

//static int32_t ihex_process_record( struct intel_record *record,
//        const uint8_t job, uint32_t next_address ) {
//    char line[80];
//    if( record->count ) {
//        if( 0 != ihex_make_line(record, line) ) {
//            DEBUG( "Error making a line.\n" );
//            return -2;
//        } else {
//            ihex_clear_record( record, next_address );
//        }
//        if( job==0 ) {
//            fprintf( stdout, "%s\n", line );
//        }
//    }
//    return 0;
//}

int32_t intel_hex_from_buffer( intel_buffer_in_t *buin,
                               dfu_bool force_full, uint32_t target_offset ) {
    char line[80];
    uint32_t offset_address = 0;    // offset address written to a previous line
    uint32_t address = 0;   // relative offset from previously set addr
    uint32_t i = buin->info.data_start;
    uint16_t i_scan;
    struct intel_record record;

    ihex_clear_record( &record, i + target_offset );

    // target_offset = 0x8000 0000 or 0x8080 0000
    // use buin->info.data_start to buin->info.data_stop as range
    // if target offset > page size, use process 04
    // reasons to complete current line:
    //      last value, next page blank, last page value, #cols reached

    for( i = buin->info.data_start; i <= buin->info.data_end; i++ ) {
        address = i + target_offset;
        if( i % buin->info.page_size == 0 && !(force_full) ) {
            /* you are at the start of a memory page, if force_full is not set
             * then check if there is any data on the page, if there is none,
             * then write current line and increment to the next page
             */
            for( i_scan = 0; i_scan < buin->info.page_size; i_scan++ ) {
                if( buin->data[i + i_scan] != 0xFF )
                    break;
            }
            if( i_scan == buin->info.page_size ) {
                // no data found: write current, jump to next page
                if( 0 != ihex_make_line(&record, line) ) {
                    DEBUG( "Error making a line.\n" );
                    return -2;
                } else {
                    if( *line )
                        fprintf( stdout, "%s\n", line );
                    ihex_clear_record( &record, address + i_scan - offset_address );
                }
                i += buin->info.page_size - 1;
                continue;
            }
        }
        if( address - offset_address >= 0x10000 ) {
            offset_address = (address / IHEX_64KB_PAGE) * IHEX_64KB_PAGE;
            // complete the line, before adding this next point

            if( 0 != ihex_make_line(&record, line) ) {
                DEBUG( "Error making a line.\n" );
                return -2;
            } else {
                if( *line )
                    fprintf( stdout, "%s\n", line );
                ihex_clear_record( &record, address - offset_address );
            }

            // reset offset address
            if( 0 != ihex_make_record_04_offset(offset_address, line) ) {
                DEBUG( "Error making a class 4 offset.\n" );
                return -2;
            } else {
                if( *line )
                    fprintf( stdout, "%s\n", line );
            }
        }
        if( record.count == IHEX_COLS ) {
            if( 0 != ihex_make_line(&record, line) ) {
                DEBUG( "Error making a line.\n" );
                return -2;
            } else {
                if( *line )
                    fprintf( stdout, "%s\n", line );
                ihex_clear_record( &record, address - offset_address );
            }
        }
        record.data[ record.count ] = buin->data[i];
        record.count ++;
    }
    if( record.count ) {
        if( 0 != ihex_make_line(&record, line) ) {
            DEBUG( "Error making a line.\n" );
            return -2;
        } else {
            if( *line )
                fprintf( stdout, "%s\n", line );
            ihex_clear_record( &record, address - offset_address );
        }
    }
    fprintf( stdout, ":00000001FF\n" );

    return 0;
}

int32_t intel_init_buffer_out( intel_buffer_out_t *bout,
                               size_t total_size, size_t page_size ) {
    uint32_t i;
    if ( !total_size || !page_size ) {
        DEBUG("What are you thinking... size must be > 0.\n");
        return -1;
    }

    bout->info.total_size = total_size;
    bout->info.page_size = page_size;
    bout->info.data_start = UINT32_MAX;      // invalid data start
    bout->info.data_end = 0;
    bout->info.valid_start = 0;
    bout->info.valid_end = total_size - 1;
    bout->info.block_start = 0;
    bout->info.block_end = 0;
    // allocate the memory
    bout->data = (uint16_t *) malloc( total_size * sizeof(uint16_t) );
    if( NULL == bout->data ) {
        DEBUG( "ERROR allocating 0x%X bytes of memory.\n",
                total_size * sizeof(uint16_t));
        return -2;
    }

    // initialize buffer to 0xFFFF (invalid / unassigned data)
    for( i = 0; i < total_size; i++ ) {
        bout->data[i] = UINT16_MAX;
    }
    return 0;
}

int32_t intel_init_buffer_in( intel_buffer_in_t *buin,
                              size_t total_size, size_t page_size ) {
    // TODO : is there a way to combine this and above? maybe typecast to an
    // in or out buffer from a char pointer?
    buin->info.total_size = total_size;
    buin->info.page_size = page_size;
    buin->info.data_start = 0;
    buin->info.data_end = total_size - 1;
    buin->info.valid_start = 0;
    buin->info.valid_end = total_size - 1;
    buin->info.block_start = 0;
    buin->info.block_end = 0;

    buin->data = (uint8_t *) malloc( total_size );
    if( NULL == buin->data ) {
        DEBUG( "ERROR allocating 0x%X bytes of memory.\n", total_size );
        return -2;
    }

    // initialize buffer to 0xFF (blank / unassigned data)
    memset( buin->data, UINT8_MAX, total_size );

    return 0;
}

int32_t intel_validate_buffer( intel_buffer_in_t *buin,
                               intel_buffer_out_t *bout,
                               dfu_bool quiet) {
    int32_t i;
    int32_t invalid_data_region = 0;
    int32_t invalid_outside_data_region = 0;

    DEBUG( "Validating image from byte 0x%X to 0x%X.\n",
            bout->info.valid_start, bout->info.valid_end );

    if( !quiet ) fprintf( stderr, "Validating...  " );
    for( i = bout->info.valid_start; i <= bout->info.valid_end; i++ ) {
        if(  bout->data[i] <= UINT8_MAX ) {
            // Memory should have been programmed here
            if( ((uint8_t) bout->data[i]) != buin->data[i] ) {
                if ( !invalid_data_region ) {
                    if( !quiet ) fprintf( stderr, "ERROR\n" );
                    DEBUG( "Image did not validate at byte: 0x%X of 0x%X.\n", i,
                            bout->info.valid_end - bout->info.valid_start + 1 );
                    DEBUG( "Wanted 0x%02x but read 0x%02x.\n",
                            0xff & bout->data[i], buin->data[i] );
                    DEBUG( "suppressing additional warnings.\n");
                }
                invalid_data_region++;
            }
        } else {
            // Memory should be blank here
            if( 0xff != buin->data[i] ) {
                if ( !invalid_data_region ) {
                    DEBUG( "Outside program region: byte 0x%X epected 0xFF.\n", i);
                    DEBUG( "but read 0x%02X.  supressing additional warnings.\n",
                            buin->data[i] );
                }
                invalid_outside_data_region++;
            }
        }
    }

    if( !quiet ) {
        if ( 0 == invalid_data_region + invalid_outside_data_region ) {
            fprintf( stderr, "Success\n" );
        } else {
            fprintf( stderr,
                    "%d invalid bytes in program region, %d outside region.\n",
                    invalid_data_region, invalid_outside_data_region );
        }
    }

    return invalid_data_region ?
        -1 * invalid_data_region : invalid_outside_data_region;
}

int32_t intel_flash_prep_buffer( intel_buffer_out_t *bout ) {
    uint16_t *page;
    int32_t i;

    TRACE( "%s( %p )\n", __FUNCTION__, bout );

    // increment pointer by page_size * sizeof(int16) until page_start >= end
    for( page = bout->data;
            page < &bout->data[bout->info.valid_end];
            page = &page[bout->info.page_size] ) {
        // check if there is valid data on this page
        for( i = 0; i < bout->info.page_size; i++ ) {
            if( page[i] <= UINT8_MAX )
                break;
        }

        if( bout->info.page_size != i ) {
            /* There was valid data in the block & we need to make
             * sure there is no unassigned data.  */
            for( i = 0; i < bout->info.page_size; i++ ) {
                if( page[i] > UINT8_MAX )
                    page[i] = 0xff;         // 0xff is blank
            }
        }
    }
    return 0;
}
