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
#include "stm32.h"
#include "atmel.h"
#include "util.h"

#define COMMAND_DEBUG_THRESHOLD 40

#define DEBUG(...)  dfu_debug( __FILE__, __FUNCTION__, __LINE__, \
                               COMMAND_DEBUG_THRESHOLD, __VA_ARGS__ )


static int security_bit_state;

// ________  P R O T O T Y P E S  _______________________________
static int32_t execute_validate( dfu_device_t *device,
                                 intel_buffer_out_t *bout,
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
    int32_t result = SUCCESS;

    if( !(GRP_STM32 & args->device_type) && !args->com_erase_data.force ) {
        if( 0 == atmel_blank_check( device, args->flash_address_bottom,
                                             args->flash_address_top,
                                             args->quiet ) ) {
            if ( !args->quiet ) {
                fprintf( stderr, "Chip already blank, to force erase use --force.\n");
            }
            return SUCCESS;
        }
    }

    DEBUG( "erase 0x%X bytes.\n",
           (args->flash_address_top - args->flash_address_bottom) );

    if( GRP_STM32 & args->device_type ) {
        result = stm32_erase_flash( device, args->quiet );
    } else {
        result = atmel_erase_flash( device, ATMEL_ERASE_ALL, args->quiet );
    }

    if( 0 != result ) {
        return result;
    }

    if( !(GRP_STM32 & args->device_type) &&
            !args->com_erase_data.suppress_validation ) {
        result = atmel_blank_check( device, args->flash_address_bottom,
                                            args->flash_address_top,
                                            args->quiet );
    }
    return result;
}

static int32_t execute_setsecure( dfu_device_t *device,
                                  struct programmer_arguments *args ) {
    int32_t result;

    if( ADC_AVR32 != args->device_type ) {
        DEBUG( "target doesn't support security bit set.\n" );
        fprintf( stderr,  "Operation not supported on %s\n",
                args->device_type_string );
        return ARGUMENT_ERROR;
    }

    result = atmel_secure( device );

    if( result < 0 ) {
        DEBUG( "Error while setting security bit. (%d)\n", result );
        fprintf( stderr, "Error setting security bit.\n" );
        return UNSPECIFIED_ERROR;
    }

    return SUCCESS;
}

// TODO : split this into a new command (no file is needed) - also general
// format of this program is that only 1 command is run at a time.. caveat is
// that if program sets a section in memory to '\0' and serialize sets it
// otherwise, the secion will end up '\0' unless a page erase is used.. so may
// need to keep this part of the flash command, but specify that serialize data
// 'wins' over data from the hex file
static int32_t serialize_memory_image( intel_buffer_out_t *bout,
                                     struct programmer_arguments *args ) {
    uint32_t target_offset = 0;
    if( args->command == com_user )
        target_offset = ATMEL_USER_PAGE_OFFSET;
    else if( args->device_type & GRP_STM32 )
        target_offset = STM32_FLASH_OFFSET;

    if ( NULL != args->com_flash_data.serial_data ) {
        int16_t *serial_data = args->com_flash_data.serial_data;
        uint32_t length = args->com_flash_data.serial_length;
        uint32_t offset = args->com_flash_data.serial_offset;
        uint32_t i;

        for( i=0; i < length; ++i ) {
            if ( 0 != intel_process_data(bout, serial_data[i],
                        target_offset, offset + i) ) {
                return BUFFER_INIT_ERROR;
            }
        }
    }
    return SUCCESS;
}

