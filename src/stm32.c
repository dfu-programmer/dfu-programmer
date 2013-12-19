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
  * along with this program; if not, write to the Free Software Foundation,
  * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
  */

//___ I N C L U D E S ________________________________________________________
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>

#include "dfu-bool.h"
#include "dfu-device.h"
#include "config.h"
#include "arguments.h"
#include "dfu.h"
#include "stm32.h"
#include "util.h"


//___ M A C R O S   ( P R I V A T E ) ________________________________________
#define STM32_DEBUG_THRESHOLD   50
#define STM32_TRACE_THRESHOLD   55

#define DEBUG(...)  dfu_debug( __FILE__, __FUNCTION__, __LINE__, STM32_DEBUG_THRESHOLD, __VA_ARGS__ )
#define TRACE(...)  dfu_debug( __FILE__, __FUNCTION__, __LINE__, STM32_TRACE_THRESHOLD, __VA_ARGS__ )

#define STM32_MAX_TRANSFER_SIZE     0x0800  /* 2048 */
#define STM32_MIN_SECTOR_BOUND      0x4000  /* 16 kb */

#define SET_ADDR_PTR            0x21
#define ERASE_CMD               0x41
#define READ_UNPROTECT          0x92
#define GET_CMD                 0x00

#define STM32_64KB_PAGE             0x10000
#define STM32_FOOTER_SIZE           16
#define STM32_CONTROL_BLOCK_SIZE    64
#define STM32_MAX_FLASH_BUFFER_SIZE (STM32_MAX_TRANSFER_SIZE + 2 * STM32_CONTROL_BLOCK_SIZE + STM32_FOOTER_SIZE)

//___ T Y P E D E F S   ( P R I V A T E ) ____________________________________

//___ P R O T O T Y P E S   ( P R I V A T E ) ________________________________
static inline int32_t stm32_get_status( dfu_device_t *device );
  /* run dfu_get_status to get the current status
   * retrn  0 on status OK, -1 on status req fail, -2 on bad status
   */

static int32_t stm32_set_address_ptr( dfu_device_t *device, uint32_t address );
  /* @brief set the address pointer to a certain address
   * @param the address to set
   * @retrn 0 on success, negative on failure
   */

static int32_t __stm32_flash_block( dfu_device_t *device,
                                    intel_buffer_out_t *bout );
  /* flash the contents of memory into a block of memory.  it is assumed that
   * the appropriate page has already been selected.  start and end are the
   * start and end addresses of the flash data.  returns 0 on success,
   * positive dfu error code if one is obtained, or negative if communitcation
   * with device fails.
   */

static int32_t stm32_select_memory_unit( dfu_device_t *device,
    stm32_mem_sectors unit );
   /* select a memory unit from the following list (enumerated)
    * flash, eeprom, security, configuration, bootloader, signature, user page
    */

static int32_t stm32_select_page( dfu_device_t *device,
                  const uint16_t mem_page );
  /* select a page in memory, numbering starts with 0, pages are
   * 64kb pages (0x10000 bytes).  Select page when the memory unit
   * is set to the user page will cause an error.
   */

static int32_t __stm32_read_block( dfu_device_t *device,
                   intel_buffer_in_t *buin,
                   const dfu_bool eeprom );
  /* assumes block does not cross 64 b page boundaries and ideally alligs
   * with flash pages. appropriate memory type and 64kb page has already
   * been selected, max transfer size is not violated it updates the buffer
   * data between data_start and data_end
   */

static inline void __print_progress( intel_buffer_info_t *info,
                    uint32_t *progress );
  /* calculate how many progress indicator steps to print and print them
   * update progress value
   */


//___ V A R I A B L E S ______________________________________________________
extern int debug;

static const uint32_t stm32_sector_addresses[] = {
  0x08000000,   /* sector  0,  16 kb */
  0x08004000,   /* sector  1,  16 kb */
  0x08008000,   /* sector  2,  16 kb */
  0x0800C000,   /* sector  3,  16 kb */
  0x08010000,   /* sector  4,  64 kb */
  0x08020000,   /* sector  5, 128 kb */
  0x08040000,   /* sector  6, 128 kb */
  0x08060000,   /* sector  7, 128 kb */
  0x08080000,   /* sector  8, 128 kb */
  0x080A0000,   /* sector  9, 128 kb */
  0x080C0000,   /* sector 10, 128 kb */
  0x080E0000,   /* sector 11, 128 kb */
  0x1FFF0000,   /* system memory, 30 kb */
  0x1FFF7800,   /* OTP area, 528 bytes */
  0x1FFFC000,   /* Option bytes, 16 bytes */
};

