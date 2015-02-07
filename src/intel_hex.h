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
#include "dfu-bool.h"

typedef struct {
    size_t total_size;          // the total size of the buffer
    size_t  page_size;          // the size of a flash page
    uint32_t block_start;       // the start addr of a transfer
    uint32_t block_end;         // the end addr of a transfer
    uint32_t data_start;        // the first valid data addr
    uint32_t data_end;          // the last valid data addr
    uint32_t valid_start;       // the first valid memory addr
    uint32_t valid_end;         // the last valid memory addr
} intel_buffer_info_t;

typedef struct {
    intel_buffer_info_t info;
    uint16_t *data;
} intel_buffer_out_t;

typedef struct {
    intel_buffer_info_t info;
    uint8_t *data;
} intel_buffer_in_t;


int32_t intel_process_data( intel_buffer_out_t *bout,
        char value, uint32_t target_offset, uint32_t address);
/* process a data value by adding to the buffer at the appropriate address or if
 * the address is out of range do nothing and return -1. Also update the valid
 * range of data in bout
 * return 0 on success, -1 on address error
 */

// NOTE : intel_process_data should be moved to a different module dealing with
// processing any data and putting it into a buffer

int32_t intel_hex_to_buffer( char *filename, intel_buffer_out_t *bout,
        uint32_t target_offset, dfu_bool quiet );
/*  Used to read in a file in intel hex format and return a chunk of
 *  memory containing the memory image described in the file.
 *
 *  \param filename the name of the intel hex file to process
 *  \param target_offset is the flash memory address location of buffer[0]
 *  \param quiet tells fcn to suppress termninal messages
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
 *              data_start field in intel_buffer_out_t
 */

int32_t intel_hex_from_buffer( intel_buffer_in_t *buin,
        dfu_bool force_full, uint32_t target_offset );
/*  Used to convert a buffer to an intel hex formatted file.
 *  target offset is the address location of buffer 0
 *  force_full sets whether to keep writing entirely blank pages.
 */

int32_t intel_init_buffer_out(intel_buffer_out_t *bout,
        size_t total_size, size_t page_size );
/* initialize a buffer used to send data to flash memory
 * the total size and page size must be provided.
 * the data array is filled with 0xFFFF (an invalid memory
 * value) indicating that it is unassigned. data start and
 * data end are initialized with UINT32_MAX indicating there
 * is no valid data in the buffer.  these two values are simply
 * convenience values so the start and end of data do not need
 * to be found multiple times.
 */

int32_t intel_init_buffer_in(intel_buffer_in_t *buin,
        size_t total_size, size_t page_size );
/* initialize a buffer_in, used for reading the contents of program
 * memory.  total memory size must be provided.  the data array is filled
 * with 0xFF, which is unprogrammed memory.
 */

int32_t intel_validate_buffer(  intel_buffer_in_t *buin,
                                intel_buffer_out_t *bout, dfu_bool quiet);
/* compare the contents of buffer_in with buffer_out to check that a target
 * memory image matches with a memory read.
 * return 0 for full validation, positive number if data bytes outside region do
 * not validate, negative number if bytes inside region that do not validate
 */

int32_t intel_flash_prep_buffer( intel_buffer_out_t *bout );
/* prepare the buffer so that valid data fills each page that contains data.
 * unassigned data in buffer is given a value of 0xff (blank memory)
 * the buffer pointer must align with the beginning of a flash page
 * return 0 on success, -1 if assigning data would extend flash above size
 */

#endif
