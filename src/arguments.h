/*
 * dfu-programmer
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

#ifndef __ARGUMENTS_H__
#define __ARGUMENTS_H__

#include "atmel.h"

/*
 *  atmel_programmer target command
 *
 *  configure {BSB|SBV|SSB|EB|HSB} [--suppress-validation, --quiet, --debug level] value
 *  dump [--quiet, --debug level]
 *  erase [--suppress-validation, --quiet, --debug level]
 *  flash [--suppress-validation, --quiet, --debug level] file
 *  get {bootloader-version|ID1|ID2|BSB|SBV|SSB|EB|manufacturer|family|product-name|product-revision|HSB} [--quiet, --debug level]
 */

enum targets_enum { tar_none, tar_at89c51snd1c, tar_at89c5131 };

enum commands_enum { com_none, com_erase, com_flash,
                     com_configure, com_get, com_dump, com_start_app };

enum configure_enum { conf_BSB = ATMEL_SET_CONFIG_BSB,
                      conf_SBV = ATMEL_SET_CONFIG_SBV,
                      conf_SSB = ATMEL_SET_CONFIG_SSB,
                      conf_EB  = ATMEL_SET_CONFIG_EB,
                      conf_HSB = ATMEL_SET_CONFIG_HSB };

enum get_enum { get_bootloader, get_ID1, get_ID2, get_BSB, get_SBV, get_SSB,
                get_EB, get_manufacturer, get_family, get_product_name,
                get_product_rev, get_HSB };

struct programmer_arguments {
    enum targets_enum target;
    enum commands_enum command;
    int  vendor_id;
    int  chip_id;
    int  debug;
    int  quiet;
    union {
        struct com_configure_struct {
            enum configure_enum name;
            int suppress_validation;
            int value;
        } com_configure_data;

        /* No special data needed for 'dump' */

        struct com_erase_struct {
            int suppress_validation;
        } com_erase_data;

        struct com_flash_struct {
            int suppress_validation;
            char original_first_char;
            char *file;
        } com_flash_data;

        struct com_get_struct {
            enum get_enum name;
        } com_get_data;
    };
};

int parse_arguments( struct programmer_arguments *args,
                     int argc,
                     char **argv );
#endif
