#ifndef __DFU_DEVICE_H__
#define __DFU_DEVICE_H__

#if HAVE_CONFIG_H
# include <config.h>
#endif
#include <stdint.h>
#ifdef HAVE_LIBUSB_1_0
#include <libusb.h>
#else
#include <usb.h>
#endif

// Atmel device classes are now defined with one bit per class.
// This simplifies checking in functions which handle more than one class.
#define ADC_8051    (1<<0)
#define ADC_AVR     (1<<1)
#define ADC_AVR32   (1<<2)
#define ADC_XMEGA   (1<<3)
#define DC_STM32    (1<<4)

// Most commands fall into one of 2 groups.
#define GRP_AVR32   (ADC_AVR32 | ADC_XMEGA)
#define GRP_AVR     (ADC_AVR | ADC_8051)
#define GRP_STM32   (DC_STM32)

typedef unsigned atmel_device_class_t;

typedef struct {
#ifdef HAVE_LIBUSB_1_0
    struct libusb_device_handle *handle;
#else
    struct usb_dev_handle *handle;
#endif
    int32_t interface;
    atmel_device_class_t type;
} dfu_device_t;

#endif /* __DFU_DEVICE_H__ */

/******************* S T M 3 2   D F U   C O M M A N D S ******************
========= USB DFU Bootloader Requests ==============
DFU_DETACH - [ 0x00 ]       wTimeout    interface   zero        none
    > Requests the device to leave DFU mode and enter the application.
    NOTE : The Detach request is not meaningful in the case of the bootloader.
           The bootloader is started by a system reset depending on the boot
           mode configuration settings, which means that no other application
           is running at this time.
DFU_DNLOAD - [ 0x01 ]       wBlockNum   interface   length      firmware
    > Requests data transfer from Host to the device in order to load them
      into device internal Flash. Includes also erase commands.
DFU_UPLOAD - [ 0x02 ]       zero        interface   length      firmware
    > Requests data transfer from device to Host in order to load content of
      device internal Flash into a Host file.
DFU_GETSTATUS - [ 0x03 ]    zero        interface   6           status
    > Requests device to send status report to the Host (including status
      resulting from the last request execution and the state the device will
      enter immediately after this request).
DFU_CLRSTATUS - [ 0x04 ]    zero        interface   zero        none
    > Requests device to clear error status and move to next step.
DFU_GETSTATE - [ 0x05 ]     zero        interface   1           state
    > Requests the device to send only the state it will enter immediately
      after this request.
DFU_ABORT - [ 0x06 ]        zero        interface   zero        none
    > Requests device to exit the current state/operation and enter idle
      state immediately.

========= STM32 DFU Commands ==============
.                       DFU_UPLOAD  DFU_DNLOAD cmd
.                       wValue      wValue  first byte
READ MEMORY             > 1
GET                     0
WRITE MEMORY                        > 1
ERASE                               0       0x41
READ UNPROTECT                      0       0x92
SET ADDRESS POINTER                 0       0x21
LEAVE DFU MODE                      0       -- (0 data length)

NOTES
    > before issuing a DNLOAD request the host should check that the device
      is in a correct sate dfuIDLE or dfuDNLOAD-IDLE and that there are no
      errors.  if the device is not in the correct state a DFU_CLRSTATUS must
      be called
    > after a command is sent using DFU_DNLOAD, DFU_GETSTATUS must be called
      to trigger command execution. a second DFU_GETSTATUS is required to
      check for success (except when writing to the option bytes beacuse device
      resets immediately after write)

READ COMMAND        DFU_UPLOAD with wValue > 1
    > host request device send a specified number of data bytes (wLength)
      where wLength can be 2 to 2048 bytes, for option bytes read size should
      be the option byte block size
    > the address to read data from is computed using wBlockNumber (wValue)
      and the address pointer using the formula:
        addresss = ((wBlockNum - 2) x wTransferSize) + Address_Pointer
        where: wTransferSize is the length of the requested data buffer
    > the address should be previously specified using SET ADDRESS POINTER
      otherwise the start of internal flash is assumed (0x0800000)

GET COMMAND         DFU_UPLOAD with wValue = 0
    > host sends DFU_UPLOAD command with wLength = 0, the device will return
      N bytes representing the command codes where N = 4

WRITE COMMAND       DFU_DNLOAD with wValue > 1
    > in a write memory operation the write size can be from 2 to 2048 bytes
      and the start address must be valid. when writing the option bytes the
      start address must be the start of the option byte area and the size
      should cover the entire option byte area
    > the address is computed using the value of the wBlockNumber (wValue) and
      the address pointer according the formula:
        address = ((wBlockNum - 2) x wTransferSize) + Address_Pointer
        where: wTransferSize is the length of the buffer sent by host
               wBlockNumber is the value of wValue parameter

SET ADDRESS POINTER - [ 0x21, mem ptr LSB A[7:0], A[8:15], A[23:16], A[31:24] ]
    > wValue should be zero
    > after sending the command host must send a DFU_GETSTATUS in order to
      execute the command.  a second DFU_GETSTATUS is needed to check if the
      command was correctly executed

ERASE COMMAND       - [ 0x41, page LSB A[7:0], A[15:8], A[23:16], A[31:24] ]
MASS ERASE          - [ 0x41 ]
    > this can take a long time to execute so timeout should be long

READ UNPROTECT      - [ 0x92 ]

LEAVE DFU MODE      - DFU_DNLOAD with 0 data length
    > device disconnect
    > init registers of peripherals to default reset values
    > initialize the user application main stack pointer
    > jumps to memory location programmed in the received 'address pointer + 4'
      which corresponds to the address of the application's reset handler

    > the address pointer has to be set (using set address pointer) before
      launching the LEAVE DFU command otherwise the bootloader will jump to the
      default memory start address (0x0800000)

- see an2606: STM32 microcontroller system memory boot mode
- see an3156: USB DFU protocol used in the STM32 bootloader
**************************************************************************/


