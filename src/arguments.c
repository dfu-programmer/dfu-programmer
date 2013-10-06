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

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dfu-bool.h"
#include "dfu-device.h"
#include "config.h"
#include "arguments.h"

struct option_mapping_structure {
    const char *name;
    int32_t value;
};

struct target_mapping_structure {
    const char *name;
    enum targets_enum value;
    atmel_device_class_t device_type;
    uint16_t chip_id;
    uint16_t vendor_id;
    size_t memory_size;
    size_t bootloader_size;
    dfu_bool bootloader_at_highmem;
    size_t flash_page_size;
    dfu_bool initial_abort;
    dfu_bool honor_interfaceclass;
    size_t eeprom_page_size;
    size_t eeprom_memory_size;
};

/* NOTE FOR: at90usb1287, at90usb1286, at90usb647, at90usb646, at90usb162, at90usb82
 *
 * The actual size of the user-programmable section is limited by the
 * space needed by the bootloader.  The size of the bootloader is set
 * by BOOTSZ0/BOOTSZ1 fuse bits; here we assume the bootloader is 4kb or 8kb.
 * The window used for the bootloader is at the top of the of memory.
 *
 * For at89c5130/1 the bootloader is outside the normal flash area.
 * Which is why the boot size is marked as 0 bytes.
 *
 * VID and PID are the USB identifiers returned by the DFU bootloader.
 * They are defined by Atmel's bootloader code, and are not in the chip datasheet.
 * An incomplete list can be found the the various DFU bootloader docs.
 * If you plug the device in, lsusb or the Windows device manager can tell you
 * the VID and PID values.
 */

