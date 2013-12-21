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
#include "intel_hex.h"

#define STM32_FLASH_OFFSET 0x08000000

typedef enum {
  mem_st_sector0 = 0,
  mem_st_sector1,
  mem_st_sector2,
  mem_st_sector3,
  mem_st_sector4,
  mem_st_sector5,
  mem_st_sector6,
  mem_st_sector7,
  mem_st_sector8,
  mem_st_sector9,
  mem_st_sector10,
  mem_st_sector11,
  mem_st_system,
  mem_st_otp_area,
  mem_st_option_bytes,
  mem_st_all,
} stm32_mem_sectors;


#define STM32_MEM_UNIT_NAMES "Sector 0", "Sector 1", "Sector 2", "Sector 3", \
  "Sector 4", "Sector 5", "Sector 6", "Sector 7", "Sector 8", "Sector 9", \
  "Sector 10", "Sector 11", "System Memory", "OTP Area", "Option Bytes", "all"

#define STM32_READ_PROT_ERROR   -10


int32_t stm32_erase_flash( dfu_device_t *device, dfu_bool quiet );
  /*  mass erase flash
   *  device  - the usb_dev_handle to communicate with
   *  returns status DFU_STATUS_OK if ok, anything else on error
   */

int32_t stm32_page_erase( dfu_device_t *device, uint32_t address,
    dfu_bool quiet );
  /* erase a page of memory (provide the page address) */

int32_t stm32_start_app( dfu_device_t *device, dfu_bool quiet );
  /* Reset the registers to default reset values and start application
   */

int32_t stm32_read_flash( dfu_device_t *device,
              intel_buffer_in_t *buin,
              uint8_t mem_segment,
              const dfu_bool quiet);
  /* read the flash from buin->info.data_start to data_end and place
   * in buin.data. mem_segment is the segment of memory from the
   * stm32_memory_unit_enum.
   */

int32_t stm32_write_flash( dfu_device_t *device, intel_buffer_out_t *bout,
    const dfu_bool eeprom, const dfu_bool force, const dfu_bool hide_progress );
  /* Flash data from the buffer to the main program memory on the device.
   * buffer contains the data to flash where buffer[0] is aligned with memory
   * address zero (which could be inside the bootloader and unavailable).
   * buffer[start / end] correspond to the start / end of available memory
   * outside the bootloader.
   * flash_page_size is the size of flash pages - used for alignment
   * eeprom bool tells if you want to flash to eeprom or flash memory
   * hide_progress bool sets whether to display progress
   */

int32_t stm32_get_commands( dfu_device_t *device );
  /* @brief get the commands list, should be length 4
   * @param device pointer
   * @retrn 0 on success
   */

int32_t stm32_get_configuration( dfu_device_t *device );
  /* @brief get the configuration structure
   * @param device pointer
   * @retrn 0 on success, negative for error
   */

int32_t stm32_read_unprotect( dfu_device_t *device, dfu_bool quiet );
  /* @brief unprotect the device (triggers a mass erase)
   * @param device pointer
   * @retrn 0 on success
   */



#if 0
int32_t stm32_read_config( dfu_device_t *device,
               stm32_device_info_t *info );

int32_t stm32_read_fuses( dfu_device_t *device, stm32_avr32_fuses_t * info );

int32_t stm32_set_fuse( dfu_device_t *device, const uint8_t property,
    const uint32_t value );

int32_t stm32_set_config( dfu_device_t *device, const uint8_t property,
    const uint8_t value );

int32_t stm32_blank_check( dfu_device_t *device, const uint32_t start,
    const uint32_t end, dfu_bool quiet );

int32_t stm32_secure( dfu_device_t *device );

int32_t stm32_getsecure( dfu_device_t *device );

int32_t stm32_user( dfu_device_t *device, intel_buffer_out_t *bout );

void stm32_print_device_info( FILE *stream, stm32_device_info_t *info );
#endif

#endif  /* __STM32_H__ */

// vim: shiftwidth=2