//___ F U N C T I O N S   ( P R I V A T E ) __________________________________
static inline int32_t stm32_get_status( dfu_device_t *device ) {
  dfu_status_t status;
  if( 0 == dfu_get_status(device, &status) ) {
    if( status.bStatus == DFU_STATUS_OK ) {
      DEBUG( "Status OK\n" );
    } else {
      DEBUG( "Status %s not OK, use DFU_CLRSTATUS\n",
          dfu_status_to_string(status.bStatus) );
      dfu_clear_status( device );
      return -2;
    }
  } else {
    DEBUG( "DFU_GETSTATUS request failed\n" );
    return -1;
  }

  return 0;
}

static int32_t stm32_set_address_ptr( dfu_device_t *device, uint32_t address ) {
  TRACE( "%s( 0x%X )\n", __FUNCTION__, address );
  const uint8_t length = 5;
  int32_t status;

  uint8_t command[] = {
    (uint8_t) SET_ADDR_PTR,
    (uint8_t) address & 0xFF,           /* address LSB */
    (uint8_t) (address>>8) & 0xFF,
    (uint8_t) (address>>16) & 0xFF,
    (uint8_t) (address>>24) & 0xFF      /* address MSB */
  };

  /* check dfu status for okay to send */
  if( (status = stm32_get_status(device)) ) {
    DEBUG("Error %d getting status on start\n", status);
    return -1;
  }

  dfu_set_transaction_num( 0 );     /* set wValue to zero */
  if( length != dfu_download(device, length, command) ) {
    DEBUG( "dfu_download failed\n" );
    return -2;
  }

  /* call dfu get status to trigger command */
  if( (status = stm32_get_status(device)) ) {
    DEBUG("Error %d triggering %s\n", status, __FUNCTION__);
    return -3;
  }

  /* check command success */
  if( (status = stm32_get_status(device)) ) {
    DEBUG("Error %d: %s unsuccessful\n", status, __FUNCTION__);
    return -4;
  }

  return 0;
}

static int32_t __stm32_flash_block( dfu_device_t *device,
                                    intel_buffer_out_t *bout ) {
  TRACE( "%s( %p, %p )\n", __FUNCTION__, device, bout );
  const size_t length = bout->info.block_end - bout->info.block_start + 1;
  int16_t i;
  uint8_t message[STM32_MAX_TRANSFER_SIZE];
  int32_t status;

  /* check input args */
  if( (NULL == device) || (NULL == bout) ) {
    DEBUG( "ERROR: Invalid arguments, device/buffer pointer is NULL.\n" );
    return -1;
  } else if ( bout->info.block_start > bout->info.block_end ) {
    DEBUG( "ERROR: End address 0x%X before start address 0x%X.\n",
        bout->info.block_end, bout->info.block_start );
    return -1;
  } else if ( length > STM32_MAX_TRANSFER_SIZE ) {
    DEBUG( "ERROR: 0x%X byte message > MAX TRANSFER SIZE (0x%X).\n",
        length, STM32_MAX_TRANSFER_SIZE );
    return -1;
  }

  for( i = 0; i < length; i++ ) {
    message[i] = (uint8_t) bout->data[bout->info.block_start + i];
  }

  if( length != dfu_download(device, length, message) ) {
    DEBUG( "dfu_download failed\n" );
    return -3;
  }

  /* call dfu get status to trigger command */
  if( (status = stm32_get_status(device)) ) {
    DEBUG("Error %d triggering %s\n", status, __FUNCTION__);
    return -3;
  }

  /* check that the command was successfully executed */
  if( (status = stm32_get_status(device)) ) {
    DEBUG("Error %d: %s unsuccessful\n", status, __FUNCTION__);
    return -4;
  }

  return 0;
}

