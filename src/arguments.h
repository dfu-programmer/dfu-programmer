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

#ifndef __ARGUMENTS_H__
#define __ARGUMENTS_H__

#include "dfu-bool.h"
#include "atmel.h"

#define DEVICE_TYPE_STRING_MAX_LENGTH   5
/*
 *  atmel_programmer target command
 *
 *  configure {BSB|SBV|SSB|EB|HSB} [--suppress-validation, --quiet, --debug level] value
 *  dump [--quiet, --debug level]
 *  erase [--suppress-validation, --quiet, --debug level]
 *  flash [--suppress-validation, --quiet, --debug level] file
 *  get {bootloader-version|ID1|ID2|BSB|SBV|SSB|EB|manufacturer|family|product-name|product-revision|HSB} [--quiet, --debug level]
 */

extern int debug;

enum targets_enum { tar_at89c51snd1c = 0,
                    tar_at89c5130    = 1,
                    tar_at89c5131    = 2,
                    tar_at89c5132    = 3,
                    tar_at90usb1287  = 4,
                    tar_at90usb1286  = 5,
                    tar_at90usb647   = 6,
                    tar_at90usb646   = 7,
                    tar_at90usb162   = 8,
                    tar_at90usb82    = 9,
                    tar_none         = 10 };

enum commands_enum { com_none, com_erase, com_flash, com_eflash,
                     com_configure, com_get, com_dump, com_edump,
                     com_start_app, com_version, com_reset };

enum configure_enum { conf_BSB = ATMEL_SET_CONFIG_BSB,
                      conf_SBV = ATMEL_SET_CONFIG_SBV,
                      conf_SSB = ATMEL_SET_CONFIG_SSB,
                      conf_EB  = ATMEL_SET_CONFIG_EB,
                      conf_HSB = ATMEL_SET_CONFIG_HSB };

enum get_enum { get_bootloader, get_ID1, get_ID2, get_BSB, get_SBV, get_SSB,
                get_EB, get_manufacturer, get_family, get_product_name,
                get_product_rev, get_HSB };

enum device_type_enum { device_8051, device_AVR };

struct programmer_arguments {
    /* target-specific inputs */
    enum targets_enum target;
    uint16_t vendor_id;
    uint16_t chip_id;
    enum device_type_enum device_type;
    char device_type_string[DEVICE_TYPE_STRING_MAX_LENGTH];
    size_t top_memory_address;
    size_t memory_size;
    size_t flash_page_size;
    dfu_bool initial_abort;
    dfu_bool honor_interfaceclass;
    uint32_t top_eeprom_memory_address;
    size_t eeprom_memory_size;
    size_t eeprom_page_size;

    /* command-specific state */
    enum commands_enum command;
    char quiet;

    union {
        struct com_configure_struct {
            enum configure_enum name;
            int32_t suppress_validation;
            int32_t value;
        } com_configure_data;

        /* No special data needed for 'dump' */

        struct com_erase_struct {
            int32_t suppress_validation;
        } com_erase_data;

        struct com_flash_struct {
            int32_t suppress_validation;
            char original_first_char;
            char *file;
        } com_flash_data;

        struct com_get_struct {
            enum get_enum name;
        } com_get_data;
    };
};

int32_t parse_arguments( struct programmer_arguments *args,
                         const size_t argc,
                         char **argv );
#endif