/* ----- target specific structures ----------------------------------------- */
static struct target_mapping_structure target_map[] = {
    // Name             ID (arguments.h)    DevType    PID     VID     MemSize  BootSz  BootHi FPage Abort IF     EPage ESize
    { "at89c51snd1c",   tar_at89c51snd1c,   ADC_8051,  0x2FFF, 0x03eb, 0x10000, 0x1000, true,  128, false, true,  0,   0      },
    { "at89c51snd2c",   tar_at89c51snd2c,   ADC_8051,  0x2FFF, 0x03eb, 0x10000, 0x1000, true,  128, false, true,  0,   0      },
    { "at89c5130",      tar_at89c5130,      ADC_8051,  0x2FFD, 0x03eb, 0x04000, 0x0000, true,  128, false, true,  128, 0x0400 },
    { "at89c5131",      tar_at89c5131,      ADC_8051,  0x2FFD, 0x03eb, 0x08000, 0x0000, true,  128, false, true,  128, 0x0400 },
    { "at89c5132",      tar_at89c5132,      ADC_8051,  0x2FFF, 0x03eb, 0x10000, 0x0C00, true,  128, false, true,  0,   0      },
    // Name             ID (arguments.h)    DevType    PID     VID     MemSize  BootSz  BootHi FPage Abort IF     EPage ESize
    { "at90usb1287",    tar_at90usb1287,    ADC_AVR,   0x2FFB, 0x03eb, 0x20000, 0x2000, true,  128, true,  false, 128, 0x1000 },
    { "at90usb1286",    tar_at90usb1286,    ADC_AVR,   0x2FFB, 0x03eb, 0x20000, 0x2000, true,  128, true,  false, 128, 0x1000 },
    { "at90usb1287-4k", tar_at90usb1287_4k, ADC_AVR,   0x2FFB, 0x03eb, 0x20000, 0x1000, true,  128, true,  false, 128, 0x1000 },
    { "at90usb1286-4k", tar_at90usb1286_4k, ADC_AVR,   0x2FFB, 0x03eb, 0x20000, 0x1000, true,  128, true,  false, 128, 0x1000 },
    { "at90usb647",     tar_at90usb647,     ADC_AVR,   0x2FF9, 0x03eb, 0x10000, 0x2000, true,  128, true,  false, 128, 0x0800 },
    { "at90usb646",     tar_at90usb646,     ADC_AVR,   0x2FF9, 0x03eb, 0x10000, 0x2000, true,  128, true,  false, 128, 0x0800 },
    { "at90usb162",     tar_at90usb162,     ADC_AVR,   0x2FFA, 0x03eb, 0x04000, 0x1000, true,  128, true,  false, 128, 0x0200 },
    { "at90usb82",      tar_at90usb82,      ADC_AVR,   0x2FF7, 0x03eb, 0x02000, 0x1000, true,  128, true,  false, 128, 0x0200 },
    { "atmega32u6",     tar_atmega32u6,     ADC_AVR,   0x2FF2, 0x03eb, 0x08000, 0x1000, true,  128, true,  false, 128, 0x0400 },
    { "atmega32u4",     tar_atmega32u4,     ADC_AVR,   0x2FF4, 0x03eb, 0x08000, 0x1000, true,  128, true,  false, 128, 0x0400 },
    { "atmega32u2",     tar_atmega32u2,     ADC_AVR,   0x2FF0, 0x03eb, 0x08000, 0x1000, true,  128, true,  false, 128, 0x0400 },
    { "atmega16u4",     tar_atmega16u4,     ADC_AVR,   0x2FF3, 0x03eb, 0x04000, 0x1000, true,  128, true,  false, 128, 0x0200 },
    { "atmega16u2",     tar_atmega16u2,     ADC_AVR,   0x2FEF, 0x03eb, 0x04000, 0x1000, true,  128, true,  false, 128, 0x0200 },
    { "atmega8u2",      tar_atmega8u2,      ADC_AVR,   0x2FEE, 0x03eb, 0x02000, 0x1000, true,  128, true,  false, 128, 0x0200 },
    // Name             ID (arguments.h)    DevType    PID     VID     MemSize  BootSz  BootHi FPage Abort IF     EPage ESize
    { "at32uc3a0128",   tar_at32uc3a0128,   ADC_AVR32, 0x2FF8, 0x03eb, 0x20000, 0x2000, false, 512, false, true,  0,   0      },
    { "at32uc3a1128",   tar_at32uc3a1128,   ADC_AVR32, 0x2FF8, 0x03eb, 0x20000, 0x2000, false, 512, false, true,  0,   0      },
    { "at32uc3a0256",   tar_at32uc3a0256,   ADC_AVR32, 0x2FF8, 0x03eb, 0x40000, 0x2000, false, 512, false, true,  0,   0      },
    { "at32uc3a1256",   tar_at32uc3a1256,   ADC_AVR32, 0x2FF8, 0x03eb, 0x40000, 0x2000, false, 512, false, true,  0,   0      },
    { "at32uc3a0512",   tar_at32uc3a0512,   ADC_AVR32, 0x2FF8, 0x03eb, 0x80000, 0x2000, false, 512, false, true,  0,   0      },
    { "at32uc3a1512",   tar_at32uc3a1512,   ADC_AVR32, 0x2FF8, 0x03eb, 0x80000, 0x2000, false, 512, false, true,  0,   0      },
    { "at32uc3a0512es", tar_at32uc3a0512es, ADC_AVR32, 0x2FF8, 0x03eb, 0x80000, 0x2000, false, 512, false, true,  0,   0      },
    { "at32uc3a1512es", tar_at32uc3a1512es, ADC_AVR32, 0x2FF8, 0x03eb, 0x80000, 0x2000, false, 512, false, true,  0,   0      },
    { "at32uc3a364",    tar_at32uc3a364,    ADC_AVR32, 0x2FF1, 0x03eb, 0x10000, 0x2000, false, 512, false, true,  0,   0      },
    { "at32uc3a364s",   tar_at32uc3a364s,   ADC_AVR32, 0x2FF1, 0x03eb, 0x10000, 0x2000, false, 512, false, true,  0,   0      },
    { "at32uc3a3128",   tar_at32uc3a3128,   ADC_AVR32, 0x2FF1, 0x03eb, 0x20000, 0x2000, false, 512, false, true,  0,   0      },
    { "at32uc3a3128s",  tar_at32uc3a3128s,  ADC_AVR32, 0x2FF1, 0x03eb, 0x20000, 0x2000, false, 512, false, true,  0,   0      },
    { "at32uc3a3256",   tar_at32uc3a3256,   ADC_AVR32, 0x2FF1, 0x03eb, 0x40000, 0x2000, false, 512, false, true,  0,   0      },
    { "at32uc3a3256s",  tar_at32uc3a3256s,  ADC_AVR32, 0x2FF1, 0x03eb, 0x40000, 0x2000, false, 512, false, true,  0,   0      },
    { "at32uc3a4256s",  tar_at32uc3a4256s,  ADC_AVR32, 0x2FF1, 0x03eb, 0x40000, 0x2000, false, 512, false, true,  0,   0      },
    // Name             ID (arguments.h)    DevType    PID     VID     MemSize  BootSz  BootHi FPage Abort IF     EPage ESize
    { "at32uc3b064",    tar_at32uc3b064,    ADC_AVR32, 0x2FF6, 0x03eb, 0x10000, 0x2000, false, 512, false, true,  0,   0      },
    { "at32uc3b164",    tar_at32uc3b164,    ADC_AVR32, 0x2FF6, 0x03eb, 0x10000, 0x2000, false, 512, false, true,  0,   0      },
    { "at32uc3b0128",   tar_at32uc3b0128,   ADC_AVR32, 0x2FF6, 0x03eb, 0x20000, 0x2000, false, 512, false, true,  0,   0      },
    { "at32uc3b1128",   tar_at32uc3b1128,   ADC_AVR32, 0x2FF6, 0x03eb, 0x20000, 0x2000, false, 512, false, true,  0,   0      },
    { "at32uc3b0256",   tar_at32uc3b0256,   ADC_AVR32, 0x2FF6, 0x03eb, 0x40000, 0x2000, false, 512, false, true,  0,   0      },
    { "at32uc3b1256",   tar_at32uc3b1256,   ADC_AVR32, 0x2FF6, 0x03eb, 0x40000, 0x2000, false, 512, false, true,  0,   0      },
    { "at32uc3b0256es", tar_at32uc3b0256es, ADC_AVR32, 0x2FF6, 0x03eb, 0x40000, 0x2000, false, 512, false, true,  0,   0      },
    { "at32uc3b1256es", tar_at32uc3b1256es, ADC_AVR32, 0x2FF6, 0x03eb, 0x40000, 0x2000, false, 512, false, true,  0,   0      },
    { "at32uc3b0512",   tar_at32uc3b0512,   ADC_AVR32, 0x2FF6, 0x03eb, 0x80000, 0x2000, false, 512, false, true,  0,   0      },
    { "at32uc3b1512",   tar_at32uc3b1512,   ADC_AVR32, 0x2FF6, 0x03eb, 0x80000, 0x2000, false, 512, false, true,  0,   0      },
    // Name             ID (arguments.h)    DevType    PID     VID     MemSize  BootSz  BootHi FPage Abort IF     EPage ESize
    { "at32uc3c064",    tar_at32uc3c064,    ADC_AVR32, 0x2FEB, 0x03eb, 0x10000, 0x2000, false, 512, false, true,  0,   0      },
    { "at32uc3c0128",   tar_at32uc3c0128,   ADC_AVR32, 0x2FEB, 0x03eb, 0x20000, 0x2000, false, 512, false, true,  0,   0      },
    { "at32uc3c0256",   tar_at32uc3c0256,   ADC_AVR32, 0x2FEB, 0x03eb, 0x40000, 0x2000, false, 512, false, true,  0,   0      },
    { "at32uc3c0512",   tar_at32uc3c0512,   ADC_AVR32, 0x2FEB, 0x03eb, 0x80000, 0x2000, false, 512, false, true,  0,   0      },
    { "at32uc3c164",    tar_at32uc3c164,    ADC_AVR32, 0x2FEB, 0x03eb, 0x10000, 0x2000, false, 512, false, true,  0,   0      },
    { "at32uc3c1128",   tar_at32uc3c1128,   ADC_AVR32, 0x2FEB, 0x03eb, 0x20000, 0x2000, false, 512, false, true,  0,   0      },
    { "at32uc3c1256",   tar_at32uc3c1256,   ADC_AVR32, 0x2FEB, 0x03eb, 0x40000, 0x2000, false, 512, false, true,  0,   0      },
    { "at32uc3c1512",   tar_at32uc3c1512,   ADC_AVR32, 0x2FEB, 0x03eb, 0x80000, 0x2000, false, 512, false, true,  0,   0      },
    { "at32uc3c264",    tar_at32uc3c264,    ADC_AVR32, 0x2FEB, 0x03eb, 0x10000, 0x2000, false, 512, false, true,  0,   0      },
    { "at32uc3c2128",   tar_at32uc3c2128,   ADC_AVR32, 0x2FEB, 0x03eb, 0x20000, 0x2000, false, 512, false, true,  0,   0      },
    { "at32uc3c2256",   tar_at32uc3c2256,   ADC_AVR32, 0x2FEB, 0x03eb, 0x40000, 0x2000, false, 512, false, true,  0,   0      },
    { "at32uc3c2512",   tar_at32uc3c2512,   ADC_AVR32, 0x2FEB, 0x03eb, 0x80000, 0x2000, false, 512, false, true,  0,   0      },
    // Name             ID (arguments.h)    DevType    PID     VID     MemSize  BootSz  BootHi FPage Abort IF     EPage ESize
    { "atxmega64a1u",   tar_atxmega64a1u,   ADC_XMEGA, 0x2FE8, 0x03eb, 0x10000, 0x1000, true,  256, true,  false, 32,  0x0800 },
    { "atxmega128a1u",  tar_atxmega128a1u,  ADC_XMEGA, 0x2FED, 0x03eb, 0x20000, 0x2000, true,  256, true,  false, 32,  0x0800 },
    { "atxmega64a3u",   tar_atxmega64a3u,   ADC_XMEGA, 0x2FE5, 0x03eb, 0x10000, 0x1000, true,  256, true,  false, 32,  0x0800 },
    { "atxmega128a3u",  tar_atxmega128a3u,  ADC_XMEGA, 0x2FE6, 0x03eb, 0x20000, 0x2000, true,  512, true,  false, 32,  0x0800 },
    { "atxmega192a3u",  tar_atxmega192a3u,  ADC_XMEGA, 0x2FE7, 0x03eb, 0x30000, 0x2000, true,  512, true,  false, 32,  0x0800 },
    { "atxmega256a3u",  tar_atxmega256a3u,  ADC_XMEGA, 0x2FEC, 0x03eb, 0x40000, 0x2000, true,  512, true,  false, 32,  0x1000 },
    { "atxmega16a4u",   tar_atxmega16a4u,   ADC_XMEGA, 0x2FE3, 0x03eb, 0x04000, 0x1000, true,  256, true,  false, 32,  0x0400 },
    { "atxmega32a4u",   tar_atxmega32a4u,   ADC_XMEGA, 0x2FE4, 0x03eb, 0x08000, 0x1000, true,  256, true,  false, 32,  0x0400 },
    { "atxmega64a4u",   tar_atxmega64a4u,   ADC_XMEGA, 0x2FDD, 0x03eb, 0x10000, 0x1000, true,  256, true,  false, 32,  0x0800 },
    { "atxmega128a4u",  tar_atxmega128a4u,  ADC_XMEGA, 0x2FDE, 0x03eb, 0x20000, 0x2000, true,  256, true,  false, 32,  0x0800 },
    { "atxmega256a3bu", tar_atxmega256a3bu, ADC_XMEGA, 0x2FE2, 0x03eb, 0x40000, 0x2000, true,  512, true,  false, 32,  0x1000 },
    // Name             ID (arguments.h)    DevType    PID     VID     MemSize  BootSz  BootHi FPage Abort IF     EPage ESize
    { "atxmega64b1",    tar_atxmega64b1,    ADC_XMEGA, 0x2FE1, 0x03eb, 0x10000, 0x1000, true,  256, true,  false, 32,  0x0800 },
    { "atxmega128b1",   tar_atxmega128b1,   ADC_XMEGA, 0x2FEA, 0x03eb, 0x20000, 0x2000, true,  256, true,  false, 32,  0x0800 },
    { "atxmega64b3",    tar_atxmega64b3,    ADC_XMEGA, 0x2FDF, 0x03eb, 0x10000, 0x1000, true,  256, true,  false, 32,  0x0800 },
    { "atxmega128b3",   tar_atxmega128b3,   ADC_XMEGA, 0x2FE0, 0x03eb, 0x20000, 0x2000, true,  256, true,  false, 32,  0x0800 },
    // Name             ID (arguments.h)    DevType    PID     VID     MemSize  BootSz  BootHi FPage Abort IF     EPage ESize
    { "atxmega64c3",    tar_atxmega64c3,    ADC_XMEGA, 0x2FD6, 0x03eb, 0x10000, 0x1000, true,  256, true,  false, 32,  0x0800 },
    { "atxmega128c3",   tar_atxmega128c3,   ADC_XMEGA, 0x2FD7, 0x03eb, 0x20000, 0x2000, true,  512, true,  false, 32,  0x0800 },
    { "atxmega256c3",   tar_atxmega256c3,   ADC_XMEGA, 0x2FDA, 0x03eb, 0x40000, 0x2000, true,  512, true,  false, 32,  0x1000 },
    { "atxmega384c3",   tar_atxmega384c3,   ADC_XMEGA, 0x2FDB, 0x03eb, 0x60000, 0x2000, true,  512, true,  false, 32,  0x1000 },
    { NULL }
};