static int32_t stm32_select_memory_unit( dfu_device_t *device,
                stm32_mem_sectors unit ) {
  TRACE( "%s( %p, %d )\n", __FUNCTION__, device, unit );

  uint8_t command[4] = { 0x06, 0x03, 0x00, (0xFF & unit) };
  dfu_status_t status;
  char *mem_names[] = { STM32_MEM_UNIT_NAMES };

  // input parameter checks
  if( NULL == device) {
    DEBUG ( "ERROR: Device pointer is NULL.\n" );
    return -1;
  }

  // check compatiblity with various devices
  if( !(GRP_AVR32 & device->type) ) {
    DEBUG( "Ignore Select Memory Unit for non GRP_AVR32 device.\n" );
    return 0;
  } else if ( (ADC_AVR32 & device->type) && !( unit > mem_st_all ) ) {
    DEBUG( "%d is not a valid memory unit for AVR32 devices.\n", unit );
    fprintf( stderr, "Invalid Memory Unit Selection.\n" );
    return -1;
  }

  // select memory unit         below is OK bc unit < len(mem_names)
  DEBUG( "Selecting %s memory unit.\n", mem_names[unit] );
  if( 4 != dfu_download(device, 4, command) ) {
    DEBUG( "stm32_select_memory_unit 0x%02X dfu_download failed.\n", unit );
    return -2;
  }

  // check that memory section was selected
  if( 0 != dfu_get_status(device, &status) ) {
    DEBUG( "DFU_GETSTATUS failed after stm32_select_memory_unit.\n" );
    return -3;
  }

  // if error, report and clear
  if( DFU_STATUS_OK != status.bStatus ) {
    DEBUG( "Error: status (%s) was not OK.\n",
      dfu_status_to_string(status.bStatus) );
    if ( STATE_DFU_ERROR == status.bState ) {
      dfu_clear_status( device );
    }
    return -4;
  }

  return 0;
}

static int32_t stm32_select_page( dfu_device_t *device,
                                  const uint16_t mem_page ) {
  TRACE( "%s( %p, %u )\n", __FUNCTION__, device, mem_page );
  dfu_status_t status;

  if( NULL == device ) {
    DEBUG ( "ERROR: Device pointer is NULL.\n" );
    return -2;
  }

  if ( ADC_8051 & device->type ) {
    DEBUG( "Select page not implemented for 8051 device, ignoring.\n" );
    return 0;
  }

  DEBUG( "Selecting page %d, address 0x%X.\n",
      mem_page, STM32_64KB_PAGE * mem_page );

  if( GRP_AVR32 & device->type ) {
    uint8_t command[5] = { 0x06, 0x03, 0x01, 0x00, 0x00 };
    command[3] = 0xff & (mem_page >> 8);
    command[4] = 0xff & mem_page;

    if( 5 != dfu_download(device, 5, command) ) {
      DEBUG( "stm32_select_page DFU_DNLOAD failed.\n" );
      return -1;
    }
  } else if( ADC_AVR == device->type ) {    // AVR but not 8051
    uint8_t command[4] = { 0x06, 0x03, 0x00, 0x00 };
    command[3] = 0xff & mem_page;

    if( 4 != dfu_download(device, 4, command) ) {
      DEBUG( "stm32_select_page DFU_DNLOAD failed.\n" );
      return -1;
    }
  }

  // check that page number was set
  if( 0 != dfu_get_status(device, &status) ) {
    DEBUG( "stm32_select_page DFU_GETSTATUS failed.\n" );
    return -3;
  }

  // if error, report and clear
  if( DFU_STATUS_OK != status.bStatus ) {
    DEBUG( "Error: status (%s) was not OK.\n",
      dfu_status_to_string(status.bStatus) );
    if ( STATE_DFU_ERROR == status.bState ) {
      dfu_clear_status( device );
    }
    return -4;
  }

  return 0;
}