/******************* A T M E L   D F U   C O M M A N D S ******************
// ---- A L L   D E V I C E S -----------------------------------
PROGRAM_MEMORY - [ 0x01, 0x00, start_MSB, start LSB, end_MSB, end_LSB ]
    > send the command, start should align with page boundary
    > send DFU_DNLOAD data, use non-significant bytes to align program start
    > optionally request status: 0 OK, 8 bad address, 3 memory locked
    > on all error DFU_CLRSTATUS
    NOTE: batchISP/FLIP send buffers with max size 0x800 bytes including prefix

READ_MEMORY - [ 0x03, 0x00, start_MSB, start_LSB, end_MSB, end_LSB ]
    > send the command
    > send DFU_UPLOAD
    > seems like you can request status, possibly only on GRP_AVR32
    NOTE: batchISP/FLIP request buffers with max size of 0x400 (1kb)

BLANK_CHECK - [ 0x03, 0x01, start_MSB, start_LSB, end_MSB, end_LSB ]
    > send the command
    > request status: 0 OK, 5 not blank, 8 bad address, 3 memory locked
    > on errCHECK_ERASED (5) send DFU_UPLOAD to get first non-blank adddress
    > on errWRITE (3), errADDRESS (8) and other error DFU_CLRSTATUS

CHIP_ERASE - [ 0x04, 0x00, 0xFF ]
    > send command
    > optionally request status: 0 OK, 9 in progress 3 memory locked
    > on errNOTDONE (9) may need to call CHIP_ERASE again (GRP_AVR)
    > on errWRITE (3) and any other error DFU_CLRSTATUS

START_APP - [ 0x04, 0x03, 0x00/0x01 ]
    > send command: 0x00 for start by resest, 0x01 for rjmp
     for start by jmp GRP_AVR devices also provide [ MSB, LSB ] of jump address
    > for start by reset device code must disable the watchdog timer
    > no DFU_GETSTATUS

SELECT_64KB_PAGE - [ 0x06, 0x03, 0x01, PP_MSB, PP_LSB ]
    > send command
    > page (PP) is a 64kb memory page (0x10000 bytes), not the flash page
    > optionally request status: 0 OK, 8 page value out of range
    > on errADDRESS (8) and any other error DFU_CLRSTATUS
    NOTE: not documented for AVR in (doc7618) but must be possible

// ---- G R O U P   A V R 3 2 -----------------------------------
SELECT_MEMORY_UNIT - [ 0x06, 0x03, 0x00, unit_num ]
    > send command
    > unit_numm [ flash, eeprom, security, config, bootloader, signature, user,
                    int_ram, ext_mem_cs0 - ext_mem_cs7, ext_mem_df ]
        - XMEGA ONLY: eeprom (1) and all other sections above user (6)
    > optionally request status: 0 OK, 8 unsupported memory
    > on errADDRESS (8) and any other error DFU_CLRSTATUS

// ---- G R O U P   A V R ---------------------------------------
PROGRAM_EEPROM - [ 0x01, 0x00, start_MSB, start_LSB, end_MSB, end_LSB ]
    > send the command, start should align with page boundary
    > send DFU_DNLOAD data
    > optionally request status: 0 OK, all other unspecified error
    > on all error DFU_CLRSTATUS
    NOTE: batchISP/FLIP send buffers with max size 0x800 bytes including prefix

READ_EEPROM - [ 0x03, 0x02, start_MSB, start_LSB, end_MSB, end_LSB ]
    > send the command
    > send DFU_UPLOAD
    NOTE: batchISP/FLIP request buffers with max size of 0x400 (1kb)

READ_CONFIG - [ 0x05, rnum0, rnum1 ]
    > send command
    > rnum [0x00, 0x00-0x02] enum boot version, boot ID1, boot ID2
    > 8051 only: rnum [0x01, 0x00-6] enum BSB, SBV, P1_CF, P3_CF, P4_CF, SSB, EB
    > rnum [0x01, 0x30-1, 0x60-1] map MFG_CODE, FAM_CODE, PROD_NAME, PROD_VER
    > 8051 only: rnum [0x02, 0x00] fuse HWB
    > optionally get status: 0 OK, 0xb chip is protected
    > on OK send upload request to get value
    > on errVENDOR (0xb) send DFU_CLRSTATUS

// ---- A D C   8 0 5 1   O N L Y -------------------------------
ERASE_BLOCK - [ 0x04, 0x00, block ]
    > send the command
    > block0 0x00 is memory from 0x0 to 0x1FFF
    > block1 0x20 is memory from 0x2000 to 0x3FFF
    > block2 0x20 is memory from 0x4000 to 0x7FFF
    > optionally request status: 0 OK, 3 chip is protected
    > on errWRITE (3) and all other error DFU_CLRSTATUS

SET_CONFIG - [0x04, rnum0, rnum1, value]
    > send command
    > rnum [0x01, 0x00-0x06] enum BSB, SBV, P1_CF, P3_CF, P4_CF, SSB, EB
    > rnum [0x02, 0x00] for Fuse HSB
    > value is the value to set
    > optionally request DFU_GETSTATUS: 0 OK, 3 chip is protected
    > on errWRITE (3) or any other error must DFU_CLRSTATUS

Notes:
    - AT90/ATmega the first command must be a full chip erase, everything else
      is locked until this command is issued
    - GRP_AVR provide entry points for low level flash drivers like page erase,
      page write, load word in the temporary page buffer (see doc7618)
    - AT89 when starting application through reset, if an external reset chip is
      make sure the reset pulse is correct (see doc4287)
    - alignment of program with page boundaries: In order to be in accordance
      with the Flash page size (128 bytes), X non-significant bytes are added
      before the firt byte to program.  The X number is calculated to align the
      beginning of the firmware with the Flash page. X = start_address[32]. For
      example, if the start address is 00AFh (175d), X = 175 [32] = 15.

- see doc32131: AVR32 UC3 USB DFU Bootloader Protocol (AVR32)
- see doc7618: USB DFU Bootloader Datasheet (AT90/ATmega)
- see doc4287: AT89C5131A USB Bootloader (AT89)
- see doc8457:  FLIP USB DFU Protocol (xmega)

**************************************************************************/
