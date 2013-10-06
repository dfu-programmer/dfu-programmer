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

// define structure containing information about the hex to buffer conversion
struct buffer_out {
    int16_t *prog_data;
    int16_t *user_data;
    uint32_t prog_usage;
    uint32_t user_usage;
};


/*  Used to read in a file in intel hex format and return a chunk of
 *  memory containing the memory image described in the file.
 *
 *  \param filename the name of the intel hex file to process
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
 *  \return success integer (0 on success, anything else is no good..)
 */

int32_t intel_hex_to_buffer( char *filename, struct buffer_out *bout );
#endif