static int32_t __stm32_read_block( dfu_device_t *device,
                   intel_buffer_in_t *buin,
                   const dfu_bool eeprom ) {
  uint8_t command[6] = { 0x03, 0x00, 0x00, 0x00, 0x00, 0x00 };
  int32_t result;

  if( buin->info.block_end < buin->info.block_start ) {
    // this would cause a problem bc read length could be way off
    DEBUG("ERROR: start address is after end address.\n");
    return -1;
  } else if( buin->info.block_end - buin->info.block_start + 1 > STM32_MAX_TRANSFER_SIZE ) {
    // this could cause a read problem
    DEBUG("ERROR: transfer size must not exceed %d.\n",
        STM32_MAX_TRANSFER_SIZE );
    return -1;
  }

  // AVR/8051 requires 0x02 here to read eeprom, XMEGA requires 0x00.
  if( true == eeprom && (GRP_AVR & device->type) ) {
    command[1] = 0x02;
  }

  command[2] = 0xff & (buin->info.block_start >> 8);
  command[3] = 0xff & (buin->info.block_start);
  command[4] = 0xff & (buin->info.block_end >> 8);
  command[5] = 0xff & (buin->info.block_end);

  if( 6 != dfu_download(device, 6, command) ) {
    DEBUG( "dfu_download failed\n" );
    return -1;
  }

  result = dfu_upload( device, buin->info.block_end - buin->info.block_start + 1,
                &buin->data[buin->info.block_start] );
  if( result < 0) {
    dfu_status_t status;

    DEBUG( "dfu_upload result: %d\n", result );
    if( 0 == dfu_get_status(device, &status) ) {
      if( DFU_STATUS_ERROR_FILE == status.bStatus ) {
        fprintf( stderr,
              "The device is read protected.\n" );
      } else {
        fprintf( stderr, "Unknown error. Try enabling debug.\n" );
      }
    } else {
      fprintf( stderr, "Device is unresponsive.\n" );
    }
    dfu_clear_status( device );

    return result;
  }

  return 0;
}

static inline void __print_progress( intel_buffer_info_t *info,
                    uint32_t *progress ) {
  if ( !(debug > STM32_DEBUG_THRESHOLD) ) {
    while ( ((info->block_end - info->data_start + 1) * 32) > *progress ) {
      fprintf( stderr, ">" );
      *progress += info->data_end - info->data_start + 1;
    }
  }
}


//___ F U N C T I O N S ______________________________________________________
int32_t stm32_erase_flash( dfu_device_t *device, stm32_mem_sectors mem,
                           dfu_bool quiet ) {
  TRACE( "%s( %p, %d )\n", __FUNCTION__, device, mem );
  uint8_t command[5] = { ERASE_CMD, 0x00, 0x00, 0x00, 0x00 };
  uint8_t length = 0;
  int32_t status;

  switch( mem ) {
    case mem_st_all:
      length = 1;
      break;
    case mem_st_sector0:
      command[1] = 0x00;    //  page LSB
      command[2] = 0x00;
      command[3] = 0x00;
      command[4] = 0x00;    // page MSB
      length = 5;
      break;
    default:
      DEBUG("Unknown mem sector %u\n", mem);
      return -1;
  }

  /* check dfu status for ok to send */
  if( (status = stm32_get_status(device)) ) {
    DEBUG("Error %d getting status on start\n", status);
    return -1;
  }

  if( !quiet ) {
    fprintf( stderr, "Erasing flash...  " );
    DEBUG("\n");
  }
  dfu_set_transaction_num( 0 );     /* set wValue to zero */
  if( length != dfu_download(device, length, command) ) {
    if( !quiet ) fprintf( stderr, "ERROR\n" );
    DEBUG( "dfu_download failed\n" );
    return -3;
  }

  /* call dfu get status to trigger command */
  if( (status = stm32_get_status(device)) ) {
    DEBUG("Error %d triggering %s\n", status, __FUNCTION__);
    return -3;
  }

  /* It can take a while to erase the chip so 10 seconds before giving up */
  if( (status = stm32_get_status(device)) ) {
    DEBUG("Error %d: %s unsuccessful\n", status, __FUNCTION__);
    return -4;
  }

  return 0;
}

int32_t stm32_start_app( dfu_device_t *device, dfu_bool quiet ) {
  TRACE( "%s( %p )\n", __FUNCTION__, device );
  int32_t status;

  /* set address pointer (jump target) to start address */
  if( (status = stm32_set_address_ptr( device, STM32_FLASH_OFFSET )) ) {
    DEBUG("Error setting address pointer\n");
    return -1;
  }

  /* check dfu status for ok to send */
  if( (status = stm32_get_status(device)) ) {
    DEBUG("Error %d getting status on start\n", status);
    return -1;
  }

  if( !quiet ) fprintf( stderr, "Launching program...  " );
  dfu_set_transaction_num( 0 );     /* set wValue to zero */
  if( 0 != dfu_download(device, 0, NULL) ) {
    if( !quiet ) fprintf( stderr, "ERROR\n" );
    DEBUG( "dfu_download failed\n" );
    return -3;
  }

  /* call dfu get status to trigger command */
  if( (status = stm32_get_status(device)) ) {
    DEBUG("Error %d triggering %s\n", status, __FUNCTION__);
    return -3;
  }

  return 0;
}

