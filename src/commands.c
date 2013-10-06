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

// ________  P R O T O T Y P E S  _______________________________
static int32_t execute_validate( dfu_device_t *device,
                                 atmel_buffer_out_t *bout,
                                 uint8_t mem_segment,
                                 dfu_bool quiet );
/* provide an out buffer to validate and whether this is from
 * flash or eeprom data sections, also wether you want it quiet
 */

// ________  F U N C T I O N S  _______________________________
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

static void security_message( void ) {
    if( security_bit_state > ATMEL_SECURE_OFF ) {
        fprintf( stderr, "The security bit %s set.\n"
                         "Erase the device to clear temporarily.\n",
                         (ATMEL_SECURE_ON == security_bit_state) ? "is" : "may be" );
    }
}

static int32_t execute_erase( dfu_device_t *device,
                              struct programmer_arguments *args ) {
    int32_t result = 0;

    if( !args->quiet ) {
        fprintf( stderr, "Erasing 0x%X bytes...\n",
           args->flash_address_top - args->flash_address_bottom + 1 );
    }

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
static int32_t serialize_memory_image( atmel_buffer_out_t *bout,
                                     struct programmer_arguments *args ) {
    uint32_t target_offset = 0;
    if( args->command == com_user )
        target_offset = ATMEL_USER_PAGE_OFFSET;

    if ( NULL != args->com_flash_data.serial_data ) {
        int16_t *serial_data = args->com_flash_data.serial_data;
        uint32_t length = args->com_flash_data.serial_length;
        uint32_t offset = args->com_flash_data.serial_offset;
        uint32_t i;

        for( i=0; i < length; ++i ) {
            if ( 0 != intel_process_data(bout, serial_data[i],
                        target_offset, offset + i) ) {
                return -1;
            }
        }
    }
    return 0;
}

static void print_flash_usage( atmel_buffer_out_t *bout ) {
    fprintf( stderr,
            "0x%X bytes written into 0x%X valid bytes (%.02f%%).\n",
            bout->data_end - bout->data_start + 1,
            bout->valid_end - bout->valid_start + 1,
            ((float) (100 * (bout->data_end - bout->data_start + 1)) /
                (float) (bout->valid_end - bout->valid_start + 1)) ) ;
}

static int32_t execute_flash_eeprom( dfu_device_t *device,
                                     struct programmer_arguments *args ) {
    int32_t result;
    int32_t retval;
    atmel_buffer_out_t bout;

    retval = -1;

    if( 0 == args->eeprom_memory_size ) {
        fprintf( stderr, "This device has no eeprom.\n" );
        return -1;
    }

    if( 0 != atmel_init_buffer_out(&bout, args->eeprom_memory_size,
                args->eeprom_page_size) ) {
        DEBUG("ERROR initializing a buffer.\n");
        goto error;
    }

    result = intel_hex_to_buffer( args->com_flash_data.file, &bout, 0 );

    if ( result < 0 ) {
        DEBUG( "Something went wrong with creating the memory image.\n" );
        fprintf( stderr,
                 "Something went wrong with creating the memory image.\n" );
        goto error;
    } else if ( result > 0 ) {
        DEBUG( "WARNING: File contains 0x%X bytes outside target memory.\n",
                result );
    }


    if (0 != serialize_memory_image(&bout, args))
      goto error;

    if( 0 != (result = atmel_flash(device, &bout, true, args->quiet)) ) {
        DEBUG( "Error while programming eeprom. (%d)\n", result );
        fprintf( stderr, "Error while programming eeprom.\n" );
        goto error;
    }

    if( 0 == args->com_flash_data.suppress_validation ) {
        if( 0 != execute_validate(device, &bout, mem_eeprom, args->quiet) ) {
            fprintf( stderr,
                    "Memory did not validate. Did you erase first?\n" );
            goto error;
        }
    }

    if( 0 == args->quiet ) print_flash_usage( &bout );

    retval = 0;

error:
    if( NULL != bout.data ) {
        free( bout.data );
        bout.data = NULL;
    }

    return retval;
}

static int32_t execute_validate( dfu_device_t *device,
                                 atmel_buffer_out_t *bout,
                                 uint8_t mem_segment,
                                 const dfu_bool quiet ) {
    int32_t retval = -1;        // return value for this fcn
    int32_t result;             // result of fcn calls
    atmel_buffer_in_t buin;     // buffer in for storing read mem

    if( 0 != atmel_init_buffer_in(&buin, bout->total_size ) ) {
        DEBUG("ERROR initializing a buffer.\n");
        goto error;
    }
    buin.valid_start = bout->valid_start;
    buin.valid_end = bout->valid_end;

    if( !quiet ) fprintf( stderr, "Reading 0x%X bytes...\n",
            buin.valid_end - buin.valid_start + 1 );
    if( 0 !=  (result = atmel_read_flash(device, &buin,
                                         mem_segment, quiet)) ) {
        DEBUG("ERROR: could not read memory, err %d.\n", result);
        fprintf( stderr, "Error while reading back memory.\n" );
        goto error;
    }

    if( !quiet ) fprintf( stderr, "Validating...  " );
    if( 0 != atmel_validate_buffer( &buin, bout ) ) {
        goto error;
    }
    if( !quiet ) fprintf( stderr, "SUCCESS\n" );

    retval = 0;

error:
    if( !quiet && 0 != retval ) fprintf( stderr, "FAIL\n" );

    if( NULL != buin.data ) {
        free( buin.data );
        buin.data = NULL;
    }

    return retval;
}

static int32_t execute_flash_user_page( dfu_device_t *device,
                                        struct programmer_arguments *args ) {
    int32_t result;
    int32_t i;
    int32_t retval;
    atmel_buffer_out_t bout;

    retval = -1;

    if (args->device_type != ADC_AVR32) {
        fprintf(stderr, "Flash User only implemented for ADC_AVR32 devices.\n");
        goto error;
    }

// TODO : consider accepting a string to flash to the user page as well as a hex
// file.. this would be easier than using serialize and could return the address
// location of the start of the string (to be used in the program file)

    // ----------------- CONVERT HEX FILE TO BINARY -------------------------
    if( 0 != atmel_init_buffer_out(&bout, args->flash_page_size,
                args->flash_page_size) ) {
        DEBUG("ERROR initializing a buffer.\n");
        goto error;
    }

    result = intel_hex_to_buffer( args->com_flash_data.file, &bout,
            ATMEL_USER_PAGE_OFFSET );

    if( result < 0 ) {
        fprintf( stderr,
                 "ERROR: Could not create user page memory image.\n" );
        goto error;
    } else if ( result > 0 ) {
        DEBUG( "User page is %d bytes at offset 0x%X.\n",
                args->flash_page_size, ATMEL_USER_PAGE_OFFSET );
        DEBUG( "File contains 0x%X bytes outside target address range.\n",
                result );
        fprintf( stderr, "WARNING: 0x%X bytes are outside target memory,\n",
                result );
        fprintf( stderr, " and will not be written.\n" );
    }

    if ( NULL != args->com_flash_data.serial_data ) {
        fprintf ( stderr,
                "ERROR: hex file is required to flash the user page (for now).\n" );
        goto error;

        // --- this is not implemented for now bc it will fail ---
        if (0 != serialize_memory_image( &bout, args ))
            goto error;
    }

    if ( bout.data_start == UINT32_MAX ) {
        fprintf( stderr,
                "ERROR: No data to write into the user page.\n" );
        goto error;
    } else {
        DEBUG("Hex file contains %u bytes to write.\n",
                bout.data_end - bout.data_start + 1 );
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
        for ( i = bout.total_size - 8; i < bout.total_size; i++ ) {
            if ( -1 != bout.data[i] ) {
                fprintf( stderr,
                        "ERROR: data overlap with bootloader configuration word(s).\n" );
                DEBUG( "At position %d, value is %d.\n", i, bout.data[i] );
                fprintf( stderr,
                        "ERROR: use the --force-config flag to write the data.\n" );
                goto error;
            }
        }
    }

    result = atmel_user( device, (int16_t *) bout.data, args->flash_page_size );

    if( result < 0 ) {
        DEBUG( "Error while flashing user page. (%d)\n", result );
        fprintf( stderr, "Error while flashing user page.\n" );
        goto error;
    }

    if( 0 == args->com_flash_data.suppress_validation ) {
        if( 0 != execute_validate(device, &bout, mem_user, args->quiet) ) {
            fprintf( stderr, "Memory did not validate. Did you erase?\n" );
            goto error;
        }
    }

    if( 0 == args->quiet ) print_flash_usage( &bout );

    retval = 0;

error:
    if( NULL != bout.data ) {
        free( bout.data );
        bout.data = NULL;
    }

    return retval;
}

static int32_t execute_flash_normal( dfu_device_t *device,
                                     struct programmer_arguments *args ) {
    int32_t  retval = -1;
    int32_t  result = 0;
    uint32_t  i;
    atmel_buffer_out_t bout;

    /* Why +1? Because the flash_address_top location is inclusive, as
     * apposed to most times when sizes are specified by length, etc.
     * and they are exclusive. */
    /* Flash vs memory size? Memory size is the entire valid region on the chip,
     * but there is often lots of blank in the bootloader region which will
     * exist in the hex file but be ignored by the program.  Flash size is the
     * valid max program size taking into account the region reserved for the
     * bootloader */

    // ----------------- CONVERT HEX FILE TO BINARY -------------------------
    if( 0 != atmel_init_buffer_out(&bout, args->memory_address_top + 1,
                args->flash_page_size) ) {
        DEBUG("ERROR initializing a buffer.\n");
        goto error;
    }
    bout.valid_start = args->flash_address_bottom;
    bout.valid_end = args->flash_address_top;

    result = intel_hex_to_buffer( args->com_flash_data.file, &bout, 0 );

    if ( result < 0 ) {
        DEBUG( "Something went wrong with creating the memory image.\n" );
        fprintf( stderr,
                 "Something went wrong with creating the memory image.\n" );
        goto error;
    } else if ( result > 0 ) {
        DEBUG( "WARNING: File contains 0x%X bytes outside target memory.\n",
                result );
        DEBUG( "There may be data in the user page (offset %#X).\n",
                ATMEL_USER_PAGE_OFFSET );
        DEBUG( "Inspect the hex file or try flash-user.\n" );
        fprintf( stderr, "WARNING: 0x%X bytes are outside target memory,\n",
                result );
        fprintf( stderr, " and will not be written.\n" );
    }

    if (0 != serialize_memory_image( &bout, args ))
      goto error;

    // check that there isn't anything overlapping the bootloader
    for( i = args->bootloader_bottom; i <= args->bootloader_top; i++) {
        if( bout.data[i] <= UINT8_MAX ) {
            if( true == args->suppressbootloader ) {
                //If we're ignoring the bootloader, don't write to it
                bout.data[i] = UINT16_MAX;
                if ( i == bout.data_start )
                    bout.data_start = UINT32_MAX;
                if ( i == bout.data_end )
                    bout.data_end = 0;
            } else {
                fprintf( stderr, "Bootloader and code overlap.\n" );
                fprintf( stderr, "Use --suppress-bootloader-mem to ignore\n" );
                goto error;
            }
        }
    }

    // ------------------  FLASH PROGRAM DATA ------------------------------
    if( 0 != (result = atmel_flash(device, &bout, false, args->quiet)) ) {
        DEBUG( "Error while flashing program data. (err %d)\n", result );
        fprintf( stderr, "Error while flashing program data.\n" );
        goto error;
    }

    // ------------------  VALIDATE PROGRAM ------------------------------
    if( 0 == args->com_flash_data.suppress_validation ) {
        if( 0 != execute_validate(device, &bout, mem_flash, args->quiet) ) {
            fprintf( stderr, "Memory did not validate. Did you erase?\n" );
            goto error;
        }
    }

    if( 0 == args->quiet ) print_flash_usage( &bout );

    retval = 0;

error:
    if( NULL != bout.data ) {
        free( bout.data );
        bout.data = NULL;
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

static int32_t execute_dump( dfu_device_t *device,
                             struct programmer_arguments *args ) {
    int32_t i = 0;
    int32_t retval = -1;        // return value for this fcn
    int32_t result;             // result of fcn calls
    atmel_buffer_in_t buin;     // buffer in for storing read mem
    uint8_t mem_segment = 0;
    size_t mem_size = 0;

    switch( args->command ) {
        case com_dump:
            mem_size = args->memory_address_top + 1;
            mem_segment = mem_flash;
            break;
        case com_edump:
            mem_size = args->eeprom_memory_size;
            mem_segment = mem_eeprom;
            break;
        case com_udump:
            mem_size = args->flash_page_size;
            mem_segment = mem_user;
            break;
        default:
            fprintf( stderr, "Dump not currenlty supported for this memory.\n" );
            goto error;
    }

    if( 0 != atmel_init_buffer_in(&buin, mem_size) ) {
        DEBUG("ERROR initializing a buffer.\n");
        goto error;
    }

    if( args->command == com_dump ) {
        buin.valid_start = args->flash_address_bottom;
        buin.valid_end = args->flash_address_top;
    }

    /* Check AVR32 security bit in order to provide a better error message. */
    security_check( device );   // avr32 has no eeprom, but OK

    if( !args->quiet ) fprintf( stderr, "Reading 0x%X bytes...\n",
            buin.valid_end - buin.valid_start + 1 );
    if( 0 != (result = atmel_read_flash(device, &buin,
                    mem_segment, args->quiet)) ) {
        DEBUG("ERROR: could not read memory, err %d.\n", result);
        fprintf( stderr, "Error while reading back memory.\n" );
        security_message();
        goto error;
    }

    for( i = 0; i <= buin.valid_end; i++ ) {
        fprintf( stdout, "%c", buin.data[i] );
    }

    fflush( stdout );

    retval = 0;

error:
    if( NULL != buin.data ) {
        free( buin.data );
        buin.data = NULL;
    }

    return retval;
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
        case com_edump:
        case com_udump:
            return execute_dump( device, args );
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
