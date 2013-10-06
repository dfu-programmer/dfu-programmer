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
    char data[256];
};

#define IHEX_DEBUG_THRESHOLD 50

#define DEBUG(...)  dfu_debug( __FILE__, __FUNCTION__, __LINE__, \
                               IHEX_DEBUG_THRESHOLD, __VA_ARGS__ )



#define IHEX_DEBUG_THRESHOLD 50

#define DEBUG(...)  dfu_debug( __FILE__, __FUNCTION__, __LINE__, \
                               IHEX_DEBUG_THRESHOLD, __VA_ARGS__ )

/*  This walks over the record and ensures that the checksum is
 *  correct for the record.
 *
 *  returns 0 if checksum validates, anything else on error
 */
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

int32_t intel_hex_to_buffer( char *filename, struct buffer_out *bout) {
    uint32_t prog_size = bout->prog_usage;      // size of the flash
    uint32_t user_size = bout->user_usage;      // size of the user page
    const uint32_t ustart = 0x800000;           // start addresss of user page
    FILE *fp = NULL;
    int32_t failure = 1;
    struct intel_record record;
    // unsigned int count, type, checksum, address; char data[256]
    unsigned int address = 0;
    unsigned int address_offset = 0;
    int i = 0;
    bout->prog_usage = 0;
    bout->user_usage = 0;
    bout->prog_data = NULL;
    bout->user_data = NULL;

    if ( (0 >= prog_size) && (0 >= user_size) ) {
        fprintf( stderr, "Must provide valid memory size in bout.\n" );
        goto error;
    }

    if (NULL == filename) {
        fprintf( stderr, "Invalid filename.\n" );
        goto error;
    }

    if( 0 == strcmp("STDIN", filename) ) {
        fp = stdin;
    } else {
        fp = fopen( filename, "r" );
        if( NULL == fp ) {
            fprintf( stderr, "Error opening the file.\n" );
            goto error;
        }
    }

    if ( prog_size ) {
        // allocate the memory
        bout->prog_data = (int16_t *) malloc( prog_size * sizeof(int16_t) );
        if( NULL == bout->prog_data ) {
            fprintf( stderr, "Error getting memory for program data.\n" );
            goto error;
        }

        // initialize program memory to -1 (invalid data)
        for( i = 0; i < prog_size; i++ ) {
            bout->prog_data[i] = -1;
        }
    }

    if ( user_size ) {
        bout->user_data = (int16_t *) malloc( user_size * sizeof(int16_t) );
        if( NULL == bout->user_data ) {
            fprintf( stderr, "Error getting memory for user page.\n" );
            goto error;
        }
        for( i = 0; i < user_size; i++ ) {
            bout->user_data[i] = -1;
        }
    }

    // iterate through ihex file and assign values to memory and user
    do {
        if( 0 != (i=intel_parse_line(fp, &record)) ) {
            fprintf( stderr, "Error parsing line %d, (err %d).\n",
                    bout->prog_usage + bout->user_usage, i );
            goto error;
        }

        switch( record.type ) {
            case 0:
                address = address_offset + record.address;
                for( i = 0; i < record.count; i++ ) {
                    if ((ustart <= address) && (address < ustart + user_size)) {
                        bout->user_data[address-ustart] = 0xff & record.data[i];
                        address++;
                        (bout->user_usage)++;
                    } else if (address_offset == 0x800000) {
                        fprintf( stderr,
                                "Address error: 0x%02x outside user page",
                                record.address);
                        goto error;
                    } else if ( address >= prog_size ) {
                        fprintf( stderr,
                                "Address error: 0x%x with offset 0x%x.\n",
                                record.address, address_offset);
                        goto error;
                    } else {
                        bout->prog_data[address] = 0xff & record.data[i];
                        address++;
                        (bout->prog_usage)++;
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
                address_offset = (0x7fffffff & record.address);
                DEBUG( "Address offset set to 0x%x.\n", address_offset );
                break;
        }
    } while( (1 != record.type) );

    failure = 0;

error:
    if( NULL != fp ) {
        fclose( fp );
        fp = NULL;
    }

    if ( 0 != failure ) {
        if ( NULL != bout->prog_data ) {
            free( bout->prog_data );
            bout->prog_data = NULL;
        }

        if ( NULL != bout->user_data ) {
            free( bout->user_data );
            bout->user_data = NULL;
        }
    }

    return failure;
}