int32_t stm32_read_flash( dfu_device_t *device, intel_buffer_in_t *buin,
    const uint8_t mem_segment, const dfu_bool quiet ) {
  uint8_t mem_page = 0;       // tracks the current memory page
  uint32_t progress = 0;      // used to indicate progress
  int32_t result = 0;
  // TODO : use status instead of result
  int32_t retval = -1;      // the return value for this function

  TRACE( "%s( %p, %p, %u, %s )\n", __FUNCTION__, device, buin,
      mem_segment, ((true == quiet) ? "true" : "false"));

  if( (NULL == buin) || (NULL == device) ) {
    DEBUG( "invalid arguments.\n" );
    if( !quiet )
      fprintf( stderr, "Program Error, use debug for more info.\n" );
    return -1;
  }

  // For the AVR32/XMEGA chips, select the flash space. (safe for all parts)
  if( 0 != stm32_select_memory_unit(device, mem_segment) ) {
    DEBUG ("Error selecting memory unit.\n");
    if( !quiet )
      fprintf( stderr, "Memory access error, use debug for more info.\n" );
    return -3;
  }

  if( !quiet ) {
    if( debug <= STM32_DEBUG_THRESHOLD ) {
      // NOTE: From here on we should go to finally on error
      fprintf( stderr, "[================================] " );
    }
    fprintf( stderr, "Reading 0x%X bytes...\n",
        buin->info.data_end - buin->info.data_start + 1 );
    if( debug <= STM32_DEBUG_THRESHOLD ) {
      // NOTE: From here on we should go to finally on error
      fprintf( stderr, "[" );
    }
  }

  // select the first memory page ( not safe for mem_st_user )
  buin->info.block_start = buin->info.data_start;
  mem_page = buin->info.block_start / STM32_64KB_PAGE;
  if( 0 != (result = stm32_select_page( device, mem_page )) ) {
    DEBUG( "ERROR selecting 64kB page %d.\n", result );
    retval = -3;
    goto finally;
  }

  while (buin->info.block_start <= buin->info.data_end) {
    // ensure the memory page is correct
    if ( buin->info.block_start / STM32_64KB_PAGE != mem_page ) {
      mem_page = buin->info.block_start / STM32_64KB_PAGE;
      if( 0 != (result = stm32_select_page( device, mem_page )) ) {
        DEBUG( "ERROR selecting 64kB page %d.\n", result );
        retval = -3;
      }
      // check if the entire page is blank ()
    }

    // find end value for the current transfer
    buin->info.block_end = buin->info.block_start +
      STM32_MAX_TRANSFER_SIZE - 1;
    if ( buin->info.block_end / STM32_64KB_PAGE > mem_page ) {
      buin->info.block_end = STM32_64KB_PAGE * mem_page - 1;
    }
    if ( buin->info.block_end > buin->info.data_end ) {
      buin->info.block_end = buin->info.data_end;
    }

    if( 0 != (result = __stm32_read_block(device, buin, 0))) {
      DEBUG( "Error reading block 0x%X to 0x%X: err %d.\n",
          buin->info.block_start, buin->info.block_end, result );
      retval = -5;
      goto finally;
    }

    buin->info.block_start = buin->info.block_end + 1;
    if ( !quiet ) __print_progress( &buin->info, &progress );
  }
  retval = 0;

finally:
  if ( !quiet ) {
    if( 0 == retval ) {
      if ( debug <= STM32_DEBUG_THRESHOLD ) {
        fprintf( stderr, "] " );
      }
      fprintf( stderr, "SUCCESS\n" );
    } else {
      if ( debug <= STM32_DEBUG_THRESHOLD ) {
        fprintf( stderr, " X  ");
      }
      fprintf( stderr, "ERROR\n" );
      if( retval==-3 )
        fprintf( stderr,
            "Memory access error, use debug for more info.\n" );
      else if( retval==-5 )
        fprintf( stderr,
            "Memory read error, use debug for more info.\n" );
    }
  }
  return retval;
}

