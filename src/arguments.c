/*
 * dfu-programmer
 *
 * $Id$
 *
 * Copyright (C) 2005 Weston Schmidt <weston_schmidt@yahoo.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arguments.h"

struct option_mapping_structure {
    const char *name;
    int value;
};

struct target_mapping_structure {
    const char *name;
    int value;
    unsigned short chip_id;
    unsigned short vendor_id;
};

/* ----- target specific structures ----------------------------------------- */
static struct target_mapping_structure target_map[] = {
    { "at89c51snd1c", tar_at89c51snd1c, 0x2FFF, 0x03eb },
    { NULL }
};

/* ----- command specific structures ---------------------------------------- */
static struct option_mapping_structure command_map[] = {
    { "configure", com_configure },
    { "dump",      com_dump      },
    { "erase",     com_erase     },
    { "flash",     com_flash     },
    { "get",       com_get       },
    { "start",     com_start_app },
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


static void usage()
{
    fprintf( stderr, "Usage: dfu-programmer target command [command-options] "
                     "[global-options] [file|data]\n" );
    fprintf( stderr, "targets:\n" );
    fprintf( stderr, "        at89c51snd1c\n" );
    fprintf( stderr, "global-options: --quiet, --debug level\n" );
    fprintf( stderr, "commands:\n" );
    fprintf( stderr, "        configure {BSB|SBV|SSB|EB|HSB} "
                     "[--suppress-validation] [global-options] data\n" );
    fprintf( stderr, "        dump "
                     "[global-options]\n" );
    fprintf( stderr, "        erase "
                     "[--suppress-validation] [global-options]\n" );
    fprintf( stderr, "        flash "
                     "[--suppress-validation] [global-options] file\n" );
    fprintf( stderr, "        get {bootloader-version|ID1|ID2|BSB|SBV|SSB|EB|\n"
                     "            manufacturer|family|product-name|\n"
                     "            product-revision|HSB} "
                     "[global-options]\n" );
    fprintf( stderr, "        start [global-options]\n" );
}

static int assign_option( int *arg,
                          char *value,
                          struct option_mapping_structure *map )
{
    while( 0 != *((int *) map) ) {
        if( 0 == strcmp(value, map->name) ) {
            *arg = map->value;
            return 0;
        }

        map++;
    }

    return -1;
}


static int assign_target( struct programmer_arguments *args,
                          char *value,
                          struct target_mapping_structure *map )
{
    while( 0 != *((int *) map) ) {
        if( 0 == strcmp(value, map->name) ) {
            args->target  = map->value;
            args->chip_id = map->chip_id;
            args->vendor_id = map->vendor_id;
            return 0;
        }

        map++;
    }

    return -1;
}


static int assign_global_options( struct programmer_arguments *args,
                                  int argc,
                                  char **argv )
{
    int i = 0;

    /* Find '--quiet' if it is here */
    for( i = 0; i < argc; i++ ) {
        if( 0 == strcmp("--quiet", argv[i]) ) {
            *argv[i] = '\0';
            args->quiet = 1;
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
                    args->com_flash_data.suppress_validation = 1;
                    break;
                default:
                    /* not supported. */
                    return -1;
            }

            break;
        }
    }


    /* Find '--debug' if it is here */
    for( i = 0; i < argc; i++ ) {
        if( 0 == strncmp("--debug", argv[i], 7) ) {

            if( 0 == strncmp("--debug=", argv[i], 8) ) {
                if( 1 != sscanf(argv[i], "--debug=%i", &(args->debug)) )
                    return -2;
            } else {
                if( (i+1) >= argc )
                    return -3;

                if( 1 != sscanf(argv[i+1], "%i", &(args->debug)) )
                    return -4;

                *argv[i+1] = '\0';
            }
            *argv[i] = '\0';
            break;
        }
    }

    return 0;
}


