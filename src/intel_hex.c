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
    unsigned int count;
    unsigned int type;
    unsigned int checksum;
    unsigned int address;
    // FIXME : change this to unsigned int 8
    char data[256];
};

#define IHEX_DEBUG_THRESHOLD 50

#define DEBUG(...)  dfu_debug( __FILE__, __FUNCTION__, __LINE__, \
                               IHEX_DEBUG_THRESHOLD, __VA_ARGS__ )



#define IHEX_DEBUG_THRESHOLD 50

#define DEBUG(...)  dfu_debug( __FILE__, __FUNCTION__, __LINE__, \
                               IHEX_DEBUG_THRESHOLD, __VA_ARGS__ )

// ________  P R O T O T Y P E S  _______________________________
static int intel_validate_checksum( struct intel_record *record );
/*  This walks over the record and ensures that the checksum is
 *  correct for the record.
 *
 *  returns 0 if checksum validates, anything else on error
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
            return -8;

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

static void intel_process_address( struct intel_record *record ) {
    switch( record->type ) {
        case 2:
            /* 0x1238 -> 0x00012380 */
            record->address = ((0xff & record->data[0]) << 8);
            record->address |= (0xff & record->data[1]);
            record->address *= 16;
            break;

        case 4:
            /* 0x1234 -> 0x12340000 */
            record->address = ((0xff & record->data[0]) << 8);
            record->address |= (0xff & record->data[1]);
            record->address <<= 16;
            break;

        case 5:
            /* 0x12345678 -> 0x12345678 */
            record->address = ((0xff & record->data[0]) << 24) |
                              ((0xff & record->data[1]) << 16) |
                              ((0xff & record->data[2]) <<  8) |
                               (0xff & record->data[3]);
            break;
    }
}

static int intel_read_data( FILE *fp, struct intel_record *record ) {
    int i;
    int c;
    int status;
    char buffer[10];
    int addr_upper = 0;
    int addr_lower = 0;

    /* read in the ':bbaaaarr'
     *   bb - byte count
     * aaaa - the address in memory
     *   rr - record type
     */

    if( NULL == fgets(buffer, 10, fp) ) return -1;
    status = sscanf( buffer, ":%02x%02x%02x%02x", &(record->count),
                     &addr_upper, &addr_lower, &(record->type) );
    if( 4 != status ) return -2;

    record->address = addr_upper << 8 | addr_lower;

    /* Read the data */
    for( i = 0; i < record->count; i++ ) {
        int data = 0;

        if( NULL == fgets(buffer, 3, fp) ) return -3;
        if( 1 != sscanf(buffer, "%02x", &data) ) return -4;

        record->data[i] = 0xff & data;
    }

    /* Read the checksum */
    if( NULL == fgets(buffer, 3, fp) ) return -5;
    if( 1 != sscanf(buffer, "%02x", &(record->checksum)) ) return -6;

    /* Chomp the [\r]\n */
    c = fgetc( fp );
    if( '\r' == c ) {
        c = fgetc( fp );
    }
    if( '\n' != c ) {
        DEBUG( "Error: end of line != \\n.\n" );
        return -7;
    }

    return 0;
}

static int intel_parse_line( FILE *fp, struct intel_record *record ) {
    int rdata_result;
    if( 0 != (rdata_result = intel_read_data(fp, record)) ) {
        DEBUG( "Error reading data (E = %d).\n", rdata_result );
        return rdata_result;
    }

    switch( intel_validate_line(record) ) {
        case 0:     /* data, extended address, etc */
            intel_process_address( record );
            break;

        case -8:    /* start address (ignore) */
            break;

        default:
            DEBUG( "intel_validate_line returned not -8 or 0" );
            return -1;
    }

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

int32_t intel_process_data( atmel_buffer_out_t *bout, char value,
        uint32_t target_offset, uint32_t address) {
    // NOTE : there are some hex program files that contain data in the user
    // page.  In this situation, the hex file is processed and used as normal
    // with a warning message containing the first line with an invalid address.
    uint32_t raddress;   // relative address = address - target offset

    // The Atmel flash page starts at address 0x80000000, we need to ignore
    // that bit
    target_offset &= 0x7fffffff;
    address &= 0x7fffffff;

    if( (address < target_offset) ||
        (address > target_offset + bout->total_size - 1) ) {
        return -1;
    } else {
        raddress = address - target_offset;
        // address >= target_offset so unsigned '-' is OK
        bout->data[raddress] = (uint16_t) (0xff & value);
        // update data limits
        if( raddress < bout->data_start ) {
            bout->data_start = raddress;
        }
        if( raddress > bout->data_end ) {
            bout->data_end = raddress;
        }
    }
    return 0;
}

int32_t intel_hex_to_buffer( char *filename, atmel_buffer_out_t *bout,
        uint32_t target_offset ) {
    FILE *fp = NULL;
    struct intel_record record;
    // unsigned int count, type, checksum, address; char data[256]
    uint32_t address = 0;       // line address for intel_hex
    uint32_t address_offset = 0;// offset address from intel_hex
    uint32_t line_count = 1;                // used for debugging hex file
    int32_t  invalid_address_count = 0;     // used for error checking
    int32_t retval;             // return value
    int i = 0;

    if ( (0 >= bout->total_size) ) {
        DEBUG( "Must provide valid memory size in bout.\n" );
        retval = -1;
        goto error;
    }

    if (NULL == filename) {
        fprintf( stderr, "Invalid filename.\n" );
        retval = -2;
        goto error;
    }

    if( 0 == strcmp("STDIN", filename) ) {
        fp = stdin;
    } else {
        fp = fopen( filename, "r" );
        if( NULL == fp ) {
            fprintf( stderr, "Error opening %s.\n", filename );
            retval = -3;
            goto error;
        }
    }

    // iterate through ihex file and assign values to memory and user
    do {
        if( 0 != (i=intel_parse_line(fp, &record)) ) {
            fprintf( stderr, "Error parsing line %d, (err %d).\n",
                    line_count, i );
            retval = -4;
            goto error;
        } else
            line_count++;

        switch( record.type ) {
            case 0:
                for( address = address_offset + ((uint32_t) record.address),
                        i = 0; i < record.count; i++, address++ ) {
                    if ( 0 != intel_process_data(bout, record.data[i],
                                target_offset, address) ) {
                        // address was invalid
                        if ( !invalid_address_count ) {
                            intel_invalid_addr_warning(line_count, address,
                                    target_offset, bout->total_size );
                        }
                        invalid_address_count++;
                    }
                }
                break;
            case 2:
                // record.address set appropriately in intel_process_address
            case 4:
                // record.address set appropriately in intel_process_address
            case 5:
                /* Note: In AVR32 memory map, FLASH starts at 0x80000000, but the
                 * ISP places this memory at 0.  The hex file will use 0x8..., so
                 * mask off that bit. */
                address_offset = (uint32_t) (0x7fffffff & record.address);
                DEBUG( "Address offset set to 0x%x.\n", address_offset );
                break;
        }
    } while( (1 != record.type) );

    if ( invalid_address_count ) {
        fprintf( stderr, "Total of 0x%X bytes in invalid addressed.\n",
                invalid_address_count );
    }

    retval = invalid_address_count;

error:
    if( NULL != fp ) {
        fclose( fp );
        fp = NULL;
    }

    return retval;
}

int32_t buffer_to_intel_hex( char *filename, atmel_buffer_in_t *buin,
        uint32_t target_offset ) {
    return -1;
}