/* ----- command specific structures ---------------------------------------- */
static struct option_mapping_structure command_map[] = {
    { "configure",    com_configure },
    { "dump",         com_dump      },
    { "dump-eeprom",  com_edump     },
    { "dump-user",    com_udump     },
    { "erase",        com_erase     },
    { "flash",        com_flash     },
    { "flash-user",   com_user      },
    { "flash-eeprom", com_eflash    },
    { "get",          com_get       },
    { "getfuse",      com_getfuse   },
    { "launch",       com_launch    },
    { "setfuse",      com_setfuse   },
    { "setsecure",    com_setsecure },
    { "reset",        com_reset     },
    { "start",        com_start_app },
    { NULL }
};

/* ----- configure specific structures -------------------------------------- */
static struct option_mapping_structure configure_map[] = {
    { "BSB", conf_BSB },
    { "SBV", conf_SBV },
    { "SSB", conf_SSB },
    { "EB",  conf_EB  },
    { "HSB", conf_HSB },
    { NULL }
};

/* ----- get specific structures -------------------------------------- */
static struct option_mapping_structure get_map[] = {
    { "bootloader-version", get_bootloader   },
    { "ID1",                get_ID1          },
    { "ID2",                get_ID2          },
    { "BSB",                get_BSB          },
    { "SBV",                get_SBV          },
    { "SSB",                get_SSB          },
    { "EB",                 get_EB           },
    { "manufacturer",       get_manufacturer },
    { "family",             get_family       },
    { "product-name",       get_product_name },
    { "product-revision",   get_product_rev  },
    { "HSB",                get_HSB          },
    { NULL }
};

