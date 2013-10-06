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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dfu-bool.h"
#include "config.h"
#include "commands.h"
#include "arguments.h"
#include "intel_hex.h"
#include "atmel.h"
#include "util.h"

#define COMMAND_DEBUG_THRESHOLD 40

#define DEBUG(...)  dfu_debug( __FILE__, __FUNCTION__, __LINE__, \
                               COMMAND_DEBUG_THRESHOLD, __VA_ARGS__ )


static int security_bit_state;

static void security_check( dfu_device_t *device ) {
    if( ADC_AVR32 == device->type ) {
        // Get security bit state for AVR32.
        security_bit_state = atmel_getsecure( device );
        DEBUG( "Security bit check returned %d.\n", security_bit_state );
    } else {
        // Security bit not present or not testable.
        security_bit_state = ATMEL_SECURE_OFF;
    }
}

static void security_message() {
    if( security_bit_state > ATMEL_SECURE_OFF ) {
        fprintf( stderr, "The security bit %s set.\n"
                         "Erase the device to clear temporarily.\n",
                         (ATMEL_SECURE_ON == security_bit_state) ? "is" : "may be" );
    }
}

static int32_t execute_erase( dfu_device_t *device,
                              struct programmer_arguments *args ) {
    int32_t result = 0;

    DEBUG( "erase 0x%X bytes.\n",
           (args->flash_address_top - args->flash_address_bottom) );

    result = atmel_erase_flash( device, ATMEL_ERASE_ALL );
    if( 0 != result ) {
        fprintf( stderr, "Erase Failed.\n" );
        return result;
    } else if( 0 == args->quiet ) {
        fprintf( stderr, "Erase Success.\n" );
    }

    result = atmel_blank_check( device, args->flash_address_bottom,
                                        args->flash_address_top );
    if( 0 != result ) {
        fprintf( stderr, "Blank Check Failed.\n" );
    }
    return result;
}

static int32_t execute_setsecure( dfu_device_t *device,
                                  struct programmer_arguments *args ) {
    int32_t result;

    if( ADC_AVR32 != args->device_type ) {
        DEBUG( "target doesn't support security bit set.\n" );
        fprintf( stderr, "target doesn't support security bit set.\n" );
        return -1;
    }

    result = atmel_secure( device );

    if( result < 0 ) {
        DEBUG( "Error while setting security bit. (%d)\n", result );
        fprintf( stderr, "Error while setting security bit.\n" );
        return -1;
    }

    return 0;
}

// TODO : split this into a new command (no file is needed) - also general
// format of this program is that only 1 command is run at a time.. caveat is
// that if program sets a section in memory to '\0' and serialize sets it
// otherwise, the secion will end up '\0' unless a page erase is used.. so may
// need to keep this part of the flash command, but specify that serialize data
// 'wins' over data from the hex file
static int32_t serialize_memory_image(int16_t *hex_data,
                                     struct programmer_arguments *args ) {
    if ( NULL != args->com_flash_data.serial_data ) {
        int16_t *serial_data = args->com_flash_data.serial_data;
        uint32_t length = args->com_flash_data.serial_length;
        uint32_t offset = args->com_flash_data.serial_offset;
        uint32_t i;
        /* The Atmel flash page starts at address 0x80000000, we need to ignore that bit */
        offset &= 0x7fffffff;
        if ((offset + length) > args->memory_address_top) {
            fprintf(stderr, "The serial data falls outside of the memory region.\n");
            return -1;
        }
        for (i=0; i<length; ++i) {
            hex_data[offset + i] = serial_data[i];
        }
    }
    return 0;
}