static int32_t execute_validate( dfu_device_t *device,
                                 intel_buffer_out_t *bout,
                                 uint8_t mem_segment,
                                 const dfu_bool quiet ) {
    int32_t retval = UNSPECIFIED_ERROR;
    int32_t result;             // result of fcn calls
    intel_buffer_in_t buin;     // buffer in for storing read mem

    if( 0 != intel_init_buffer_in(&buin, bout->info.total_size,
                                            bout->info.page_size ) ) {
        DEBUG("ERROR initializing a buffer.\n");
        retval = BUFFER_INIT_ERROR;
        goto error;
    }
    buin.info.data_start = bout->info.valid_start;
    buin.info.data_end = bout->info.valid_end;

    if( device->type & GRP_STM32 ) {
        result = stm32_read_flash( device, &buin, mem_segment, quiet );
    } else {
        result = atmel_read_flash( device, &buin, mem_segment, quiet );
    }

    if( 0 != result ) {
        DEBUG("ERROR: could not read memory, err %d.\n", result);
        retval = FLASH_READ_ERROR;
        goto error;
    }

    if( 0 != (result = intel_validate_buffer( &buin, bout, quiet )) ) {
        if( result < 0 ) {
            retval = VALIDATION_ERROR_IN_REGION;
        } else {
            retval = VALIDATION_ERROR_OUTSIDE_REGION;
        }
        goto error;
    }

    retval = SUCCESS;

error:
    if( !quiet && SUCCESS != retval ) fprintf( stderr, "FAIL\n" );

    if( NULL != buin.data ) {
        free( buin.data );
        buin.data = NULL;
    }

    return retval;
}

static void print_flash_usage( intel_buffer_info_t *info ) {
    fprintf( stderr,
            "0x%X bytes written into 0x%X bytes memory (%.02f%%).\n",
            info->data_end - info->data_start + 1,
            info->valid_end - info->valid_start + 1,
            ((float) (100 * (info->data_end - info->data_start + 1)) /
                (float) (info->valid_end - info->valid_start + 1)) ) ;
}

static int32_t execute_hex2bin( dfu_device_t *device,
        struct programmer_arguments *args ) {
    int32_t  retval = -1;
    uint32_t  i;
    intel_buffer_out_t bout;
    size_t   memory_size;
    size_t   page_size;
    uint32_t target_offset = 0;

    memory_size = args->memory_address_top + 1;
    page_size = args->flash_page_size;

    // ----------------- CONVERT HEX FILE TO BINARY -------------------------
    if( 0 != intel_init_buffer_out(&bout, memory_size, page_size) ) {
        DEBUG("ERROR initializing a buffer.\n");
        goto error;
    }

    if( 0!= intel_hex_to_buffer( args->com_convert_data.file, &bout,
                target_offset, args->quiet ) ) {
        DEBUG( "Something went wrong with creating the memory image.\n" );
        goto error;
    }

    if( !args->quiet )
        fprintf( stderr, "Dumping 0x%X bytes from address offset 0x%X.\n",
                bout.info.data_end + 1, target_offset );
    for( i = 0; i <= bout.info.data_end; i++ ) {
        fprintf( stdout, "%c", bout.data[i] <= 0xFF ? bout.data[i] & 0xFF : 0xFF);
    }

    fflush( stdout );

    retval = 0;

error:
    if( NULL != bout.data ) {
        free( bout.data );
        bout.data = NULL;
    }

    return retval;
}