/* ----- getfuse specific structures ---------------------------------- */
static struct option_mapping_structure getfuse_map[] = {
    { "LOCK",           get_lock           },
    { "EPFL",           get_epfl           },
    { "BOOTPROT",       get_bootprot       },
    { "BODLEVEL",       get_bodlevel       },
    { "BODHYST",        get_bodhyst        },
    { "BODEN",          get_boden          },
    { "ISP_BOD_EN",     get_isp_bod_en     },
    { "ISP_IO_COND_EN", get_isp_io_cond_en },
    { "ISP_FORCE",      get_isp_force      },
    { NULL }
};

/* ----- setfuse specific structures ---------------------------------- */
static struct option_mapping_structure setfuse_map[] = {
    { "LOCK",           set_lock           },
    { "EPFL",           set_epfl           },
    { "BOOTPROT",       set_bootprot       },
    { "BODLEVEL",       set_bodlevel       },
    { "BODHYST",        set_bodhyst        },
    { "BODEN",          set_boden          },
    { "ISP_BOD_EN",     set_isp_bod_en     },
    { "ISP_IO_COND_EN", set_isp_io_cond_en },
    { "ISP_FORCE",      set_isp_force      },
    { NULL }
};

static void list_targets()
{
    struct target_mapping_structure *map = NULL;
    int col = 0;

    map = target_map;

    fprintf( stderr, "targets:\n" );
    while( 0 != *((int32_t *) map) ) {
        if( 0 == col ) {
            fprintf( stderr, " " );
        }
        fprintf( stderr, "   %-16s", map->name );
        if( 4 == ++col ) {
            fprintf( stderr, "\n" );
            col = 0;
        }
        map++;
    }
    if( 0 != col )
        fprintf( stderr, "\n" );
}

