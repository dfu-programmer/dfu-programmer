/** dfu-programmer
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

#ifndef __STM32_H__
#define __STM32_H__

#include <stdio.h>
#include <stdint.h>
#include "dfu-bool.h"
#include "dfu-device.h"

#define STM32_USER_PAGE_OFFSET  0x80800000

#define STM32_ERASE_BLOCK_0   0
#define STM32_ERASE_BLOCK_1   1
#define STM32_ERASE_BLOCK_2   2
#define STM32_ERASE_BLOCK_3   3
#define STM32_ERASE_ALL     4

#define STM32_SET_CONFIG_BSB  0
#define STM32_SET_CONFIG_SBV  1
#define STM32_SET_CONFIG_SSB  2
#define STM32_SET_CONFIG_EB   3
#define STM32_SET_CONFIG_HSB  4

#define STM32_SECURE_OFF    0     // Security bit is cleared
#define STM32_SECURE_ON     1     // Security bit is set
#define STM32_SECURE_MAYBE    2     // Call to check security bit failed

/* All values are valid if in the range of 0-255, invalid otherwise */
typedef struct {
  int16_t bootloaderVersion;  // Bootloader Version
  int16_t bootID1;      // Device boot ID 1
  int16_t bootID2;      // Device boot ID 2
  int16_t bsb;        // Boot Status Byte
  int16_t sbv;        // Software Boot Vector
  int16_t ssb;        // Software Security Byte
  int16_t eb;         // Extra Byte
  int16_t manufacturerCode;   // Manufacturer Code
  int16_t familyCode;     // Family Code
  int16_t productName;    // Product Name
  int16_t productRevision;  // Product Revision
  int16_t hsb;        // Hardware Security Byte
} stm32_device_info_t;

typedef struct {
  int32_t lock;         // Locked region
  int32_t epfl;         // External Privileged fetch lock
  int32_t bootprot;       // Bootloader protected area
  int32_t bodlevel;       // Brown-out detector trigger level
  int32_t bodhyst;      // BOD hysteresis enable
  int32_t boden;        // BOD enable state
  int32_t isp_bod_en;     // Tells the ISP to enable BOD
  int32_t isp_io_cond_en;   // ISP uses User page to launch bootloader
  int32_t isp_force;      // Start the ISP no matter what
} stm32_avr32_fuses_t;

typedef struct {
  size_t total_size;      // the total size of the buffer
  size_t  page_size;      // the size of a flash page
  uint32_t block_start;     // the start addr of a transfer
  uint32_t block_end;     // the end addr of a transfer
  uint32_t data_start;    // the first valid data addr
  uint32_t data_end;      // the last valid data addr
  uint32_t valid_start;     // the first valid memory addr
  uint32_t valid_end;     // the last valid memory addr
} stm32_buffer_info_t;

typedef struct {
  stm32_buffer_info_t info;
  uint16_t *data;
} stm32_buffer_out_t;

typedef struct {
  stm32_buffer_info_t info;
  uint8_t *data;
} stm32_buffer_in_t;


typedef enum {
  mem_st_flash,
  mem_st_eeprom,
  mem_st_security,
  mem_st_config,
  mem_st_boot,
  mem_st_sig,
  mem_st_user,
  mem_st_ram,
  mem_st_ext0,
  mem_st_ext1,
  mem_st_ext2,
  mem_st_ext3,
  mem_st_ext,
  mem_st_ext5,
  mem_st_ext6,
  mem_st_ext7,
  mem_st_extdf
} stm32_memory_unit_enum;

#define STM32_MEM_UNIT_NAMES "flash", "eeprom", "security", "config", \
  "bootloader", "signature", "user", "int_ram", "ext_cs0", "ext_cs1", \
  "ext_cs2", "ext_cs3", "ext_cs4", "ext_cs5", "ext_cs6", "ext_cs7", "ext_df"


int32_t stm32_erase_flash( dfu_device_t *device, const uint8_t mode,
                           dfu_bool quiet );
  /*  stm32_erase_flash
   *  device  - the usb_dev_handle to communicate with
   *  mode    - the mode to use when erasing flash
   *        STM32_ERASE_ALL
   *  returns status DFU_STATUS_OK if ok, anything else on error
   */