int32_t stm32_write_flash( dfu_device_t *device, intel_buffer_out_t *bout,
    const dfu_bool eeprom, const dfu_bool force, const dfu_bool quiet ) {
  TRACE( "%s( %p, %p, %s, %s )\n", __FUNCTION__, device, bout,
          ((true == eeprom) ? "true" : "false"),
          ((true == quiet) ? "true" : "false") );

  uint32_t i;
  uint32_t progress = 0;    // keep record of sent progress as bytes * 32
  uint32_t address_offset;  // keep record of sent progress as bytes * 32
  uint8_t  reset_address_flag;  // reset address offset required
  uint16_t  xfer_size = 0;      // the size of a transfer
  uint8_t mem_section = 0;   // tracks the current memory page
  int32_t retval = -1;  // the return value for this function
  int32_t status;

  /* check arguments */
  if( (NULL == device) || (NULL == bout) ) {
    DEBUG( "ERROR: Invalid arguments, device/buffer pointer is NULL.\n" );
    if( !quiet )
      fprintf( stderr, "Program Error, use debug for more info.\n" );
    return -1;
  } else if( bout->info.valid_start > bout->info.valid_end ) {
    DEBUG( "ERROR: No valid target memory, end 0x%X before start 0x%X.\n",
        bout->info.valid_end, bout->info.valid_start );
    if( !quiet )
      fprintf( stderr, "Program Error, use debug for more info.\n" );
    return -1;
  }

  /* for each page with data, fill unassigned values on the page with 0xFF
   * bout->data[0] always aligns with a flash page boundary irrespective
   * of where valid_start is located */
  if( 0 != intel_flash_prep_buffer( bout ) ) {
    if( !quiet )
      fprintf( stderr, "Program Error, use debug for more info.\n" );
    return -2;
  }

  /* determine the limits of where actual data resides in the buffer */
  bout->info.data_start = UINT32_MAX;
  for( i = 0; i < bout->info.total_size; i++ ) {
    if( bout->data[i] <= UINT8_MAX ) {
      bout->info.data_end = i;
      if( bout->info.data_start == UINT32_MAX )
        bout->info.data_start = i;
    }
  }

  /* debug info about data limits */
  DEBUG("Flash available from 0x%X to 0x%X (64kB p. %u to %u), 0x%X bytes.\n",
      bout->info.valid_start, bout->info.valid_end,
      bout->info.valid_start / STM32_64KB_PAGE,
      bout->info.valid_end / STM32_64KB_PAGE,
      bout->info.valid_end - bout->info.valid_start + 1); // bytes inclusive so +1
  DEBUG("Data start @ 0x%X: 64kB p %u; %uB p 0x%X + 0x%X offset.\n",
      bout->info.data_start, bout->info.data_start / STM32_64KB_PAGE,
      bout->info.page_size, bout->info.data_start / bout->info.page_size,
      bout->info.data_start % bout->info.page_size);
  DEBUG("Data end @ 0x%X: 64kB p %u; %uB p 0x%X + 0x%X offset.\n",
      bout->info.data_end, bout->info.data_end / STM32_64KB_PAGE,
      bout->info.page_size, bout->info.data_end / bout->info.page_size,
      bout->info.data_end % bout->info.page_size);
  DEBUG("Totals: 0x%X bytes, %u %uB pages, %u 64kB byte pages.\n",
      bout->info.data_end - bout->info.data_start + 1,
      bout->info.data_end/bout->info.page_size - bout->info.data_start/bout->info.page_size + 1,
      bout->info.page_size,
      bout->info.data_end/STM32_64KB_PAGE - bout->info.data_start/STM32_64KB_PAGE + 1 );

  /* more error checking */
  if( (bout->info.data_start < bout->info.valid_start) ||
      (bout->info.data_end > bout->info.valid_end) ) {
    DEBUG( "ERROR: Data exists outside of the valid target flash region.\n" );
    if( !quiet )
      fprintf( stderr, "Hex file error, use debug for more info.\n" );
    return -1;
  } else if( bout->info.data_start == UINT32_MAX ) {
    DEBUG( "ERROR: No valid data to flash.\n" );
    if( !quiet )
      fprintf( stderr, "Hex file error, use debug for more info.\n" );
    return -1;
  }

  if( !quiet ) {
    if( debug <= STM32_DEBUG_THRESHOLD ) {
      /* NOTE: from here on we should run finally block */
      fprintf( stderr, "[================================] " );
    }
    fprintf( stderr, "Programming 0x%X bytes...\n",
        bout->info.data_end - bout->info.data_start + 1 );
    if( debug <= STM32_DEBUG_THRESHOLD ) {
      /* NOTE: from here on we need to run finally block */
      fprintf( stderr, "[" );
    }
  }

  /* program the data */
  bout->info.block_start = bout->info.data_start;
  reset_address_flag = 1;

  while( bout->info.block_start <= bout->info.data_end ) {
    if( reset_address_flag ) {
      address_offset = bout->info.block_start;
      if( (status = stm32_set_address_ptr(device,
              STM32_FLASH_OFFSET + address_offset)) ) {
        DEBUG("Error setting address 0x%X\n", address_offset);
        retval = -3;
        goto finally;
      }
      dfu_set_transaction_num( 2 ); /* sets block offset 0 */
      reset_address_flag = 0;
    }

    /* find end address (info.block_end) for data section to write */
    mem_section = bout->info.block_start / STM32_MIN_SECTOR_BOUND;
    for( bout->info.block_end = bout->info.block_start;
         bout->info.block_end <= bout->info.data_end;
         bout->info.block_end++ ) {
      xfer_size = bout->info.block_end - bout->info.block_start + 1;
      // check if the current value is valid
      if( bout->data[bout->info.block_end] > UINT8_MAX ) break;
      // check if the current data packet is too big
      if( xfer_size > STM32_MAX_TRANSFER_SIZE ) break;
      // check if the current data value is outside of the memory sector
      if( bout->info.block_end / STM32_MIN_SECTOR_BOUND - mem_section ) break;
    }
    bout->info.block_end--; // bout->info.block_end was one step beyond the last data value to flash
    xfer_size--;
    if( xfer_size != STM32_MAX_TRANSFER_SIZE ) {
      DEBUG("xfer_size change, need addr reset\n");
      reset_address_flag = 1;
    }

    /* write the data */
    DEBUG("Program data block: 0x%X to 0x%X, 0x%X bytes.\n",
        bout->info.block_start, bout->info.block_end, xfer_size);

    if( (status = __stm32_flash_block( device, bout )) ) {
      DEBUG( "Error flashing the block: err %d.\n", status );
      retval = -4;
      goto finally;
    }

    // incrment bout->info.block_start to the next valid address
    for( bout->info.block_start = bout->info.block_end + 1;
         bout->info.block_start <= bout->info.data_end;
         bout->info.block_start++ ) {
      if( (bout->data[bout->info.block_start] <= UINT8_MAX) ) break;
    } // bout->info.block_start is now on the first valid data for the next segment

    if( reset_address_flag == 0 && (bout->info.block_start !=
        (STM32_MAX_TRANSFER_SIZE * (dfu_get_transaction_num() - 2))
        + address_offset) ) {
      DEBUG("block start does not match addr, reset req\n");
      reset_address_flag = 1;
    }

    // display progress in 32 increments (if not hidden)
    if ( !quiet ) __print_progress( &bout->info, &progress );
  }
  retval = 0;

finally:
  if ( !quiet ) {
    if( 0 == retval ) {
      if ( debug <= STM32_DEBUG_THRESHOLD ) {
        fprintf( stderr, "] " );
      }
      fprintf( stderr, "SUCCESS\n" );
    } else {
      if ( debug <= STM32_DEBUG_THRESHOLD ) {
        fprintf( stderr, " X  ");
      }
      fprintf( stderr, "ERROR\n" );
      if( retval==-3 )
        fprintf( stderr,
            "Memory access error, use debug for more info.\n" );
      else if( retval==-4 )
        fprintf( stderr,
            "Memory write error, use debug for more info.\n" );
    }
  }

  return retval;
}

// vim: shiftwidth=2