static void basic_help()
{
    fprintf( stderr, "Type 'dfu-programmer --help'    for a list of commands\n" );
    fprintf( stderr, "     'dfu-programmer --targets' to list supported target devices\n" );
    fprintf( stderr, "     'dfu-programmer --version' to show version information\n" );
}

static void usage()
{
    fprintf( stderr, "Usage: dfu-programmer target[:usb-bus,usb-addr] command [options] "
                     "[global-options] [file|data]\n\n" );

    fprintf( stderr, "global-options:\n"
                     "        --quiet\n"
                     "        --debug level    (level is an integer specifying level of detail)\n"
                     "        Global options can be used with any command and must come\n"
                     "        after the command and before any file or data value\n\n" );
    fprintf( stderr, "commands:\n" );
    fprintf( stderr, "        configure {BSB|SBV|SSB|EB|HSB} "
                     "[--suppress-validation] data\n" );
    fprintf( stderr, "        dump\n" );
    fprintf( stderr, "        dump-eeprom\n" );
    fprintf( stderr, "        dump-user\n" );
    fprintf( stderr, "        erase [--suppress-validation]\n" );
    fprintf( stderr, "        flash [--suppress-validation] [--suppress-bootloader-mem]\n"
                     "                     [--serial=hexdigits:offset] {file|STDIN}\n" );
    fprintf( stderr, "        flash-eeprom [--suppress-validation]\n"
                     "                     [--serial=hexdigits:offset] {file|STDIN}\n" );
    fprintf( stderr, "        flash-user   [--suppress-validation]\n"
                     "                     [--serial=hexdigits:offset] {file|STDIN}\n" );
    fprintf( stderr, "        get     {bootloader-version|ID1|ID2|BSB|SBV|SSB|EB|\n"
                     "                 manufacturer|family|product-name|\n"
                     "                 product-revision|HSB}\n" );
    fprintf( stderr, "        getfuse {LOCK|EPFL|BOOTPROT|BODLEVEL|BODHYST|\n"
                     "                 BODEN|ISP_BOD_EN|ISP_IO_COND_EN|\n"
                     "                 ISP_FORCE}\n" );
    fprintf( stderr, "        launch       [--no-reset]\n" );
    fprintf( stderr, "        setfuse {LOCK|EPFL|BOOTPROT|BODLEVEL|BODHYST|\n"
                     "                 BODEN|ISP_BOD_EN|ISP_IO_COND_EN|\n"
                     "                 ISP_FORCE} data\n" );
    fprintf( stderr, "        setsecure\n" );
}

static int32_t assign_option( int32_t *arg,
                              char *value,
                              struct option_mapping_structure *map )
{
    while( 0 != *((int32_t *) map) ) {
        if( 0 == strcasecmp(value, map->name) ) {
            *arg = map->value;
            return 0;
        }

        map++;
    }

    return -1;
}