int32_t stm32_start_app( dfu_device_t *device, dfu_bool quiet );
  /* Reset the registers to default reset values and start application
   */


int32_t stm32_init_buffer_out(stm32_buffer_out_t *bout,
    size_t total_size, size_t page_size );
/* intialize a buffer used to send data to flash memory
 * the total size and page size must be provided.
 * the data array is filled with 0xFFFF (an invalid memory
 * value) indicating that it is unassigned. data start and
 * data end are initialized with UINT32_MAX indicating there
 * is no valid data in the buffer.  these two values are simply
 * convenience values so the start and end of data do not need
 * to be found multiple times.
 */

int32_t stm32_init_buffer_in(stm32_buffer_in_t *buin,
    size_t total_size, size_t page_size );
/* initialize a buffer_in, used for reading the contents of program
 * memory.  total memory size must be provided.  the data array is filled
 * with 0xFF, which is unprogrammed memory.
 */

int32_t stm32_validate_buffer(stm32_buffer_in_t *buin,
                stm32_buffer_out_t *bout, dfu_bool quiet);
/* compare the contents of buffer_in with buffer_out to check that a target
 * memory image matches with a memory read.
 * return 0 for full validation, positive number if data bytes outside region do
 * not validate, negative number if bytes inside region that do not validate
 */

int32_t stm32_read_config( dfu_device_t *device,
               stm32_device_info_t *info );
/*  stm32_read_config reads in all of the configuration and Manufacturer
 *  Information into the stm32_device_info data structure for easier use later.
 *
 *  device  - the usb_dev_handle to communicate with
 *  info    - the data structure to populate
 *
 *  returns 0 if successful, < 0 if not
 */

int32_t stm32_read_fuses( dfu_device_t *device,
              stm32_avr32_fuses_t * info );

int32_t stm32_set_fuse( dfu_device_t *device,
              const uint8_t property,
              const uint32_t value );

int32_t stm32_set_config( dfu_device_t *device,
              const uint8_t property,
              const uint8_t value );

int32_t stm32_read_flash( dfu_device_t *device,
              stm32_buffer_in_t *buin,
              uint8_t mem_segment,
              const dfu_bool quiet);
/* read the flash from buin->info.data_start to data_end and place
 * in buin.data. mem_segment is the segment of memory from the
 * stm32_memory_unit_enum.
 */

int32_t stm32_blank_check( dfu_device_t *device,
               const uint32_t start,
               const uint32_t end,
               dfu_bool quiet );
/* check if memory between start byte and end byte (inclusive) is blank
 * returns 0 for success, < 0 for communication errors, > 0 for not blank
 */

int32_t stm32_secure( dfu_device_t *device );

int32_t stm32_getsecure( dfu_device_t *device );

int32_t stm32_flash( dfu_device_t *device,
           stm32_buffer_out_t *bout,
           const dfu_bool eeprom,
           const dfu_bool force,
           const dfu_bool hide_progress );
/* Flash data from the buffer to the main program memory on the device.
 * buffer contains the data to flash where buffer[0] is aligned with memory
 * address zero (which could be inside the bootloader and unavailable).
 * buffer[start / end] correspond to the start / end of available memory
 * outside the bootloader.
 * flash_page_size is the size of flash pages - used for alignment
 * eeprom bool tells if you want to flash to eeprom or flash memory
 * hide_progress bool sets whether to display progress
 */

int32_t stm32_user( dfu_device_t *device,
          stm32_buffer_out_t *bout );
/* Flash data to the user page.  Provide the buffer and the size of
 * flash pages for the device (this is the size of the user page).
 * Note that only the entire user page can be flashed because it is
 * deleted before it is flashed to.  Therfore buffer must fill this
 * space (start at zero and contain page_size bytes).
 */

void stm32_print_device_info( FILE *stream, stm32_device_info_t *info );

#endif  /* __STM32_H__ */

// vim: shiftwidth=2