static int assign_com_configure_option( struct programmer_arguments *args,
                                        int parameter,
                                        char *value )
{
    /* name & value */
    if( 0 == parameter ) {
        /* name */
        if( 0 != assign_option((int *) &(args->com_configure_data.name),
                               value, configure_map) )
        {
            return -1;
        }
    } else {
        int temp = 0;
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


static int assign_com_flash_option( struct programmer_arguments *args,
                                    int parameter,
                                    char *value )
{
    /* file */
    args->com_flash_data.original_first_char = *value;
    args->com_flash_data.file = value;

    return 0;
}


static int assign_com_get_option( struct programmer_arguments *args,
                                  int parameter,
                                  char *value )
{
    /* name */
    if( 0 != assign_option((int *) &(args->com_get_data.name),
                           value, get_map) )
    {
        return -1;
    }

    return 0;
}


static int assign_command_options( struct programmer_arguments *args,
                                   int argc,
                                   char **argv )
{
    int i = 0;
    int param = 0;
    int required_params = 0;

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

            case com_flash:
                required_params = 1;
                if( 0 != assign_com_flash_option(args, param, argv[i]) )
                    return -3;
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
    char *command = NULL;

    printf( " target: %s\n", 
                (args->target == tar_none) ? "none" : "at89c51snd1c" );
    printf( "chip_id: 0x%04x\n", args->chip_id );
    printf( "vendor_id: 0x%04x\n", args->vendor_id );

    switch( args->command ) {
        case com_none:
            command = "none";
            break;
        case com_erase:
            command = "erase";
            break;
        case com_flash:
            command = "flash";
            break;
        case com_configure:
            command = "configure";
            break;
        case com_get:
            command = "get";
            break;
        case com_dump:
            command = "dump";
            break;
        case com_start_app:
            command = "start";
            break;
    }

    printf( "command: %s\n", command );
    printf( "  quiet: %s\n", (0 == args->quiet) ? "false" : "true" );
    printf( "  debug: %d\n", args->debug );

    switch( args->command ) {
        case com_none:
            break;
        case com_configure:
            printf( "name: %d\n", args->com_configure_data.name );
            printf( "suppress validation: %s\n",
                    (0 == args->com_configure_data.suppress_validation) ?
                        "false" : "true" );
            printf( "value: %d\n", args->com_configure_data.value );
            break;
        case com_dump:
            break;
        case com_start_app:
            break;
        case com_erase:
            printf( "suppress validation: %s\n",
                    (0 == args->com_erase_data.suppress_validation) ?
                        "false" : "true" );
            break;
        case com_flash:
            printf( "suppress validation: %s\n",
                    (0 == args->com_flash_data.suppress_validation) ?
                        "false" : "true" );
            printf( "file: %s\n", args->com_flash_data.file );
            break;
        case com_get:
            printf( "name: %d\n", args->com_get_data.name );
            break;
    }

}


int parse_arguments( struct programmer_arguments *args,
                     int argc,
                     char **argv )
{
    int i;

    if( NULL == args )
        return -1;

    /* initialize the argument block to empty, known values */
    args->target  = tar_none;
    args->command = com_none;
    args->debug   = 0;
    args->quiet   = 0;

    /* Make sure there are the minimum arguments */
    if( argc < 3 ) {
        usage();
        return -2;
    }

    if( 0 != assign_target(args, argv[1], target_map) ) {
        usage();
        return -3;
    }

    if( 0 != assign_option((int *) &(args->command), argv[2], command_map) ) {
        usage();
        return -4;
    }

    /* These were taken care of above. */
    *argv[0] = '\0';
    *argv[1] = '\0';
    *argv[2] = '\0';

    if( 0 != assign_global_options(args, argc, argv) ) {
        usage();
        return -5;
    }

    if( 0 != assign_command_options(args, argc, argv) ) {
        usage();
        return -6;
    }

    /* Make sure there weren't any *extra* options. */
    for( i = 0; i < argc; i++ ) {
        if( '\0' != *argv[i] )
            return -7;
    }

    /* if this is a flash command, restore the filename */
    if( com_flash == args->command ) {
        args->com_flash_data.file[0] = args->com_flash_data.original_first_char;
    }

    return 0;
}