static int32_t assign_target( struct programmer_arguments *args,
                              char *value,
                              struct target_mapping_structure *map )
{
    while( 0 != *((int32_t *) map) ) {
        size_t name_len = strlen(map->name);
        if( 0 == strncasecmp(value, map->name, name_len)
            && (value[name_len] == '\0'
                || value[name_len] == ':')) {
            args->target  = map->value;
            args->chip_id = map->chip_id;
            args->vendor_id = map->vendor_id;
            args->bus_id = 0;
            args->device_address = 0;
            if (value[name_len] == ':') {
              /* The target name includes USB bus and address info.
               * This is used to differentiate between multiple dfu
               * devices with the same vendor/chip ID numbers. By
               * specifying the bus and address, mltiple units can
               * be programmed at one time.
               */
              int bus = 0;
              int address = 0;
              if( 2 != sscanf(&value[name_len+1], "%i,%i", &bus, &address) )
                return -1;
              if (bus <= 0) return -1;
              if (address <= 0) return -1;
              args->bus_id = bus;
              args->device_address = address;
            }
            args->device_type = map->device_type;
            args->eeprom_memory_size = map->eeprom_memory_size;
            args->flash_page_size = map->flash_page_size;
            args->eeprom_page_size = map->eeprom_page_size;
            args->initial_abort = map->initial_abort;
            /* There have been several reports on the mailing list of dfu-programmer
               reporting "No device present" when there clearly is. It seems Atmel's
               bootloader has changed (or is buggy) and doesn't report interface class
               and subclass the way is did before. However we have already matched
               VID and PID so why would we worry about this. Don't use the device-
               specific value, just ignore the error for all device types.
            */
            args->honor_interfaceclass = false;
            args->memory_address_top = map->memory_size - 1;
            args->memory_address_bottom = 0;
            args->flash_address_top = args->memory_address_top;
            args->flash_address_bottom = args->memory_address_bottom;
            args->bootloader_bottom = 0;
            args->bootloader_top = 0;
            args->bootloader_at_highmem = map->bootloader_at_highmem;
            if( true == map->bootloader_at_highmem ) {
                args->bootloader_bottom = map->memory_size - map->bootloader_size;
                args->bootloader_top = args->flash_address_top;
                args->flash_address_top -= map->bootloader_size;
            } else {
                args->bootloader_bottom = args->flash_address_bottom;
                args->bootloader_top += map->bootloader_size - 1;
                args->flash_address_bottom += map->bootloader_size;
            }
            switch( args->device_type ) {
                case ADC_8051:
                    strncpy( args->device_type_string, "8051",
                             DEVICE_TYPE_STRING_MAX_LENGTH );
                    break;
                case ADC_AVR:
                    strncpy( args->device_type_string, "AVR",
                             DEVICE_TYPE_STRING_MAX_LENGTH );
                    break;
                case ADC_AVR32:
                    strncpy( args->device_type_string, "AVR32",
                             DEVICE_TYPE_STRING_MAX_LENGTH );
                    break;
                case ADC_XMEGA:
                    strncpy( args->device_type_string, "XMEGA",
                             DEVICE_TYPE_STRING_MAX_LENGTH );
                    break;
            }
            return 0;
        }

        map++;
    }

    return -1;
}

static int32_t assign_global_options( struct programmer_arguments *args,
                                      const size_t argc,
                                      char **argv )
{
    size_t i = 0;

    /* Find '--quiet' if it is here */
    for( i = 0; i < argc; i++ ) {
        if( 0 == strcmp("--quiet", argv[i]) ) {
            *argv[i] = '\0';
            args->quiet = 1;
            break;
        }
    }

    /* Find '--suppress-bootloader-mem' if it is here */
    for( i = 0; i < argc; i++ ) {
        if( 0 == strcmp("--suppress-bootloader-mem", argv[i]) ) {
            *argv[i] = '\0';
            args->suppressbootloader = 1;
            break;
        }
    }

    /* Find '--suppress-validation' if it is here - even though it is not
     * used by all this is easier. */
    for( i = 0; i < argc; i++ ) {
        if( 0 == strcmp("--suppress-validation", argv[i]) ) {
            *argv[i] = '\0';

            switch( args->command ) {
                case com_configure:
                    args->com_configure_data.suppress_validation = 1;
                    break;
                case com_erase:
                    args->com_erase_data.suppress_validation = 1;
                    break;
                case com_flash:
                case com_eflash:
                case com_user:
                    args->com_flash_data.suppress_validation = 1;
                    break;
                default:
                    /* not supported. */
                    return -1;
            }

            break;
        }
    }

    /* Find '--no-reset' if it is here - even though it is not
     * used by all this is easier. */
    for( i = 0; i < argc; i++ ) {
        if( 0 == strcmp("--no-reset", argv[i]) ) {
            *argv[i] = '\0';

            if ( args->command == com_launch ) {
                args->com_launch_config.noreset = true;
            } else {
                // not supported
                return -1;
            }
            break;
        }
    }

    /* Find '--debug' if it is here */
    for( i = 0; i < argc; i++ ) {
        if( 0 == strncmp("--debug", argv[i], 7) ) {

            if( 0 == strncmp("--debug=", argv[i], 8) ) {
                if( 1 != sscanf(argv[i], "--debug=%i", &debug) )
                    return -2;
            } else {
                if( (i+1) >= argc )
                    return -3;

                if( 1 != sscanf(argv[i+1], "%i", &debug) )
                    return -4;

                *argv[i+1] = '\0';
            }
            *argv[i] = '\0';
            break;
        }
    }

    /* Find '--serial=<hexdigit+>:<offset>' */
    for( i = 0; i < argc; i++ ) {
      if( 0 == strncmp("--serial=", argv[i], 9) ) {
            *argv[i] = '\0';

            switch( args->command ) {
                case com_flash:
                case com_eflash:
                case com_user: {
                    char *hexdigits = &argv[i][9];
                    char *offset_start = hexdigits;
                    size_t num_digits = 0;
                    int16_t *serial_data = NULL;
                    long serial_offset = 0;
                    size_t j = 0;
                    char buffer[3] = {0,0,0};
                    while (*offset_start != ':') {
                        char c = *offset_start;
                        if ('\0' == c) {
                            return -1;
                        } else if (('0' <= c && c <= '9')
                            || ('a' <= c && c <= 'f')
                            || ('A' <= c && c <= 'F')) {
                            ++offset_start;
                        } else {
                          fprintf(stderr, "other character: '%c'\n", *offset_start);
                            return -1;
                        }
                    }
                    num_digits = offset_start - hexdigits;
                    if (num_digits & 1) {
                        fprintf(stderr,"There must be an even number of hexdigits in the serial data\n");
                        return -1;
                    }
                    *offset_start++ = '\0';
                    if( 1 != sscanf(offset_start, "%ld", &serial_offset) ) {
                      fprintf(stderr, "sscanf failed\n");
                      return -1;
                    }
                    serial_data = (int16_t *) malloc( (num_digits/2) * sizeof(int16_t) );
                    for (j=0; j < num_digits; j+=2) {
                      int data;
                      buffer[0] = hexdigits[j];
                      buffer[1] = hexdigits[j+1];
                      buffer[2] = 0;
                      if( 1 != sscanf(buffer, "%02x", &data) ) {
                        fprintf(stderr, "sscanf failed with buffer: %s\n", buffer);
                        return -1;
                      }
                      serial_data[j/2] = (int16_t)data;
                    }
                    args->com_flash_data.serial_data = serial_data;
                    args->com_flash_data.serial_offset = serial_offset;
                    args->com_flash_data.serial_length = num_digits/2;
                    break;
                }
                default:
                    /* not supported. */
                  fprintf(stderr,"command did not match: %d    flash: %d\n", args->command, com_flash);
                    return -1;
            }
            fprintf(stderr, "Success getting serial number\n");
            break;
        }
    }

    return 0;
}