static int32_t execute_bin2hex( dfu_device_t *device,
        struct programmer_arguments *args ) {
    int32_t retval = -1;        // return value for this fcn
    intel_buffer_in_t buin;     // buffer in for storing read mem
    enum atmel_memory_unit_enum mem_segment = args->com_convert_data.segment;
    size_t mem_size = 0;
    size_t page_size = 0;
    uint32_t target_offset = 0; // address offset on the target device
        // NOTE: target_offset may not be set appropriately for device
        // classes other than ADC_AVR32
    FILE *fp = NULL;
    char *filename = args->com_convert_data.file;

    if( ADC_AVR32 == args->device_type ) {
        target_offset = 0x80000000;
    }

    switch( mem_segment ) {
        case mem_flash:
            mem_size = args->memory_address_top + 1;
            page_size = args->flash_page_size;
            break;
        case mem_eeprom:
            mem_size = args->eeprom_memory_size;
            page_size = args->eeprom_page_size;
            break;
        case mem_user:
            mem_size = args->flash_page_size;
            page_size = args->flash_page_size;
            target_offset = 0x80800000;
            break;
        default:
            fprintf( stderr, "Dump not currenlty supported for this memory.\n" );
            goto error;
    }

    if( 0 != intel_init_buffer_in(&buin, mem_size, page_size) ) {
        DEBUG("ERROR initializing a buffer.\n");
        goto error;
    }

    if( mem_segment == mem_flash ) {
        buin.info.data_start = args->flash_address_bottom;
        buin.info.data_end = args->flash_address_top;
    }

    if( NULL == filename ) {
        if( !args->quiet ) fprintf( stderr, "Invalid filename.\n" );
        retval = -2;
        goto error;
    }

    if( 0 == strcmp("STDIN", filename) ) {
        fp = stdin;
    } else {
        fp = fopen( filename, "r" );
        if( NULL == fp ) {
            if( !args->quiet ) fprintf( stderr, "Error opening %s\n", filename );
            retval = -3;
            goto error;
        }
    }

    buin.info.data_end = fread(buin.data, 1, buin.info.total_size, fp);
    if( buin.info.data_end == 0 ) {
        if( !args->quiet ) fprintf( stderr, "ERROR: no bytes read\n" );
        retval = -4;
        goto error;
    }

    if( !args->quiet )
        fprintf( stderr, "Read 0x%X bytes, making hex with address offset 0x%X.\n",
                buin.info.data_end + 1, target_offset );

    retval = intel_hex_from_buffer( &buin, args->com_convert_data.force, target_offset );

error:
    if( NULL != buin.data ) {
        free( buin.data );
        buin.data = NULL;
    }

    return retval;
}

