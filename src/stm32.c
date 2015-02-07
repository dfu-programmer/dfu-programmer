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
#define STM32_OTP_BYTES_SIZE        528     /* number of OTP bytes (0x210) */
#define STM32_OPTION_BYTES_SIZE     16      /* number option bytes */

#define SET_ADDR_PTR            0x21
#define ERASE_CMD               0x41
#define READ_UNPROTECT          0x92
#define GET_CMD                 0x00

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

static int32_t stm32_write_block( dfu_device_t *device,
                                  size_t xfer_len,
                                  uint8_t *buffer );
  /* flash the contents of memory into a block of memory.  it is assumed that
   * the appropriate page has already been selected.  start and end are the
   * start and end addresses of the flash data.  returns 0 on success,
   * positive dfu error code if one is obtained, or negative if communitcation
   * with device fails.
   */

static int32_t stm32_read_block( dfu_device_t *device,
                                   size_t xfer_len,
                                   uint8_t *buffer );
  /* read a block of memory, assumes address pointer is already set
   */

static inline void print_progress( intel_buffer_info_t *info,
                    uint32_t *progress );
  /* calculate how many progress indicator steps to print and print them
   * update progress value
   */

static int32_t stm32_erase( dfu_device_t *device, uint8_t *command,
                            uint8_t command_length, dfu_bool quiet );
  /* erase, erase page, and read unprotect all share this functionality
   * although with different commands
   */


//___ V A R I A B L E S ______________________________________________________
extern int debug;       /* defined in main.c */

/* FIXME : these should be read from usb device descriptor because they are
 * device specififc */
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