static int32_t assign_com_setfuse_option( struct programmer_arguments *args,
                                            const int32_t parameter,
                                            char *value )
{
    /* name & value */
    if( 0 == parameter ) {
        /* name */
        if( 0 != assign_option((int32_t *) &(args->com_setfuse_data.name),
                               value, setfuse_map) )
        {
            return -1;
        }
    } else {
        int32_t temp = 0;
        /* value */
        if( 1 != sscanf(value, "%i", &(temp)) )
            return -2;

        /* ensure the range is greater than 0 */
        if( temp < 0 )
            return -3;

        args->com_setfuse_data.value = temp;
    }

    return 0;
}

static int32_t assign_com_configure_option( struct programmer_arguments *args,
                                            const int32_t parameter,
                                            char *value )
{
    /* name & value */
    if( 0 == parameter ) {
        /* name */
        if( 0 != assign_option((int32_t *) &(args->com_configure_data.name),
                               value, configure_map) )
        {
            return -1;
        }
    } else {
        int32_t temp = 0;
        /* value */
        if( 1 != sscanf(value, "%i", &(temp)) )
            return -2;

        /* ensure the range is greater than 0 */
        if( temp < 0 )
            return -3;

        args->com_configure_data.value = temp;
    }

    return 0;
}

static int32_t assign_com_flash_option( struct programmer_arguments *args,
                                        const int32_t parameter,
                                        char *value )
{
    /* file */
    args->com_flash_data.original_first_char = *value;
    args->com_flash_data.file = value;

    return 0;
}

static int32_t assign_com_getfuse_option( struct programmer_arguments *args,
                                      const int32_t parameter,
                                      char *value )
{
    /* name */
    if( 0 != assign_option((int32_t *) &(args->com_getfuse_data.name),
                           value, getfuse_map) )
    {
        return -1;
    }

    return 0;
}

static int32_t assign_com_get_option( struct programmer_arguments *args,
                                      const int32_t parameter,
                                      char *value )
{
    /* name */
    if( 0 != assign_option((int32_t *) &(args->com_get_data.name),
                           value, get_map) )
    {
        return -1;
    }

    return 0;
}

static int32_t assign_command_options( struct programmer_arguments *args,
                                       const size_t argc,
                                       char **argv )
{
    size_t i = 0;
    int32_t param = 0;
    int32_t required_params = 0;

    /* Deal with all remaining command-specific arguments. */
    for( i = 0; i < argc; i++ ) {
        if( '\0' == *argv[i] )
            continue;

        switch( args->command ) {
            case com_configure:
                required_params = 2;
                if( 0 != assign_com_configure_option(args, param, argv[i]) )
                    return -1;
                break;

            case com_setfuse:
                required_params = 2;
                if( 0 != assign_com_setfuse_option(args, param, argv[i]) )
                    return -1;
                break;

            case com_flash:
            case com_eflash:
            case com_user:
                required_params = 1;
                if( 0 != assign_com_flash_option(args, param, argv[i]) )
                    return -3;
                break;

            case com_getfuse:
                required_params = 1;
                if( 0 != assign_com_getfuse_option(args, param, argv[i]) )
                    return -4;
                break;
            case com_get:
                required_params = 1;
                if( 0 != assign_com_get_option(args, param, argv[i]) )
                    return -4;
                break;

            default:
                return -5;
        }

        *argv[i] = '\0';
        param++;
    }

    if( required_params != param )
        return -6;

    return 0;
}