static int32_t execute_flash( dfu_device_t *device,
                                struct programmer_arguments *args ) {
    int32_t  retval = UNSPECIFIED_ERROR;
    int32_t  result;
    uint32_t  i;
    intel_buffer_out_t bout;
    size_t   memory_size;
    size_t   page_size;
    enum atmel_memory_unit_enum mem_type = args->com_flash_data.segment;
    uint32_t target_offset = 0;

    /* assign the correct memory size */
    switch ( mem_type ) {
        case mem_flash:
            if( args->device_type & GRP_STM32 ) {
                target_offset = STM32_FLASH_OFFSET;
            }
            memory_size = args->memory_address_top + 1;
            page_size = args->flash_page_size;
            break;
        case mem_eeprom:
            if( 0 == args->eeprom_memory_size ) {
                fprintf( stderr, "This device has no eeprom.\n" );
                return ARGUMENT_ERROR;
            }
            memory_size = args->eeprom_memory_size;
            page_size = args->eeprom_page_size;
            break;
        case mem_user:
            memory_size = args->flash_page_size;
            page_size = args->flash_page_size;
            mem_type = mem_user;
            target_offset = ATMEL_USER_PAGE_OFFSET;
            if( args->device_type != ADC_AVR32 ){
                fprintf(stderr, "Flash User only implemented for ADC_AVR32 devices.\n");
                retval = ARGUMENT_ERROR;
                goto error;
            }
            break;
        default:
            DEBUG("Unknown memory type %d\n", mem_type);
            return ARGUMENT_ERROR;
    }

    // ----------------- CONVERT HEX FILE TO BINARY -------------------------
    if( 0 != intel_init_buffer_out(&bout, memory_size, page_size) ) {
        DEBUG("ERROR initializing a buffer.\n");
        retval = BUFFER_INIT_ERROR;
        goto error;
    }

    result = intel_hex_to_buffer( args->com_flash_data.file, &bout,
            target_offset, args->quiet );

    if ( result < 0 ) {
        DEBUG( "Something went wrong with creating the memory image.\n" );
        retval = BUFFER_INIT_ERROR;
        goto error;
    } else if ( result > 0 ) {
        DEBUG( "WARNING: File contains 0x%X bytes outside target memory.\n",
                result );
        if( mem_type == mem_flash ) {
            DEBUG( "There may be data in the user page (offset %#X).\n",
                    ATMEL_USER_PAGE_OFFSET );
            DEBUG( "Inspect the hex file or try flash-user.\n" );
        }
        if( !args->quiet ) {
            fprintf( stderr,
                    "WARNING: 0x%X bytes are outside target memory,\n", result );
            fprintf( stderr, " and will not be written.\n" );
        }
    }
// TODO : consider accepting a string to flash to the user page as well as a hex
// file.. this would be easier than using serialize and could return the address
// location of the start of the string (to be used in the program file)

    if (0 != serialize_memory_image( &bout, args )) {
        retval = BUFFER_INIT_ERROR;
        goto error;
    }

    if( mem_type == mem_flash ) {
        bout.info.valid_start = args->flash_address_bottom;
        bout.info.valid_end = args->flash_address_top;

        // check that there isn't anything overlapping the bootloader
        for( i = args->bootloader_bottom; i <= args->bootloader_top; i++) {
            if( bout.data[i] <= UINT8_MAX ) {
                if( true == args->suppressbootloader ) {
                    //If we're ignoring the bootloader, don't write to it
                    bout.data[i] = UINT16_MAX;
                } else {
                    fprintf( stderr, "Bootloader and code overlap.\n" );
                    fprintf( stderr, "Use --suppress-bootloader-mem to ignore\n" );
                    retval = BUFFER_INIT_ERROR;
                    goto error;
                }
            }
        }
    } else if ( mem_type == mem_user ) {
        // check here about overwriting?

        if ( bout.info.data_start == UINT32_MAX ) {
            fprintf( stderr,
                    "ERROR: No data to write into the user page.\n" );
            retval = BUFFER_INIT_ERROR;
            goto error;
        } else {
            DEBUG("Hex file contains %u bytes to write.\n",
                    bout.info.data_end - bout.info.data_start + 1 );
        }

        if ( !(args->com_flash_data.force) ) {
            /* depending on the version of the bootloader, there could be
            * configuration values in the last word or last two words of the
            * user page.  If these are overwritten the device may not start.
            * A warning should be issued before these values can be changed. */
            fprintf( stderr,
                    "ERROR: --force flag is required to write user page.\n" );
            fprintf( stderr,
                    " Last word(s) in user page contain configuration data.\n");
            fprintf( stderr,
                    " The user page is erased whenever any data is written.\n");
            fprintf( stderr,
                    " Without valid config. device always resets in bootloader.\n");
            fprintf( stderr,
                    " Use dump-user to obtain valid configuration words.\n");
            retval = ARGUMENT_ERROR;
            goto error;
            // TODO : implement so this error only appers when data overlaps the
            // bootloader configuration words.  This would require reading the user
            // page to add that data to the buffer, and also should include
            // checking the bootloader version to make sure the right number of
            // words are blocked / written.
            //  ----------- the below for loop is not currently in use -----------
            for ( i = bout.info.total_size - 8; i < bout.info.total_size; i++ ) {
                if ( -1 != bout.data[i] ) {
                    fprintf( stderr,
                            "ERROR: data overlap with bootloader configuration word(s).\n" );
                    DEBUG( "At position %d, value is %d.\n", i, bout.data[i] );
                    fprintf( stderr,
                            "ERROR: use the --force-config flag to write the data.\n" );
                    retval = ARGUMENT_ERROR;
                    goto error;
                }
            }
        }
    }

    // ------------------ WRITE PROGRAM DATA -------------------------------
    if( mem_type == mem_user ) {
        result = atmel_user( device, &bout );
    } else {
        if( args->device_type & GRP_STM32 ) {
            result = stm32_write_flash( device, &bout,
                    mem_type == mem_eeprom ? true : false,
                    args->com_flash_data.force, args->quiet );
        } else {
            result = atmel_flash(device, &bout,
                    mem_type == mem_eeprom ? true : false,
                    args->com_flash_data.force, args->quiet);
        }
    }
    if( 0 != result ) {
        DEBUG( "Error writing %s data. (err %d)\n", "memory", result );
        retval = FLASH_WRITE_ERROR;
        goto error;
    }

    // ------------------  VALIDATE PROGRAM ------------------------------
    if( 0 == args->com_flash_data.suppress_validation ) {
        if( 0 != ( retval = execute_validate(device, &bout, mem_type, args->quiet)) ) {
            fprintf( stderr, "Memory did not validate. Did you erase?\n" );
            goto error;
        } else if ( 0 == args->quiet ) {
            print_flash_usage( &bout.info );
        }
    } else if( 0 == args->quiet ) {
        print_flash_usage( &bout.info );
    }


    retval = SUCCESS;

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

    /* only ADC_AVR32 seems to support fuse operation */
    if( !(ADC_AVR32 & args->device_type) ) {
        DEBUG( "target doesn't support fuse set operation.\n" );
        fprintf( stderr, "target doesn't support fuse set operation.\n" );
        return ARGUMENT_ERROR;
    }

    /* Check AVR32 security bit in order to provide a better error message. */
    security_check( device );

    if( args->device_type & GRP_STM32 ) {
        fprintf( stderr, "Operation not supported on %s.\n",
                args->device_type_string );
        return ARGUMENT_ERROR;
    } else {
        status = atmel_read_fuses( device, &info );
    }

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

    if( args->device_type & GRP_STM32 ) {
        fprintf( stderr, "Operation not supported on %s.\n",
                args->device_type_string );
        return -1;
    } else {
        status = atmel_read_config( device, &info );
    }

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
    int32_t retval = UNSPECIFIED_ERROR;
    int32_t result;             // result of fcn calls
    intel_buffer_in_t buin;     // buffer in for storing read mem
    enum atmel_memory_unit_enum mem_segment = args->com_read_data.segment;
    size_t mem_size = 0;
    size_t page_size = 0;
    uint32_t target_offset = 0; // address offset on the target device
        // NOTE: target_offset may not be set appropriately for device
        // classes other than ADC_AVR32

    switch( mem_segment ) {
        case mem_flash:
            mem_size = args->memory_address_top + 1;
            page_size = args->flash_page_size;
            if( ADC_AVR32 == args->device_type ) {
                target_offset = 0x80000000;
            } else if( GRP_STM32 & args->device_type ) {
                target_offset = STM32_FLASH_OFFSET;
            }
            break;
        case mem_eeprom:
            mem_size = args->eeprom_memory_size;
            page_size = args->eeprom_page_size;
            break;
        case mem_user:
            mem_size = args->flash_page_size;
            page_size = args->flash_page_size;
            target_offset = 0x80800000;
            break;
        default:
            fprintf( stderr, "Dump not currenlty supported for this memory.\n" );
            retval = ARGUMENT_ERROR;
            goto error;
    }

    if( 0 != intel_init_buffer_in(&buin, mem_size, page_size) ) {
        DEBUG("ERROR initializing a buffer.\n");
        retval = BUFFER_INIT_ERROR;
        goto error;
    }

    if( mem_segment == mem_flash ) {
        buin.info.data_start = args->flash_address_bottom;
        buin.info.data_end = args->flash_address_top;
    }

    if( args->device_type & GRP_STM32 ) {
        result = stm32_read_flash(device, &buin, mem_segment, args->quiet);
    } else {
        /* Check AVR32 security bit in order to provide a better error message */
        security_check( device );   // avr32 has no eeprom, but OK
        result = atmel_read_flash(device, &buin, mem_segment, args->quiet);
    }

    if( 0 != result ) {
        DEBUG("ERROR: could not read memory, err %d.\n", result);
        security_message();
        retval = FLASH_READ_ERROR;
        goto error;
    }

    // determine first & last page with non-blank data
    if( args->com_read_data.force ) {
        buin.info.data_start = 0;
    } else {
        // find first page with data
        for( i = buin.info.data_start; i < buin.info.data_end; i++ ) {
            if( buin.data[i] != 0xFF ) break;
            if( i / buin.info.page_size >
                    buin.info.data_start / buin.info.page_size ) {
                // i has just jumpped to a different page than buin.data_start
                buin.info.data_start = i;
            }
        }
        if( i == buin.info.data_end ) {
            if( !args->quiet )
                fprintf( stderr,
                        "Memory is blank, returning a single blank page.\n"
                        "Use --force to return the entire memory regardless.\n");
            buin.info.data_start = 0;
            buin.info.data_end = buin.info.page_size - 1;
        } else {        // find last page with data
            for( i = buin.info.data_end; i > buin.info.data_start; i-- ) {
                if( buin.data[i] !=0xFF ) break;
                if( i / buin.info.page_size <
                        buin.info.data_end / buin.info.page_size ) {
                    buin.info.data_end = i;
                }
            }
        }
    }

    if( args->com_read_data.bin ) {
        if( !args->quiet )
            fprintf( stderr, "Dumping 0x%X bytes from address offset 0x%X.\n",
                    buin.info.data_end + 1, target_offset );
        for( i = 0; i <= buin.info.data_end; i++ ) {
            fprintf( stdout, "%c", buin.data[i] );
        }
    } else {
        if( !args->quiet )
            fprintf( stderr, "Dumping 0x%X bytes from address offset 0x%X.\n",
                    buin.info.data_end - buin.info.data_start + 1,
                    target_offset + buin.info.data_start );
        intel_hex_from_buffer( &buin,
                args->com_read_data.force, target_offset );
    }

    fflush( stdout );

    retval = SUCCESS;

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

    /* only ADC_AVR32 seems to support fuse operation */
    if( !(ADC_AVR32 & args->device_type) || (GRP_STM32 & args->device_type) ) {
        fprintf( stderr,  "Operation not supported on %s\n",
                args->device_type_string );
        DEBUG( "target doesn't support fuse set operation.\n" );
        return -1;
    }

    /* Check AVR32 security bit in order to provide a better error message. */
    security_check( device );

    if( 0 != atmel_set_fuse(device, name, value) ) {
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
        fprintf( stderr, "Operation not supported on %s\n",
                args->device_type_string );
        DEBUG( "target doesn't support configure operation.\n" );
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
    if( args->device_type & GRP_STM32 ) {
        return stm32_start_app( device, args->quiet );
    } else if( args->com_launch_config.noreset ) {
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
        case com_bin2hex:
            return execute_bin2hex( device, args );
        case com_hex2bin:
            return execute_hex2bin( device, args );
        case com_flash:
            return execute_flash( device, args );
        case com_eflash:
            args->com_flash_data.segment = mem_eeprom;
            args->command = com_launch;
            return execute_flash( device, args );
        case com_user:
            args->com_flash_data.segment = mem_user;
            args->command = com_launch;
            return execute_flash( device, args );
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
            args->command = com_read;
            args->com_read_data.force = true;
            args->com_read_data.bin = 1;
            return execute_dump( device, args );
        case com_edump:
            args->com_read_data.segment = mem_eeprom;
            args->com_read_data.force = true;
            args->command = com_read;
            args->com_read_data.bin = 1;
            return execute_dump( device, args );
        case com_udump:
            args->com_read_data.segment = mem_eeprom;
            args->com_read_data.force = true;
            args->command = com_read;
            args->com_read_data.bin = 1;
            return execute_dump( device, args );
        case com_read:
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

    return ARGUMENT_ERROR;
}