static int32_t execute_flash_eeprom( dfu_device_t *device,
                                     struct programmer_arguments *args ) {
    int32_t result;
    int32_t i;
    int32_t retval;
    int32_t usage;
    uint8_t *buffer = NULL;
    int16_t *hex_data = NULL;
    struct buffer_out bout;

    retval = -1;

    if( 0 == args->eeprom_memory_size ) {
        fprintf( stderr, "This device has no eeprom.\n" );
        return -1;
    }

    buffer = (uint8_t *) malloc( args->eeprom_memory_size );
    if( NULL == buffer ) {
        fprintf( stderr, "Request for %lu bytes of memory failed.\n",
                 (unsigned long) args->eeprom_memory_size );
        goto error;
    }
    memset( buffer, 0, args->eeprom_memory_size );

    bout.prog_usage = args->eeprom_memory_size;
    bout.user_usage = 0; // no malloc of user_data so no need to free

    result = intel_hex_to_buffer( args->com_flash_data.file, &bout );

    if ( 0 != result ) {
        DEBUG( "Something went wrong with creating the memory image.\n" );
        fprintf( stderr,
                 "Something went wrong with creating the memory image.\n" );
        goto error;
    }

    hex_data = bout.prog_data;
    usage = bout.prog_usage;

    if (0 != serialize_memory_image(hex_data,args))
      goto error;

    result = atmel_flash( device, hex_data, 0, args->eeprom_memory_size,
                          args->eeprom_page_size, true );

    if( result < 0 ) {
        DEBUG( "Error while programming eeprom. (%d)\n", result );
        fprintf( stderr, "Error while programming eeprom.\n" );
        goto error;
    }

    if( 0 == args->com_flash_data.suppress_validation ) {
        if( 0 == args->quiet ) {
            fprintf( stderr, "Validating...\n" );
        }

        result = atmel_read_flash( device, 0, args->eeprom_memory_size,
                                   buffer, args->eeprom_memory_size, true, false );

        if( args->eeprom_memory_size != result ) {
            DEBUG( "Error while reading back eeprom.\n" );
            fprintf( stderr, "Error while reading back eeprom.\n" );
            goto error;
        }

        for( i = 0; i < result; i++ ) {
            if( (0 <= hex_data[i]) && (hex_data[i] < UINT8_MAX) ) {
                /* Memory should have been programmed in this location. */
                if( ((uint8_t) hex_data[i]) != buffer[i] ) {
                    DEBUG( "Image did not validate at location: %d (%02x != %02x)\n", i,
                           (0xff & hex_data[i]), (0xff & buffer[i]) );
                    fprintf( stderr, "Eeprom did not validate.\n" );
                    goto error;
                }
            }
        }
    }

    if( 0 == args->quiet ) {
        fprintf( stderr, "%d bytes used (%.02f%%)\n", usage,
                         ((float)(usage*100)/(float)(args->eeprom_memory_size)) );
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

static int32_t execute_flash_user_page( dfu_device_t *device,
                                        struct programmer_arguments *args ) {
    int32_t result;
    int32_t i;
    int32_t retval;
    uint8_t *buffer = NULL;
    struct buffer_out bout;

    retval = -1;

    if (args->device_type != ADC_AVR32) {
        fprintf(stderr, "Flash User only implemented for ADC_AVR32 devices.\n");
        goto error;
    }

// TODO : consider accepting a string to flash to the user page as well as a hex
// file.. this would be easier than using serialize and could return the address
// location of the start of the string (to be used in the program file)

    // ----------------- CONVERT HEX FILE TO BINARY -------------------------
    bout.prog_usage = args->memory_address_top - args->memory_address_bottom + 1;
            // better error handling here than with prog_usage = 0;
    bout.user_usage = args->flash_page_size;

    result = intel_hex_to_buffer( args->com_flash_data.file, &bout );

    if( 0 != result ) {
        DEBUG( "User page is %d bytes at offset 0x80800000.\n",
                args->flash_page_size );
        fprintf( stderr,
                 "ERROR: Could not create user page memory image.\n" );
        goto error;
    }

    if ( NULL != args->com_flash_data.serial_data ) {
        fprintf ( stderr,
                "ERROR: hex file is required to flash the user page (for now).\n" );
        goto error;

        // --- this is not implemented for now bc it will fail ---
        if (0 != serialize_memory_image( bout.user_data, args ))
            goto error;
    }

    if ( bout.prog_usage ) {
        DEBUG( "Data (%d bytes) from hex file is in program region. (%d in user)\n",
                bout.prog_usage, bout.user_usage);
        fprintf( stderr, "ERROR: Data exists in program region.\n");
        goto error;
    }

    if ( bout.user_usage == 0 ) {
        fprintf( stderr,
                "ERROR: No data to write into the user page.\n" );
        goto error;
    } else {
        DEBUG("Hex file contains %u bytes to write.\n", bout.user_usage );
    }

    if ( !(args->com_flash_data.force_config) ) {
        /* depending on the version of the bootloader, there could be
         * configuration values in the last word or last two words of the
         * user page.  If these are overwritten the device may not start.
         * A warning should be issued before these values can be changed. */
        fprintf( stderr,
                "ERROR: --force-config flag is required to write user page.\n" );
        fprintf( stderr,
                " Last word(s) in user page contain configuration data.\n");
        fprintf( stderr,
                " The user page is erased whenever any data is written.\n");
        fprintf( stderr,
                " Without valid config. device always resets in bootloader.\n");
        fprintf( stderr,
                " Use dump-user to obtain valid configuration words.\n");
        goto error;
// TODO : implement so this error only appers when data overlaps the bootloader
// configuration words.  This would require reading the user page to add that
// data to the buffer, and also should include checking the bootloader version
// to make sure the right number of words are blocked / written.
//  ----------- the below for loop is not currently in use -----------
        for ( i = args->flash_page_size - 8; i < args->flash_page_size; i++ ) {
            if ( -1 != bout.user_data[i] ) {
                fprintf( stderr,
                        "ERROR: data overlap with bootloader configuration word(s).\n" );
                DEBUG( "At position %d, value is %d.\n", i, bout.user_data[i] );
                fprintf( stderr,
                        "ERROR: use the --force-config flag to write the data.\n" );
                goto error;
            }
        }
    }

    result = atmel_user( device, bout.user_data, args->flash_page_size );

    if( result < 0 ) {
        DEBUG( "Error while flashing user page. (%d)\n", result );
        fprintf( stderr, "Error while flashing user page.\n" );
        goto error;
    }

    if( 0 == args->com_flash_data.suppress_validation ) {
        if( 0 == args->quiet ) {
            fprintf( stderr, "Validating...\n" );
        }

        buffer = (uint8_t *) malloc( args->flash_page_size );
        if( NULL == buffer ) {
            fprintf( stderr, "Request for 0x%X bytes of memory failed.\n",
                    (uint16_t) args->flash_page_size );
            goto error;
        }
        memset( buffer, 0, args->flash_page_size );

        result = atmel_read_flash( device, 0, args->flash_page_size,
                                   buffer, args->flash_page_size, false, true );

        if( args->flash_page_size != result ) {
            DEBUG( "Error while reading back user flash.\n" );
            fprintf( stderr, "Error while reading back user flash.\n" );
            goto error;
        }

        for( i = 0; i < result; i++ ) {
            if( (0 <= bout.user_data[i]) && (bout.user_data[i] < UINT8_MAX) ) {
                /* Memory should have been programmed in this location. */
                if( ((uint8_t) bout.user_data[i]) != buffer[i] ) {
                    DEBUG( "Image did not validate at location: %d (%02x != %02x)\n",
                            i, (0xff & bout.user_data[i]), buffer[i] );
                    fprintf( stderr, "User flash did not validate. Did you erase first?\n" );
                    goto error;
                }
            }
        }
    }

    if( 0 == args->quiet ) {
        fprintf( stderr, "%d bytes written (%.02f%%)\n", bout.user_usage,
                ((float)(bout.user_usage*100) / (float)(args->flash_page_size)) );
    }

    retval = 0;

error:
    if( NULL != buffer ) {
        free( buffer );
        buffer = NULL;
    }

    if( NULL != bout.prog_data ) {
        free( bout.prog_data );
        bout.prog_data = NULL;
    }

    if( NULL != bout.user_data ) {
        free( bout.user_data );
        bout.user_data = NULL;
    }

    return retval;
}

static int32_t execute_flash_normal( dfu_device_t *device,
                                     struct programmer_arguments *args ) {
    int32_t  retval = -1;
    int32_t  result = 0;
    uint8_t *buffer = NULL;
    uint32_t  i,j;
    uint32_t memory_size;
    uint32_t flash_size;
    struct buffer_out bout;

    /* Why +1? Because the flash_address_top location is inclusive, as
     * apposed to most times when sizes are specified by length, etc.
     * and they are exclusive. */
    /* Flash vs memory size? Memory size is the entire valid region on the chip,
     * but there is often lots of blank in the bootloader region which will
     * exist in the hex file but be ignored by the program.  Flash size is the
     * valid max program size taking into account the region reserved for the
     * bootloader */
    memory_size = args->memory_address_top - args->memory_address_bottom + 1;
    flash_size = args->flash_address_top - args->flash_address_bottom + 1;

    // ----------------- CONVERT HEX FILE TO BINARY -------------------------
    bout.prog_usage = memory_size;
    bout.user_usage = args->device_type == ADC_AVR32 ? \
                       args->flash_page_size : 0;

    result = intel_hex_to_buffer( args->com_flash_data.file, &bout );

    if( 0 != result ) {
        DEBUG( "Something went wrong with creating the memory image.\n" );
        fprintf( stderr,
                 "Something went wrong with creating the memory image.\n" );
        goto error;
    }

    if (0 != serialize_memory_image( bout.prog_data, args ))
      goto error;

    // ------------------  FLASH PROGRAM DATA ------------------------------
    // check that there isn't anything overlapping the bootloader
    for( i = args->bootloader_bottom; i <= args->bootloader_top; i++) {
        if( -1 != bout.prog_data[i] ) {
            if( true == args->suppressbootloader ) {
                //If we're ignoring the bootloader, don't write to it
                bout.prog_usage--; // byte was previously counted
                bout.prog_data[i] = -1;
            } else {
                fprintf( stderr, "Bootloader and code overlap.\n" );
                fprintf( stderr, "Use --suppress-bootloader-mem to ignore\n" );
                goto error;
            }
        }
    }

    DEBUG( "Write 0x%X of 0x%X bytes\n", bout.prog_usage, flash_size );

    if( 0 == args->quiet ) {
        fprintf( stderr, "Programming...\n" );
    }

    // flash the hex_data onto the device
    result = atmel_flash( device, bout.prog_data, args->flash_address_bottom,
                          args->flash_address_top + 1, args->flash_page_size, false );

    if( result < 0 ) {
        DEBUG( "Error while flashing program data. (err %d)\n", result );
        fprintf( stderr, "Error while flashing program data.\n" );
        goto error;
    }

    // ------------------  VALIDATE PROGRAM ------------------------------
    if( 0 == args->com_flash_data.suppress_validation ) {
        if( 0 == args->quiet ) {
            fprintf( stderr, "Validating...\n" );
        }

        // initialize a buffer to read data into
        buffer = (uint8_t *) malloc( flash_size );

        if( NULL == buffer ) {
            fprintf( stderr, "Request for %d bytes of memory failed.\n",
                    flash_size );
            goto error;
        }

        memset( buffer, 0, flash_size );

        // load data from device into buffer
        result = atmel_read_flash( device, args->flash_address_bottom,
                                   args->flash_address_top + 1, buffer,
                                   flash_size, false, false );
        DEBUG ( "0x%X bytes of flash read into buffer.\n", result );

        // basic error check on atmel_read_flash
        if( flash_size != result ) {
            DEBUG( "Error reading flash to validate.\n" );
            fprintf( stderr, "Error reading flash to validate.\n" );
            goto error;
        }

        DEBUG( "Starting program validation comparison.\n" );
        // check that valid hex data (where hex_data[i] != -1) matches buffer
        for( i = 0, j = args->flash_address_bottom; i < result; i++, j++ ) {
            if( (0 <= bout.prog_data[j]) && (bout.prog_data[j] < UINT8_MAX) ) {
                /* Memory should have been programmed in this location. */
                if( ((uint8_t) bout.prog_data[j]) != buffer[i] ) {
                    DEBUG( "Image validates between: 0x%X and 0x%X.\n",
                            args->flash_address_bottom, j-1 );
                    DEBUG( "Image did not validate at location: 0x%X of 0x%X.\n",
                            j, args->flash_address_bottom + result );
                    DEBUG( "Wanted 0x%02x but read 0x%02x.\n",
                            (0xff & bout.prog_data[j]), (buffer[i]) );
                    fprintf( stderr, "Flash did not validate. Did you erase first?\n" );
                    goto error;
                }
            }
        }
    }

    if( 0 == args->quiet ) {
        fprintf( stderr, "0x%X of 0x%X bytes programmed (%.02f%%)\n",
                bout.prog_usage, flash_size,
                ((float) (bout.prog_usage * 100)) / ((float) (flash_size)) );
    }

    retval = 0;

    if ( bout.user_usage ) {
        // HOW DO WE WANT TO HANDLE THIS?  IGNORE & WARN?
        fprintf( stderr, "WARNING: Data in the user page region not written.\n" );
        fprintf( stderr, "Use flash user to program user data.\n" );
        // or go ahead and program (this wont work bc error gets thrown when
        // passing in user info that contains program data)
        //DEBUG("0x%X bytes in user page being programmed.\n", bout.user_usage);
        //retval = execute_flash_user_page( device, args );
    }

error:
    if( NULL != buffer ) {
        free( buffer );
        buffer = NULL;
    }

    if( NULL != bout.prog_data ) {
        free( bout.prog_data );
        bout.prog_data = NULL;
    }

    if( NULL != bout.user_data ) {
        free( bout.user_data );
        bout.user_data = NULL;
    }

    return retval;
}

static int32_t execute_getfuse( dfu_device_t *device,
                            struct programmer_arguments *args ) {
    atmel_avr32_fuses_t info;
    char *message = NULL;
    int32_t value = 0;
    int32_t status;

    /* Check AVR32 security bit in order to provide a better error message. */
    security_check( device );

    status = atmel_read_fuses( device, &info );

    if( 0 != status ) {
        DEBUG( "Error reading %s config information.\n",
               args->device_type_string );
        fprintf( stderr, "Error reading %s config information.\n",
                         args->device_type_string );
        security_message();
        return status;
    }

    switch( args->com_getfuse_data.name ) {
        case get_lock:
            value = info.lock;
            message = "Locked regions";
            break;
        case get_epfl:
            value = info.epfl;
            message = "External Privileged Fetch Lock";
            break;
        case get_bootprot:
            value = info.bootprot;
            message = "Bootloader protected area";
            break;
        case get_bodlevel:
            value = info.bodlevel;
            message = "Brown-out detector trigger level";
            break;
        case get_bodhyst:
            value = info.bodhyst;
            message = "BOD Hysteresis enable";
            break;
        case get_boden:
            value = info.boden;
            message = "BOD Enable";
            break;
        case get_isp_bod_en:
            value = info.isp_bod_en;
            message = "ISP BOD enable";
            break;
        case get_isp_io_cond_en:
            value = info.isp_io_cond_en;
            message = "ISP IO condition enable";
            break;
        case get_isp_force:
            value = info.isp_force;
            message = "ISP Force";
            break;
    }
    fprintf( stdout, "%s%s0x%02x (%d)\n",
             ((0 == args->quiet) ? message : ""),
             ((0 == args->quiet) ? ": " : ""),
             value, value );
    return 0;
}

static int32_t execute_get( dfu_device_t *device,
                            struct programmer_arguments *args ) {
    atmel_device_info_t info;
    char *message = NULL;
    int16_t value = 0;
    int32_t status;
    int32_t controller_error = 0;

    /* Check AVR32 security bit in order to provide a better error message. */
    security_check( device );

    status = atmel_read_config( device, &info );

    if( 0 != status ) {
        DEBUG( "Error reading %s config information.\n",
               args->device_type_string );
        fprintf( stderr, "Error reading %s config information.\n",
                         args->device_type_string );
        security_message();
        return status;
    }

    switch( args->com_get_data.name ) {
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
            if( ADC_8051 != args->device_type ) {
                controller_error = 1;
            }
            break;
        case get_SBV:
            value = info.sbv;
            message = "Software Boot Vector";
            if( ADC_8051 != args->device_type ) {
                controller_error = 1;
            }
            break;
        case get_SSB:
            value = info.ssb;
            message = "Software Security Byte";
            if( ADC_8051 != args->device_type ) {
                controller_error = 1;
            }
            break;
        case get_EB:
            value = info.eb;
            message = "Extra Byte";
            if( ADC_8051 != args->device_type ) {
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
            if( ADC_8051 != args->device_type ) {
                controller_error = 1;
            }
            break;
    }

    if( 0 != controller_error ) {
        DEBUG( "%s requires 8051 based controller\n", message );
        fprintf( stderr, "%s requires 8051 based controller\n",
                         message );
        return -1;
    }

    if( value < 0 ) {
        fprintf( stderr, "The requested device info is unavailable.\n" );
        return -2;
    }

    fprintf( stdout, "%s%s0x%02x (%d)\n",
             ((0 == args->quiet) ? message : ""),
             ((0 == args->quiet) ? ": " : ""),
             value, value );
    return 0;
}

static int32_t execute_dump_normal( dfu_device_t *device,
                                    struct programmer_arguments *args ) {
    int32_t i = 0;
    uint8_t *buffer = NULL;
    size_t memory_size;
    size_t adjusted_flash_top_address;

    /* Why +1? Because the flash_address_top location is inclusive, as
     * apposed to most times when sizes are specified by length, etc.
     * and they are exclusive. */
    adjusted_flash_top_address = args->flash_address_top + 1;
    memory_size = adjusted_flash_top_address - args->flash_address_bottom;

    buffer = (uint8_t *) malloc( memory_size );
    if( NULL == buffer ) {
        fprintf( stderr, "Request for %lu bytes of memory failed.\n",
                 (unsigned long) memory_size );
        goto error;
    }

    /* Check AVR32 security bit in order to provide a better error message. */
    security_check( device );

    DEBUG( "dump %d bytes\n", memory_size );

    if( memory_size != atmel_read_flash(device, args->flash_address_bottom,
                                  adjusted_flash_top_address, buffer,
                                  memory_size, false, false) )
    {
        fprintf( stderr, "Failed to read %lu bytes from device.\n",
                 (unsigned long) memory_size );
        security_message();
        return -1;
    }

    if( false == args->bootloader_at_highmem ) {
        for( i = 0; i <= args->bootloader_top; i++ ) {
            fprintf( stdout, "%c", 0xff );
        }
    }
    for( i = 0; i < memory_size; i++ ) {
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

static int32_t execute_dump_eeprom( dfu_device_t *device,
                                    struct programmer_arguments *args ) {
    int32_t i = 0;
    uint8_t *buffer = NULL;
    size_t memory_size;

    if( 0 == args->eeprom_memory_size ) {
        fprintf( stderr, "This device has no eeprom.\n" );
        return -1;
    }

    memory_size = args->eeprom_memory_size;

    buffer = (uint8_t *) malloc( args->eeprom_memory_size );
    if( NULL == buffer ) {
        fprintf( stderr, "Request for %lu bytes of memory failed.\n",
                 (unsigned long) memory_size );
        goto error;
    }

    /* Check AVR32 security bit in order to provide a better error message. */
    security_check( device );

    DEBUG( "dump %d bytes\n", memory_size );

    if( memory_size != atmel_read_flash(device, 0,
                                  args->eeprom_memory_size, buffer,
                                  memory_size, true, false) )
    {
        fprintf( stderr, "Failed to read %lu bytes from device.\n",
                 (unsigned long) memory_size );
        security_message();
        return -1;
    }

    for( i = 0; i < memory_size; i++ ) {
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

static int32_t execute_dump_user_page( dfu_device_t *device,
                             struct programmer_arguments *args ) {
    int32_t i = 0;
    uint8_t *buffer = NULL;
    size_t page_size = args->flash_page_size;

    buffer = (uint8_t *) malloc( page_size );
    if( NULL == buffer ) {
        fprintf( stderr, "Request for %lu bytes of memory failed.\n",
                 (unsigned long) page_size );
        goto error;
    }

    /* Check AVR32 security bit in order to provide a better error message. */
    security_check( device );

    DEBUG( "dump %d bytes\n", page_size );

    if( page_size != atmel_read_flash(device, 0,
                                  page_size, buffer,
                                  page_size, false, true) )
    {
        fprintf( stderr, "Failed to read %lu bytes from device.\n",
                 (unsigned long) page_size );
        security_message();
        return -1;
    }

    for( i = 0; i < page_size; i++ ) {
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

static int32_t execute_setfuse( dfu_device_t *device,
                                  struct programmer_arguments *args ) {
    int32_t value = args->com_setfuse_data.value;
    int32_t name = args->com_setfuse_data.name;

    if( GRP_AVR & args->device_type ) {
        DEBUG( "target doesn't support fuse set operation.\n" );
        fprintf( stderr, "target doesn't support fuse set operation.\n" );
        return -1;
    }

    /* Check AVR32 security bit in order to provide a better error message. */
    security_check( device );

    if( 0 != atmel_set_fuse(device, name, value) )
    {
        DEBUG( "Fuse set failed.\n" );
        fprintf( stderr, "Fuse set failed.\n" );
        security_message();
        return -1;
    }

    return 0;
}

static int32_t execute_configure( dfu_device_t *device,
                                  struct programmer_arguments *args ) {
    int32_t value = args->com_configure_data.value;
    int32_t name = args->com_configure_data.name;

    if( ADC_8051 != args->device_type ) {
        DEBUG( "target doesn't support configure operation.\n" );
        fprintf( stderr, "target doesn't support configure operation.\n" );
        return -1;
    }

    if( (0xff & value) != value ) {
        DEBUG( "Value to configure must be in range 0-255.\n" );
        fprintf( stderr, "Value to configure must be in range 0-255.\n" );
        return -1;
    }

    if( 0 != atmel_set_config(device, name, value) )
    {
        DEBUG( "Configuration set failed.\n" );
        fprintf( stderr, "Configuration set failed.\n" );
        return -1;
    }

    return 0;
}

static int32_t execute_launch( dfu_device_t *device,
                                  struct programmer_arguments *args ) {
    if ( args->com_launch_config.noreset ) {
        return atmel_start_app_noreset( device );
    } else {
        return atmel_start_app_reset( device );
    }
}

int32_t execute_command( dfu_device_t *device,
                         struct programmer_arguments *args ) {
    device->type = args->device_type;
    switch( args->command ) {
        case com_erase:
            return execute_erase( device, args );
        case com_flash:
            return execute_flash_normal( device, args );
        case com_eflash:
            return execute_flash_eeprom( device, args );
        case com_user:
            return execute_flash_user_page( device, args );
        case com_start_app:
            args->com_launch_config.noreset = true;
        case com_reset:
            args->command = com_launch;
        case com_launch:
            return execute_launch( device, args );
        case com_get:
            return execute_get( device, args );
        case com_getfuse:
            return execute_getfuse( device, args );
        case com_dump:
            return execute_dump_normal( device, args );
        case com_edump:
            return execute_dump_eeprom( device, args );
        case com_udump:
            return execute_dump_user_page( device, args );
        case com_configure:
            return execute_configure( device, args );
        case com_setfuse:
            return execute_setfuse( device, args );
        case com_setsecure:
            return execute_setsecure( device, args );
        default:
            fprintf( stderr, "Not supported at this time.\n" );
    }

    return -1;
}
