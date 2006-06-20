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

#ifndef __ATMEL_H__
#define __ATMEL_H__

#include <stdio.h>
#include <usb.h>

#define ATMEL_ERASE_BLOCK_0     0
#define ATMEL_ERASE_BLOCK_1     1
#define ATMEL_ERASE_BLOCK_2     2
#define ATMEL_ERASE_BLOCK_3     3
#define ATMEL_ERASE_ALL         4

#define ATMEL_SET_CONFIG_BSB    0
#define ATMEL_SET_CONFIG_SBV    1
#define ATMEL_SET_CONFIG_SSB    2
#define ATMEL_SET_CONFIG_EB     3
#define ATMEL_SET_CONFIG_HSB    4

/* All values are valid if in the range of 0-255, invalid otherwise */
struct atmel_device_info {
    short bootloaderVersion;    // Bootloader Version
    short bootID1;              // Device boot ID 1
    short bootID2;              // Device boot ID 2
    short bsb;                  // Boot Status Byte
    short sbv;                  // Software Boot Vector
    short ssb;                  // Software Security Byte
    short eb;                   // Extra Byte
    short manufacturerCode;     // Manufacturer Code
    short familyCode;           // Family Code
    short productName;          // Product Name
    short productRevision;      // Product Revision
    short hsb;                  // Hardware Security Byte
};

void atmel_init( void );
int atmel_read_config_8051( struct usb_dev_handle *device,
                       const int interface,
                       struct atmel_device_info *info );
int atmel_read_config_avr( struct usb_dev_handle *device,
                       const int interface,
                       struct atmel_device_info *info );
int atmel_erase_flash( struct usb_dev_handle *device,
                       const int interface,
                       const unsigned char mode );
int atmel_set_config( struct usb_dev_handle *device,
                      const int interface,
                      const unsigned char property,
                      const unsigned char value );
int atmel_read_flash( struct usb_dev_handle *device,
                      const int interface,
                      const u_int32_t start,
                      const u_int32_t end,
                      char* buffer,
                      int buffer_len );
int atmel_blank_check( struct usb_dev_handle *device,
                      const int interface,
                      const u_int32_t start,
                      const u_int32_t end );
int atmel_reset( struct usb_dev_handle *device,
                 const int interface );
int atmel_flash( struct usb_dev_handle *device,
                 const int interface,
                 const u_int32_t start,
                 const u_int32_t end,
                 char* buffer );
int atmel_start_app( struct usb_dev_handle *device,
                     const int interface );

void atmel_print_device_info( FILE *stream, struct atmel_device_info *info );
#endif
