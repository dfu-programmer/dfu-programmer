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

#include "config.h"
#include "commands.h"
#include "arguments.h"
#include "intel_hex.h"
#include "atmel.h"


static int execute_erase( struct usb_dev_handle *device,
                          int interface,
                          struct programmer_arguments args )
{
    int result = 0;

    if( 2 < debug ) {
        fprintf( stderr, "%s: erase %d bytes\n", __FUNCTION__,
                         args.memory_size );
    }

    result = atmel_erase_flash( device, interface, ATMEL_ERASE_ALL );
    if( 0 != result )
        return result;

    return atmel_blank_check( device, interface, 0, args.top_memory_address );
}


static int execute_flash( struct usb_dev_handle *device,
                          int interface,
                          struct programmer_arguments args )
{
    char *hex_data = NULL;
    int   usage = 0;
    int   retval = -1;
    int   result = 0;
    char  *buffer = NULL;
    int i;

    buffer = (char *) malloc( args.memory_size );
    if( NULL == buffer ) {
        fprintf( stderr, "Request for %d bytes of memory failed.\n",
                 args.memory_size );
        goto error;
    }

    memset( buffer, 0, args.memory_size );

    hex_data = intel_hex_to_buffer( args.com_flash_data.file,
                                    args.memory_size, 0xff, &usage );
    if( NULL == hex_data ) {
        fprintf( stderr,
                 "Something went wrong with creating the memory image.\n" );
        goto error;
    }

    if( 2 < debug ) {
        fprintf( stderr, "%s: write %d/%d bytes\n", __FUNCTION__,
                         usage, args.memory_size );
    }

    result = atmel_flash( device, interface, 0, args.top_memory_address,
                          hex_data );

    if( args.memory_size != result ) {
        fprintf( stderr, "Error while flashing. (%d)\n", result );
        goto error;
    }

    if( 0 == args.com_flash_data.suppress_validation ) {
        fprintf( stderr, "Validating...\n" );

        result = atmel_read_flash( device, interface, 0,
                                   args.top_memory_address, buffer,
                                   args.memory_size );

        if( args.memory_size != result ) {
            fprintf( stderr, "Error while validating.\n" );
            goto error;
        }

        if( 0 != memcmp(hex_data, buffer, args.memory_size) ) {
            fprintf( stderr, "Image did not validate.\n" );
            goto error;
        }
    }

    if( 0 == args.quiet ) {
        fprintf( stderr, "%d bytes used (%.02f%%)\n", usage,
                         ((float)(usage*100)/(float)(args.top_memory_address)) );
    }

    retval = 0;

error:
    if( NULL != buffer ) {
        free( buffer );
        buffer = NULL;
    }

    if( NULL != hex_data ) {
        free( hex_data );
        hex_data = NULL;
    }

    return retval;
}


static int execute_start_app( struct usb_dev_handle *device,
                              int interface,
                              struct programmer_arguments args )
{
    return atmel_start_app( device, interface );
}


static int execute_get( struct usb_dev_handle *device,
                        int interface,
                        struct programmer_arguments args )
{
    struct atmel_device_info info;
    char *message = NULL;
    short value = 0;
    int status;
    int controller_error = 0;

    if( device_8051 == args.device_type ) {
        status = atmel_read_config_8051( device, interface, &info );
    } else {
        status = atmel_read_config_avr( device, interface, &info );
    }

    if( 0 != status ) {
        fprintf( stderr, "Error reading %s config information.\n",
                         args.device_type_string );
        return status;
    }

    switch( args.com_get_data.name ) {
        case get_bootloader:
            value = info.bootloaderVersion;
            message = "Bootloader Version";
            break;
        case get_ID1:
            value = info.bootID1;
            message = "Device boot ID 1";
            break;
        case get_ID2:
            value = info.bootID2;
            message = "Device boot ID 2";
            break;
        case get_BSB:
            value = info.bsb;
            message = "Boot Status Byte";
            if( device_8051 != args.device_type ) {
                controller_error = 1;
            }
            break;
        case get_SBV:
            value = info.sbv;
            message = "Software Boot Vector";
            if( device_8051 != args.device_type ) {
                controller_error = 1;
            }
            break;
        case get_SSB:
            value = info.ssb;
            message = "Software Security Byte";
            if( device_8051 != args.device_type ) {
                controller_error = 1;
            }
            break;
        case get_EB:
            value = info.eb;
            message = "Extra Byte";
            if( device_8051 != args.device_type ) {
                controller_error = 1;
            }
            break;
        case get_manufacturer:
            value = info.manufacturerCode;
            message = "Manufacturer Code";
            break;
        case get_family:
            value = info.familyCode;
            message = "Family Code";
            break;
        case get_product_name:
            value = info.productName;
            message = "Product Name";
            break;
        case get_product_rev:
            value = info.productRevision;
            message = "Product Revision";
            break;
        case get_HSB:
            value = info.hsb;
            message = "Hardware Security Byte";
            break;
    }

    if( 0 != controller_error ) {
        fprintf( stderr, "%s requires 8051 based controller\n",
                         message );
        return -1;
    }

    if( value < 0 ) {
        fprintf( stderr, "The requested device info is unavailable.\n" );
        return -2;
    }

    fprintf( stdout, "%s%s0x%02x (%d)\n", 
             ((0 == args.quiet) ? message : ""),
             ((0 == args.quiet) ? ": " : ""),
             value, value );
    return 0;
}


static int execute_dump( struct usb_dev_handle *device,
                         int interface,
                         struct programmer_arguments args )
{
    int i = 0;
    char  *buffer = NULL;

    buffer = (char *) malloc( (args.memory_size) );
    if( NULL == buffer ) {
        fprintf( stderr, "Request for %d bytes of memory failed.\n",
                 args.memory_size );
        goto error;
    }

    if( 2 < debug ) {
        fprintf( stderr, "%s: dump %d bytes\n", __FUNCTION__,
                         args.memory_size );
    }

    if( args.memory_size != atmel_read_flash(device, interface, 0,
                                  args.top_memory_address, buffer,
                                  args.memory_size) )
    {
        fprintf( stderr, "Error while validating.\n" );
        return -1;
    }

    for( i = 0; i < args.memory_size; i++ ) {
        fprintf( stdout, "%c", buffer[i] );
    }

    fflush( stdout );

error:
    if( NULL != buffer ) {
        free( buffer );
        buffer = NULL;
    }

    return 0;
}


static int execute_configure( struct usb_dev_handle *device,
                              int interface,
                              struct programmer_arguments args )
{
    int value = args.com_configure_data.value;
    int name = args.com_configure_data.name;

    if( device_8051 != args.device_type ) {
        fprintf( stderr, "target doesn't support configure operation.\n" );
        return -1;
    }

    if( (0xff & value) != value ) {
        fprintf( stderr, "Value to configure must be in range 0-255.\n" );
        return -1;
    }

    if( 0 != atmel_set_config(device, interface, name, value) )
    {
        fprintf( stderr, "Configuration set failed.\n" );
        return -1;
    }

    return 0;
}


int execute_command( struct usb_dev_handle *device,
                     int interface,
                     struct programmer_arguments args )
{
    switch( args.command ) {
        case com_erase:
            return execute_erase( device, interface, args );
        case com_flash:
            return execute_flash( device, interface, args );
        case com_start_app:
            return execute_start_app( device, interface, args );
        case com_get:
            return execute_get( device, interface, args );
        case com_dump:
            return execute_dump( device, interface, args );
        case com_configure:
            return execute_configure( device, interface, args );
        default:
            fprintf( stderr, "Not supported at this time.\n" );
    }

    return -1;
}