static int32_t stm32_write_block( dfu_device_t *device,
                                  size_t xfer_len,
                                  uint8_t *buffer ) {
  TRACE( "%s( %p, %u, %p )\n", __FUNCTION__, device, xfer_len, buffer );
  int32_t status;

  /* check input args */
  if( (NULL == device) || (NULL == buffer) ) {
    DEBUG( "ERROR: Invalid arguments, device/buffer pointer is NULL.\n" );
    return -1;
  } else if ( xfer_len > STM32_MAX_TRANSFER_SIZE ) {
    DEBUG( "ERROR: 0x%X byte message > MAX TRANSFER SIZE (0x%X).\n",
        xfer_len, STM32_MAX_TRANSFER_SIZE );
    return -1;
  } else if ( xfer_len < 1 ) {
    DEBUG( "ERROR: xfer_len is %u\n", xfer_len );
    return -1;
  }

  if( xfer_len != dfu_download(device, xfer_len, buffer) ) {
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

static int32_t stm32_read_block( dfu_device_t *device,
                                   size_t xfer_len,
                                   uint8_t *buffer ) {
  TRACE( "%s( %p, %u, %p )\n", __FUNCTION__, device, xfer_len, buffer );
  int32_t result;

  if( buffer == NULL ) {
    DEBUG("ERROR: buffer ptr is NULL\n");
    return -1;
  } else if( xfer_len > STM32_MAX_TRANSFER_SIZE ) {
    /* this could cause a read problem */
    DEBUG("ERROR: transfer size %d exceeds max %d.\n",
        xfer_len, STM32_MAX_TRANSFER_SIZE );
    return -1;
  }

  /* check status before read */
  if( (result = stm32_get_status(device)) ) {
    DEBUG("Status Error %d before read\n", result );
    return -2;
  }

  result = dfu_upload( device, xfer_len, buffer );
  if( result < 0) {
    dfu_status_t status;

    DEBUG( "ERROR: dfu_upload result: %d\n", result );
    if( 0 == dfu_get_status(device, &status) ) {
      DEBUG( "Error Status %s, state %s\n",
          dfu_status_to_string(status.bStatus),
          dfu_state_to_string(status.bState) );
      if( status.bStatus == DFU_STATUS_ERROR_VENDOR ) {
        /* status = dfuERROR and state = errVENDOR */
        DEBUG("Device is read protected\n");
        return STM32_READ_PROT_ERROR;
      }
    } else {
      DEBUG("DFU GET_STATUS fail\n");
    }
    dfu_clear_status( device );

    return result;
  }

  return 0;
}

static inline void print_progress( intel_buffer_info_t *info,
                                     uint32_t *progress ) {
  if ( !(debug > STM32_DEBUG_THRESHOLD) ) {
    while ( ((info->block_end - info->data_start + 1) * 32) > *progress ) {
      fprintf( stderr, ">" );
      *progress += info->data_end - info->data_start + 1;
    }
  }
}

static int32_t stm32_erase( dfu_device_t *device, uint8_t *command,
                            uint8_t command_length, dfu_bool quiet ) {
  int32_t status;
  dfu_set_transaction_num( 0 );     /* set wValue to zero */
  if( command_length != dfu_download(device, command_length, command) ) {
    if( !quiet ) fprintf( stderr, "ERROR\n" );
    DEBUG( "dfu_download failed\n" );
    return UNSPECIFIED_ERROR;
  }

  /* call dfu get status to trigger command */
  if( (status = stm32_get_status(device)) ) {
    if( !quiet ) fprintf( stderr, "ERROR\n" );
    DEBUG("Error %d triggering %s\n", status, __FUNCTION__);
    return UNSPECIFIED_ERROR;
  }

  /* check status again for erase status, this can take a while */
  if( (status = stm32_get_status(device)) ) {
    DEBUG("Error %d: %s unsuccessful\n", status, __FUNCTION__);
    if( !quiet ) fprintf( stderr, "ERROR\n" );
    return UNSPECIFIED_ERROR;
  } else {
    if( !quiet ) fprintf( stderr, "DONE\n" );
  }

  return SUCCESS;
}

//___ F U N C T I O N S ______________________________________________________
int32_t stm32_erase_flash( dfu_device_t *device, dfu_bool quiet ) {
  TRACE( "%s( %p, %s )\n", __FUNCTION__, device, quiet ? "ture" : "false" );
  uint8_t command[] = { ERASE_CMD };
  uint8_t length = 1;

  if( !quiet ) {
    fprintf( stderr, "Erasing flash...  " );
    DEBUG("\n");
  }

  return stm32_erase( device, command, length, quiet );
}

int32_t stm32_page_erase( dfu_device_t *device, uint32_t address,
                           dfu_bool quiet ) {
  TRACE( "%s( %p, 0x%X, %s )\n", __FUNCTION__, device, address,
      quiet ? "ture" : "false" );
  uint8_t length = 5;

  uint8_t command[5] = {
    ERASE_CMD,
    (uint8_t) (0xff & address),             //  page LSB
    (uint8_t) (0xff & ((address)>>8)),
    (uint8_t) (0xff & ((address)>>16)),
    (uint8_t) (0xff & ((address)>>24))      // page MSB
  };

  return stm32_erase( device, command, length, quiet );
}

int32_t stm32_start_app( dfu_device_t *device, dfu_bool quiet ) {
  TRACE( "%s( %p )\n", __FUNCTION__, device );
  int32_t status;

  /* set address pointer (jump target) to start address */
  if( (status = stm32_set_address_ptr( device, STM32_FLASH_OFFSET )) ) {
    DEBUG("Error setting address pointer\n");
    return UNSPECIFIED_ERROR;
  }

  /* check dfu status for ok to send */
  if( (status = stm32_get_status(device)) ) {
    DEBUG("Error %d getting status on start\n", status);
    return UNSPECIFIED_ERROR;
  }

  if( !quiet ) fprintf( stderr, "Launching program...  \n" );
  dfu_set_transaction_num( 0 );     /* set wValue to zero */
  if( 0 != dfu_download(device, 0, NULL) ) {
    if( !quiet ) fprintf( stderr, "ERROR\n" );
    DEBUG( "dfu_download failed\n" );
    return UNSPECIFIED_ERROR;
  }

  /* call dfu get status to trigger command */
  if( (status = stm32_get_status(device)) ) {
    DEBUG("Error %d triggering %s\n", status, __FUNCTION__);
    return UNSPECIFIED_ERROR;
  }

  return SUCCESS;
}

int32_t stm32_read_flash( dfu_device_t *device, intel_buffer_in_t *buin,
    const uint8_t mem_segment, const dfu_bool quiet ) {
  TRACE( "%s( %p, %p, %u, %s )\n", __FUNCTION__, device, buin,
      mem_segment, ((true == quiet) ? "true" : "false"));

  uint8_t  reset_address_flag;  // reset address offset required
  uint32_t address_offset;  // keep record of sent progress as bytes * 32
  uint16_t  xfer_size = 0;      // the size of a transfer
  uint8_t mem_section = 0;       // tracks the current memory page
  uint32_t progress = 0;      // used to indicate progress
  int32_t status;
  int32_t retval = UNSPECIFIED_ERROR;   // the return value for this function

  if( (NULL == buin) || (NULL == device) ) {
    DEBUG( "invalid arguments.\n" );
    if( !quiet )
      fprintf( stderr, "Program Error, use debug for more info.\n" );
    return ARGUMENT_ERROR;
  }

  if( !quiet ) {
    if( debug <= STM32_DEBUG_THRESHOLD ) {
      /* NOTE: From here on we should go to finally on error */
      fprintf( stderr, "[================================] " );
    }
    fprintf( stderr, "Reading 0x%X bytes...\n",
        buin->info.data_end - buin->info.data_start + 1 );
    if( debug <= STM32_DEBUG_THRESHOLD ) {
      /* NOTE: From here on we should go to finally on error */
      fprintf( stderr, "[" );
    }
  }

  /* read the data */
  buin->info.block_start = buin->info.data_start;
  reset_address_flag = 0;
  address_offset = buin->info.block_start;

  while( buin->info.block_start <= buin->info.data_end ) {
    if( reset_address_flag ) {
      address_offset = buin->info.block_start;
      if( (status = stm32_set_address_ptr(device,
              STM32_FLASH_OFFSET + address_offset)) ) {
        DEBUG("Error setting address 0x%X\n", address_offset);
        retval = UNSPECIFIED_ERROR;
        goto finally;
      }
      dfu_set_transaction_num( 2 ); /* sets block offset 0 */
      reset_address_flag = 0;
    }

    // find end value for the current transfer
    buin->info.block_end = buin->info.block_start + STM32_MAX_TRANSFER_SIZE - 1;
    mem_section = buin->info.block_start / STM32_MIN_SECTOR_BOUND;
    if( buin->info.block_end / STM32_MIN_SECTOR_BOUND > mem_section ) {
      buin->info.block_end = STM32_MIN_SECTOR_BOUND * mem_section - 1;
    }
    if( buin->info.block_end > buin->info.data_end ) {
      buin->info.block_end = buin->info.data_end;
    }
    xfer_size = buin->info.block_end - buin->info.block_start + 1;
    if( xfer_size != STM32_MAX_TRANSFER_SIZE ) {
      DEBUG("xfer_size change, need addr reset\n");
      reset_address_flag = 1;
    }

    if( (status = stm32_read_block( device, xfer_size,
            &buin->data[buin->info.block_start] )) ) {
      DEBUG( "Error reading block 0x%X to 0x%X: err %d.\n",
          buin->info.block_start, buin->info.block_end, status );
      retval = ( status == -10 ) ? DEVICE_ACCESS_ERROR : FLASH_READ_ERROR;
      /* read protect error code in read_block is -10 */
      goto finally;
    }

    buin->info.block_start = buin->info.block_end + 1;
    if( reset_address_flag == 0 && (buin->info.block_start !=
        (STM32_MAX_TRANSFER_SIZE * (dfu_get_transaction_num() - 2))
        + address_offset) ) {
      DEBUG("block start & address mismatch, reset req\n");
      reset_address_flag = 1;
    }

    if( !quiet ) print_progress( &buin->info, &progress );
  }
  retval = SUCCESS;

finally:
  if ( !quiet ) {
    if( SUCCESS == retval ) {
      if ( debug <= STM32_DEBUG_THRESHOLD ) {
        fprintf( stderr, "] " );
      }
      fprintf( stderr, "SUCCESS\n" );
    } else {
      if ( debug <= STM32_DEBUG_THRESHOLD ) {
        fprintf( stderr, " X  ");
      }
      fprintf( stderr, "ERROR\n" );
      if( retval==DEVICE_ACCESS_ERROR )
        fprintf( stderr,
            "Memory access error, use debug for more info.\n" );
      else if( retval==FLASH_READ_ERROR )
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
  int32_t retval = UNSPECIFIED_ERROR;   // the return value for this function
  uint8_t buffer[STM32_MAX_TRANSFER_SIZE];     // buffer holding out data
  int32_t status;

  /* check arguments */
  if( (NULL == device) || (NULL == bout) ) {
    DEBUG( "ERROR: Invalid arguments, device/buffer pointer is NULL.\n" );
    if( !quiet )
      fprintf( stderr, "Program Error, use debug for more info.\n" );
    return ARGUMENT_ERROR;
  } else if( bout->info.valid_start > bout->info.valid_end ) {
    DEBUG( "ERROR: No valid target memory, end 0x%X before start 0x%X.\n",
        bout->info.valid_end, bout->info.valid_start );
    if( !quiet )
      fprintf( stderr, "Program Error, use debug for more info.\n" );
    return BUFFER_INIT_ERROR;
  }

  /* for each page with data, fill unassigned values on the page with 0xFF
   * bout->data[0] always aligns with a flash page boundary irrespective
   * of where valid_start is located */
  if( 0 != intel_flash_prep_buffer( bout ) ) {
    if( !quiet )
      fprintf( stderr, "Program Error, use debug for more info.\n" );
    return BUFFER_INIT_ERROR;
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
  DEBUG("Flash available from 0x%X to 0x%X, 0x%X bytes.\n",
      bout->info.valid_start, bout->info.valid_end,
      bout->info.valid_end - bout->info.valid_start + 1); // bytes inclusive so +1
  DEBUG("Data start @ 0x%X; %uB p 0x%X + 0x%X offset.\n",
      bout->info.data_start, bout->info.page_size,
      bout->info.data_start / bout->info.page_size,
      bout->info.data_start % bout->info.page_size);
  DEBUG("Data end @ 0x%X; %uB p 0x%X + 0x%X offset.\n",
      bout->info.data_end, bout->info.page_size,
      bout->info.data_end / bout->info.page_size,
      bout->info.data_end % bout->info.page_size);
  DEBUG("Totals: 0x%X bytes, %u %uB pages.\n",
      bout->info.data_end - bout->info.data_start + 1,
      bout->info.data_end / bout->info.page_size \
        - bout->info.data_start/bout->info.page_size + 1,
      bout->info.page_size );

  /* more error checking */
  if( (bout->info.data_start < bout->info.valid_start) ||
      (bout->info.data_end > bout->info.valid_end) ) {
    DEBUG( "ERROR: Data exists outside of the valid target flash region.\n" );
    if( !quiet )
      fprintf( stderr, "Hex file error, use debug for more info.\n" );
    return BUFFER_INIT_ERROR;
  } else if( bout->info.data_start == UINT32_MAX ) {
    DEBUG( "ERROR: No valid data to flash.\n" );
    if( !quiet )
      fprintf( stderr, "Hex file error, use debug for more info.\n" );
    return BUFFER_INIT_ERROR;
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
        retval = DEVICE_ACCESS_ERROR;
        goto finally;
      }
      dfu_set_transaction_num( 2 ); /* sets block offset 0 */
      reset_address_flag = 0;
    }

    /* find end address (info.block_end) for data section to write */
    mem_section = bout->info.block_start / STM32_MIN_SECTOR_BOUND;
    for( i = 0, bout->info.block_end = bout->info.block_start;
         bout->info.block_end <= bout->info.data_end;
         bout->info.block_end++, i++ ) {
      xfer_size = bout->info.block_end - bout->info.block_start + 1;
      // check if the current value is valid
      if( bout->data[bout->info.block_end] > UINT8_MAX ) break;
      // check if the current data packet is too big
      if( xfer_size > STM32_MAX_TRANSFER_SIZE ) break;
      // check if the current data value is outside of the memory sector
      if( bout->info.block_end / STM32_MIN_SECTOR_BOUND - mem_section ) break;

      buffer[i] = (uint8_t) bout->data[bout->info.block_end];
    }
    bout->info.block_end--; // bout->info.block_end was one step beyond the last data value to flash
    xfer_size = bout->info.block_end - bout->info.block_start + 1;
    if( xfer_size != STM32_MAX_TRANSFER_SIZE ) {
      DEBUG("xfer_size %u not max %u, need addr reset\n",
          xfer_size, STM32_MAX_TRANSFER_SIZE);
      reset_address_flag = 1;
    }

    /* write the data */
    DEBUG("Program data block: 0x%X to 0x%X, 0x%X bytes.\n",
        bout->info.block_start, bout->info.block_end, xfer_size);

    if( (status = stm32_write_block( device, xfer_size, buffer )) ) {
      DEBUG( "Error flashing the block: err %d.\n", status );
      retval = FLASH_WRITE_ERROR;
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
    if ( !quiet ) print_progress( &bout->info, &progress );
  }
  retval = SUCCESS;

finally:
  if ( !quiet ) {
    if( SUCCESS == retval ) {
      if ( debug <= STM32_DEBUG_THRESHOLD ) {
        fprintf( stderr, "] " );
      }
      fprintf( stderr, "SUCCESS\n" );
    } else {
      if ( debug <= STM32_DEBUG_THRESHOLD ) {
        fprintf( stderr, " X  ");
      }
      fprintf( stderr, "ERROR\n" );
      if( retval==DEVICE_ACCESS_ERROR )
        fprintf( stderr,
            "Memory access error, use debug for more info.\n" );
      else if( retval==FLASH_WRITE_ERROR )
        fprintf( stderr,
            "Memory write error, use debug for more info.\n" );
    }
  }

  return retval;
}

int32_t stm32_get_commands( dfu_device_t *device ) {
  TRACE("%s( %p )\n", __FUNCTION__, device);
  int32_t result;
  uint8_t i;
  const size_t xfer_len = 80;
  uint8_t buffer[xfer_len];

  /* check status before read */
  if( (result = stm32_get_status(device)) ) {
    DEBUG("Status Error %d before read\n", result );
    return UNSPECIFIED_ERROR;
  }

  dfu_set_transaction_num( 0 );
  result = dfu_upload( device, xfer_len, buffer );
  if( result < 0) {
    dfu_status_t status;

    DEBUG( "dfu_upload result: %d\n", result );
    result = UNSPECIFIED_ERROR;
    if( 0 == dfu_get_status(device, &status) ) {
      if( status.bStatus == DFU_STATUS_OK ) {
        DEBUG("DFU Status OK, state %d\n", status.bState);
      } else if( status.bStatus == DFU_STATUS_ERROR_VENDOR ) {
        DEBUG("Device is read protected\n");
        /* status = dfuERROR and state = errVENDOR */
        result = DEVICE_ACCESS_ERROR;
      } else {
        DEBUG("Unknown error status %d / state %d\n",
            status.bStatus, status.bState );
      }
    } else {
      DEBUG("DFU GET_STATUS fail\n");
    }
    dfu_clear_status( device );

    return result;
  }

  fprintf( stdout, "There are %d commands:\n", result );
  for( i = 0; i < result; i++ ) {
    fprintf( stdout, "  0x%02X\n", buffer[i] );
  }

  return SUCCESS;
}

int32_t stm32_get_configuration( dfu_device_t *device ) {
  TRACE("%s( %p )\n", __FUNCTION__, device);
  int32_t status;
  uint8_t i;
  uint8_t buffer[STM32_OPTION_BYTES_SIZE];

  if( (status = stm32_set_address_ptr(device,
          stm32_sector_addresses[mem_st_option_bytes])) ) {
    DEBUG("Error (%d) setting address 0x%X\n",
        status, stm32_sector_addresses[mem_st_option_bytes]);
    return UNSPECIFIED_ERROR;
  }

  if( (status = stm32_read_block(device, STM32_OPTION_BYTES_SIZE, buffer)) ) {
    DEBUG("Error (%d) reading option buffer block\n", status );
    return FLASH_READ_ERROR;
  }

  fprintf( stdout, "There are %d option bytes:\n", STM32_OPTION_BYTES_SIZE );
  fprintf( stdout, "0x%02X", buffer[0] );
  for( i = 1; i < STM32_OPTION_BYTES_SIZE; i++ ) {
    fprintf( stdout, ", 0x%02X", buffer[i] );
  }
  fprintf( stdout, "\n" );

  return SUCCESS;
}

int32_t stm32_read_unprotect( dfu_device_t *device, dfu_bool quiet ) {
  TRACE( "%s( %p, %s )\n", __FUNCTION__, device, quiet ? "ture" : "false" );
  uint8_t command[] = { READ_UNPROTECT };
  uint8_t length = 1;

  if( !quiet ) {
    fprintf( stderr, "Read Unprotect, Erasing flash...  " );
    DEBUG("\n");
  }

  return stm32_erase( device, command, length, quiet );
}

// vim: shiftwidth=2
