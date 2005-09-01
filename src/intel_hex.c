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
 * Copyright (C) 2005 Weston Schmidt <weston_schmidt@yahoo.com>
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

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>

#include "intel_hex.h"

struct intel_record {
    unsigned int count;
    unsigned int type;
    unsigned int checksum;
    unsigned int address;
    char data[256];
};


/*
 *  This walks over the record and ensures that the checksum is
 *  correct for the record.
 *
 *  returns 0 if checksum validates, anything else on error
 */
static int intel_validate_checksum( struct intel_record *record )
{
    int i = 0;
    int checksum = 0;

    checksum = record->count + record->type + record->checksum +
                        (record->address >> 8) + (0xff & record->address);

    for( i = 0; i < record->count; i++ ) {
        checksum += record->data[i];
    }

    return (0xff & checksum);
}


static int intel_validate_line( struct intel_record *record )
{
    /* Validate the checksum */
    if( 0 != intel_validate_checksum(record) )
        return -1;

    /* Validate the type */
    switch( record->type ) {
        case 0:
            /* Nothing to do. */
            break;

        case 1:
            if( 0 != record->count )
                return -2;
            break;

        case 2:
            if(    (0 != record->address)
                || (2 != record->count)
                || (record->data[1] != (0xf8 & record->data[1])) )
            {
                return -3;
            }
            break;

        case 4:
            if( (0 != record->address) || (2 != record->count) )
                return -4;
            break;

        default:
            fprintf( stderr, "Unsupported type. %d\n", record->type );
            /* Types 3 & 5 as well as any other types are unsupported. */
            return -5;
    }

    return 0;
}


static void intel_process_address( struct intel_record *record )
{
    switch( record->type ) {
        case 2:
            /* 0x1238 -> 0x00012380 */
            record->address = (record->data[0] << 8) | record->data[1];
            record->address *= 16;
            break;

        case 4:
            /* 0x1234 -> 0x12340000 */
            record->address = (record->data[0] << 8) | record->data[1];
            record->address <<= 16;
            break;
    }
}


static int intel_read_data( FILE *fp, struct intel_record *record )
{
    char *buffer = NULL;
    size_t buffer_length = 0;
    int length = 0;
    int i = 0, j = 0;
    int addr_upper = 0;
    int addr_lower = 0;

    length = getline( &buffer, &buffer_length, fp );
    if( (length < 11) || (NULL == buffer) )
        goto error;

    if( 4 != sscanf(buffer, ":%02x%02x%02x%02x", &(record->count),
                                                 &addr_upper,
                                                 &addr_lower,
                                                 &(record->type))
      )
        goto error;

    record->address = addr_upper << 8 | addr_lower;

    /* Trim the \r\n values if they are there. */
    if( '\n' == buffer[length-1] )
        length--;

    if( '\r' == buffer[length-1] )
        length--;

    if( ((record->count * 2) + 11) != length )
        goto error;

    for( i = 9, j = 0; i < length - 2; i += 2, j++ ) {
        int data = 0;
        if( 1 != sscanf(&buffer[i], "%02x", &data) )
            goto error;
        record->data[j] = 0xff & data;
    }

    if( 1 != sscanf(&buffer[length-2], "%02x", &(record->checksum)) )
        goto error;

    free( buffer );

    return 0;

error:
    if( NULL != buffer )
        free( buffer );

    return -1;

}


static int intel_parse_line( FILE *fp, struct intel_record *record )
{
    if( 0 != intel_read_data(fp, record) )
        return -1;

    if( 0 != intel_validate_line(record) )
        return -1;

    intel_process_address( record );

    return 0;
}

char *intel_hex_to_buffer( char *filename, int max_size,
                           char invalid,   int *usage )
{
    char *memory = NULL;
    FILE *fp = NULL;
    int failure = 1;
    struct intel_record record;
    unsigned int address = 0;
    unsigned int address_offset = 0;
    int i = 0;

    if( (NULL == filename) || (0 >= max_size)  ) {
        fprintf( stderr, "Invalid filename or max_size.\n" );
        goto error;
    }

    fp = fopen( filename, "r" );
    if( NULL == fp ) {
        fprintf( stderr, "Error opening the file.\n" );
        goto error;
    }

    memory = (char *) malloc( max_size );
    if( NULL == memory )
        goto error;

    for( i = 0; i < max_size; i++ )
        memory[i] = invalid;

    *usage = 0;
    do {
        if( 0 != intel_parse_line(fp, &record) ) {
            goto error;
        }

        switch( record.type ) {
            case 0:
                address = address_offset + record.address;
                for( i = 0; i < record.count; i++ ) {
                    if( address >= max_size )
                        goto error;

                    memory[address++] = record.data[i];
                    (*usage)++;
                }
                break;

            case 2:
            case 4:
                address_offset = record.address;
                break;
        }

    } while( (1 != record.type) );

    failure = 0;

error:

    if( NULL != fp ) {
        fclose( fp );
        fp = NULL;
    }

    if( (NULL != memory) && (0 != failure) ) {
        free( memory );
        memory = NULL;
    }

    return memory;
}