static void print_args( struct programmer_arguments *args )
{
    const char *command = "(unknown)";
    const char *target = "(unknown)";
    size_t i;

    for( i = 0; i < sizeof(target_map) / sizeof(target_map[0]); i++ ) {
        if( args->target == target_map[i].value ) {
            target = target_map[i].name;
            break;
        }
    }

    for( i = 0; i < sizeof(command_map) / sizeof(command_map[0]); i++ ) {
        if( args->command == command_map[i].value ) {
            command = command_map[i].name;
            break;
        }
    }

    fprintf( stderr, "     target: %s\n", target );
    fprintf( stderr, "    chip_id: 0x%04x\n", args->chip_id );
    fprintf( stderr, "  vendor_id: 0x%04x\n", args->vendor_id );
    fprintf( stderr, "    command: %s\n", command );
    fprintf( stderr, "      quiet: %s\n", (0 == args->quiet) ? "false" : "true" );
    fprintf( stderr, "      debug: %d\n", debug );
    fprintf( stderr, "device_type: %s\n", args->device_type_string );
    fprintf( stderr, "------ command specific below ------\n" );

    switch( args->command ) {
        case com_configure:
            fprintf( stderr, "       name: %d\n", args->com_configure_data.name );
            fprintf( stderr, "   validate: %s\n",
                     (args->com_configure_data.suppress_validation) ?
                        "false" : "true" );
            fprintf( stderr, "      value: %d\n", args->com_configure_data.value );
            break;
        case com_erase:
            fprintf( stderr, "   validate: %s\n",
                     (args->com_erase_data.suppress_validation) ?
                        "false" : "true" );
            break;
        case com_flash:
        case com_eflash:
        case com_user:
            fprintf( stderr, "   validate: %s\n",
                     (args->com_flash_data.suppress_validation) ?
                        "false" : "true" );
            fprintf( stderr, "   hex file: %s\n", args->com_flash_data.file );
            break;
        case com_get:
            fprintf( stderr, "       name: %d\n", args->com_get_data.name );
            break;
        case com_launch:
            fprintf( stderr, "   no-reset: %d\n", args->com_launch_config.noreset );
            break;
        default:
            break;
    }
    fprintf( stderr, "\n" );
    fflush( stdout );
}

int32_t parse_arguments( struct programmer_arguments *args,
                         const size_t argc,
                         char **argv )
{
    int32_t i;
    int32_t status = 0;

    if( NULL == args )
        return -1;

    /* initialize the argument block to empty, known values */
    args->target  = tar_none;
    args->command = com_none;
    args->quiet   = 0;
    args->suppressbootloader = 0;

    /* Special case - check for the help commands which do not require a device type */
    if( argc == 2 ) {
        if( 0 == strcasecmp(argv[1], "--version") ) {
            fprintf( stderr, PACKAGE_STRING "\n");
            return -1;
        }
        if( 0 == strcasecmp(argv[1], "--targets") ) {
            list_targets();
            return -1;
        }
        if( 0 == strcasecmp(argv[1], "--help") ) {
            usage();
            return -1;
        }
    }

    /* Make sure there are the minimum arguments */
    if( argc < 3 ) {
        basic_help();
        return -1;
    }

    if( 0 != assign_target(args, argv[1], target_map) ) {
        fprintf( stderr, "Unsupported target '%s'.\n", argv[1]);
        status = -3;
        goto done;
    }

    if( 0 != assign_option((int32_t *) &(args->command), argv[2], command_map) ) {
        status = -4;
        goto done;
    }

    /* These were taken care of above. */
    *argv[0] = '\0';
    *argv[1] = '\0';
    *argv[2] = '\0';

    if( 0 != assign_global_options(args, argc, argv) ) {
        status = -5;
        goto done;
    }

    if( 0 != assign_command_options(args, argc, argv) ) {
        status = -6;
        goto done;
    }

    /* Make sure there weren't any *extra* options. */
    for( i = 0; i < argc; i++ ) {
        if( '\0' != *argv[i] ) {
            fprintf( stderr, "unrecognized parameter\n" );
            status = -7;
            goto done;
        }
    }

    /* if this is a flash command, restore the filename */
    if( (com_flash == args->command) || (com_eflash == args->command) || (com_user == args->command) ) {
        if( 0 == args->com_flash_data.file ) {
            fprintf( stderr, "flash filename is missing\n" );
            status = -8;
            goto done;
        }
        args->com_flash_data.file[0] = args->com_flash_data.original_first_char;
    }

done:
    if( 1 < debug ) {
        print_args( args );
    }

    if(-3 == status ) {
        list_targets();
    } else if( 0 != status ) {
        usage();
    }

    return status;
}
