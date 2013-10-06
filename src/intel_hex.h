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

#ifndef __INTEL_HEX_H__
#define __INTEL_HEX_H__

#include <stdint.h>

#include "atmel.h"

int32_t intel_process_data( atmel_buffer_out_t *bout,
        char value, uint32_t target_offset, uint32_t address);
/* process a data value by adding to the buffer at the appropriate address or if
 * the address is out of range do nothing and return -1. Also update the valid
 * range of data in bout
 * return 0 on success, -1 on address error
 */

// NOTE : intel_process_data should be moved to a different module dealing with
// processing any data and putting it into a buffer

int32_t intel_hex_to_buffer( char *filename, atmel_buffer_out_t *bout,
        uint32_t target_offset );
/*  Used to read in a file in intel hex format and return a chunk of
 *  memory containing the memory image described in the file.
 *
 *  \param filename the name of the intel hex file to process
 *  \param target_offset is the flash memory address location of buffer[0]
 *  \param bout buffer_out structure containing pointer to memory data for the
 *          program and for the user page.  Each is an array of int16_t's where
 *          the values 0-255 are valid memory values, and anything else
 *          indicates an unused memory location.  These do not need to be
 *          initialized before passing this parameter to the function.
 *
 *          when passed to the function, program_usage and user_usage must
 *          indicate the maximum size of each of these memory sections
 *          in bytes.  After the program has run they will indicate the
 *          amount of available memory image used for each section
 *
 *  \return success integer
 *          0 = success
 *          + = the amount of data that exists outside the specified memory
 *              area and has not been added to the buffer
 *          - = all sorts of error codes (eg, no data in flash memory, ...)
 *              if the hex file contains no valid data an error is NOT thrown
 *              but the presence of valid data can be checked using the
 *              data_start field in atmel_buffer_out_t
 */

int32_t buffer_to_intel_hex( char *filename, atmel_buffer_in_t *buin,
        uint32_t target_offset );
/*  Used to convert a buffer to an intel hex formatted file.
 *
 */

#endif
