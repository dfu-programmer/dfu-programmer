/*
 * Hardware-free tests for src/intel_hex.c: parsing Intel HEX into a
 * buffer, padding pages, validating a read-back image, and writing a
 * buffer back out as Intel HEX.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#define dup _dup
#define dup2 _dup2
#define fileno _fileno
#define close _close
#else
#include <unistd.h>
#endif

#include "intel_hex.h"
#include "unit.h"

int debug = 0;  /* util.c reads this; it normally lives in libdfu.c */

#define HEX_FILE "test_intel_hex.tmp.hex"
#define OUT_FILE "test_intel_hex.tmp.out"

/* ---- helpers ------------------------------------------------------- */

/* Build one Intel HEX record line, with its checksum, into `line`. */
static void make_record( char *line, uint8_t type, uint16_t address,
                         const uint8_t *data, uint8_t count ) {
    unsigned sum = count + (address >> 8) + (address & 0xff) + type;
    int n = sprintf( line, ":%02X%04X%02X", count, address, type );
    for( uint8_t i = 0; i < count; i++ ) {
        n += sprintf( line + n, "%02X", data[i] );
        sum += data[i];
    }
    sprintf( line + n, "%02X", (0x100 - (sum & 0xff)) & 0xff );
}

static void write_file( const char *path, const char *text ) {
    FILE *fp = fopen( path, "wb" );
    if( NULL == fp ) { perror( path ); exit( 2 ); }
    fputs( text, fp );
    fclose( fp );
}

static int32_t parse_text( const char *text, intel_buffer_out_t *bout,
                           size_t total, size_t page, uint32_t offset ) {
    char path[] = HEX_FILE;
    write_file( path, text );
    if( 0 != intel_init_buffer_out(bout, total, page) ) {
        fprintf( stderr, "intel_init_buffer_out failed\n" );
        exit( 2 );
    }
    return intel_hex_to_buffer( path, bout, offset, true );
}

/* Run intel_hex_from_buffer with stdout redirected to OUT_FILE. */
static int32_t buffer_to_hex_file( intel_buffer_in_t *buin, bool force_full,
                                   uint32_t offset ) {
    int32_t rv;
    int saved;
    FILE *fp;

    fflush( stdout );
    saved = dup( fileno(stdout) );
    fp = fopen( OUT_FILE, "wb" );
    if( saved < 0 || NULL == fp ) { perror( OUT_FILE ); exit( 2 ); }
    dup2( fileno(fp), fileno(stdout) );
    rv = intel_hex_from_buffer( buin, force_full, offset );
    fflush( stdout );
    dup2( saved, fileno(stdout) );
    close( saved );
    fclose( fp );
    return rv;
}

/* Every line of OUT_FILE must be a well-formed record. Returns line count. */
static int check_hex_lines( void ) {
    char line[128];
    int lines = 0;
    FILE *fp = fopen( OUT_FILE, "rb" );
    if( NULL == fp ) { perror( OUT_FILE ); exit( 2 ); }
    while( fgets(line, sizeof(line), fp) ) {
        unsigned count, sum = 0, byte;
        size_t len = strcspn( line, "\r\n" );
        lines++;
        CHECK( ':' == line[0] );
        CHECK( 1 == sscanf(line + 1, "%2x", &count) );
        CHECK_EQ( len, 11 + 2 * count );
        for( size_t i = 1; i + 1 < len; i += 2 ) {
            sscanf( line + i, "%2x", &byte );
            sum += byte;
        }
        CHECK_EQ( sum & 0xff, 0 );
    }
    fclose( fp );
    return lines;
}

/* ---- tests --------------------------------------------------------- */

static void test_init_buffer_out( void ) {
    intel_buffer_out_t bout;
    CHECK_EQ( intel_init_buffer_out(&bout, 1024, 128), 0 );
    CHECK_EQ( bout.info.total_size, 1024 );
    CHECK_EQ( bout.info.page_size, 128 );
    CHECK_EQ( bout.info.data_start, UINT32_MAX );
    CHECK_EQ( bout.info.valid_start, 0 );
    CHECK_EQ( bout.info.valid_end, 1023 );
    int all_unset = 1;
    for( int i = 0; i < 1024; i++ ) all_unset &= (UINT16_MAX == bout.data[i]);
    CHECK( all_unset );
    free( bout.data );

    CHECK( 0 != intel_init_buffer_out(&bout, 0, 128) );
    CHECK( 0 != intel_init_buffer_out(&bout, 1024, 0) );
}

static void test_parse_data_records( void ) {
    intel_buffer_out_t bout;
    uint8_t a[16], b[4] = { 0xAA, 0xBB, 0xCC, 0xDD };
    char text[512], line[64];
    for( int i = 0; i < 16; i++ ) a[i] = (uint8_t) i;
    make_record( line, 0, 0x0000, a, 16 );   strcpy( text, line ); strcat( text, "\n" );
    make_record( line, 0, 0x0100, b, 4 );    strcat( text, line ); strcat( text, "\n" );
    strcat( text, ":00000001FF\n" );

    CHECK_EQ( parse_text(text, &bout, 1024, 128, 0), 0 );
    for( int i = 0; i < 16; i++ ) CHECK_EQ( bout.data[i], i );
    CHECK_EQ( bout.data[16], UINT16_MAX );
    CHECK_EQ( bout.data[0xFF], UINT16_MAX );
    CHECK_EQ( bout.data[0x100], 0xAA );
    CHECK_EQ( bout.data[0x103], 0xDD );
    CHECK_EQ( bout.data[0x104], UINT16_MAX );
    CHECK_EQ( bout.info.data_start, 0 );
    CHECK_EQ( bout.info.data_end, 0x103 );
    free( bout.data );
}

static void test_parse_crlf( void ) {
    intel_buffer_out_t bout;
    uint8_t a[2] = { 0x12, 0x34 };
    char text[256], line[64];
    make_record( line, 0, 0x0010, a, 2 );
    sprintf( text, "%s\r\n:00000001FF\r\n", line );
    CHECK_EQ( parse_text(text, &bout, 256, 64, 0), 0 );
    CHECK_EQ( bout.data[0x10], 0x12 );
    CHECK_EQ( bout.data[0x11], 0x34 );
    free( bout.data );
}

static void test_parse_extended_segment( void ) {
    /* Type 02: segment 0x1000 << 4 = 0x10000 */
    intel_buffer_out_t bout;
    uint8_t seg[2] = { 0x10, 0x00 }, d[1] = { 0x5A };
    char text[256], line[64];
    make_record( line, 2, 0, seg, 2 );  sprintf( text, "%s\n", line );
    make_record( line, 0, 0x0004, d, 1 ); strcat( text, line ); strcat( text, "\n:00000001FF\n" );
    CHECK_EQ( parse_text(text, &bout, 0x20000, 256, 0), 0 );
    CHECK_EQ( bout.data[0x10004], 0x5A );
    CHECK_EQ( bout.info.data_start, 0x10004 );
    free( bout.data );
}

static void test_parse_extended_linear_avr32( void ) {
    /* AVR32 flash is at 0x80000000; the bootloader sees it at 0. */
    intel_buffer_out_t bout;
    uint8_t ela[2] = { 0x80, 0x00 }, d[2] = { 0xC0, 0xDE };
    char text[256], line[64];
    make_record( line, 4, 0, ela, 2 );    sprintf( text, "%s\n", line );
    make_record( line, 0, 0x0020, d, 2 ); strcat( text, line ); strcat( text, "\n:00000001FF\n" );
    CHECK_EQ( parse_text(text, &bout, 0x1000, 512, 0x80000000), 0 );
    CHECK_EQ( bout.data[0x20], 0xC0 );
    CHECK_EQ( bout.data[0x21], 0xDE );
    free( bout.data );
}

static void test_parse_out_of_range( void ) {
    /* Data past the end of memory is counted and skipped, not an error. */
    intel_buffer_out_t bout;
    uint8_t d[4] = { 1, 2, 3, 4 };
    char text[256], line[64];
    make_record( line, 0, 0x00FE, d, 4 );  /* 0xFE, 0xFF fit; 0x100, 0x101 don't */
    sprintf( text, "%s\n:00000001FF\n", line );
    CHECK_EQ( parse_text(text, &bout, 0x100, 64, 0), 2 );
    CHECK_EQ( bout.data[0xFE], 1 );
    CHECK_EQ( bout.data[0xFF], 2 );
    free( bout.data );
}

static void test_parse_errors( void ) {
    intel_buffer_out_t bout;
    char missing[] = "no-such-file.hex";

    /* bad checksum (the valid one would be BE) */
    CHECK_EQ( parse_text(":0100000041BF\n:00000001FF\n", &bout, 256, 64, 0), -5 );
    free( bout.data );
    /* not a record */
    CHECK_EQ( parse_text("hello\n", &bout, 256, 64, 0), -4 );
    free( bout.data );
    /* unsupported record type 6 */
    CHECK_EQ( parse_text(":00000006FA\n", &bout, 256, 64, 0), -5 );
    free( bout.data );
    /* missing end-of-file record */
    CHECK_EQ( parse_text(":0100000041BE\n", &bout, 256, 64, 0), -4 );
    free( bout.data );

    intel_init_buffer_out( &bout, 256, 64 );
    CHECK_EQ( intel_hex_to_buffer(missing, &bout, 0, true), -3 );
    CHECK_EQ( intel_hex_to_buffer(NULL, &bout, 0, true), -2 );
    free( bout.data );
}

static void test_flash_prep_buffer( void ) {
    intel_buffer_out_t bout;
    intel_init_buffer_out( &bout, 512, 128 );
    bout.data[130] = 0x42;                  /* page 1 only */
    CHECK_EQ( intel_flash_prep_buffer(&bout), 0 );
    CHECK_EQ( bout.data[0], UINT16_MAX );   /* page 0 untouched */
    CHECK_EQ( bout.data[127], UINT16_MAX );
    CHECK_EQ( bout.data[128], 0xFF );       /* page 1 padded */
    CHECK_EQ( bout.data[130], 0x42 );
    CHECK_EQ( bout.data[255], 0xFF );
    CHECK_EQ( bout.data[256], UINT16_MAX ); /* page 2 untouched */
    free( bout.data );
}

static void test_validate_buffer( void ) {
    intel_buffer_out_t bout;
    intel_buffer_in_t buin;
    intel_init_buffer_out( &bout, 256, 64 );
    intel_init_buffer_in( &buin, 256, 64 );
    for( int i = 10; i < 20; i++ ) {
        bout.data[i] = (uint16_t) i;
        buin.data[i] = (uint8_t) i;
    }
    CHECK_EQ( intel_validate_buffer(&buin, &bout, true), 0 );

    buin.data[200] = 0x00;                  /* stray byte outside the image */
    CHECK_EQ( intel_validate_buffer(&buin, &bout, true), 1 );

    buin.data[12] = 0x99;                   /* wrong byte inside the image */
    CHECK_EQ( intel_validate_buffer(&buin, &bout, true), -1 );
    free( bout.data );
    free( buin.data );
}

/* Write a buffer as hex, parse it back, and compare byte for byte. */
static void roundtrip( size_t total, size_t page, uint32_t offset,
                       bool force_full, unsigned seed ) {
    intel_buffer_in_t buin;
    intel_buffer_out_t bout;
    char path[] = OUT_FILE;

    intel_init_buffer_in( &buin, total, page );
    srand( seed );
    for( size_t p = 0; p < total; p += page ) {
        if( (rand() % 3) == 0 ) continue;   /* leave about 1/3 of pages blank */
        for( size_t i = p; i < p + page; i++ ) {
            buin.data[i] = (rand() % 5) ? (uint8_t) rand() : 0xFF;
        }
    }

    CHECK_EQ( buffer_to_hex_file(&buin, force_full, offset), 0 );
    CHECK( check_hex_lines() > 0 );

    intel_init_buffer_out( &bout, total, page );
    CHECK_EQ( intel_hex_to_buffer(path, &bout, offset, true), 0 );

    size_t mismatches = 0, first = 0;
    for( size_t i = 0; i < total; i++ ) {
        /* Blank (0xFF) bytes may be written out or skipped. */
        uint16_t got = bout.data[i];
        int ok = (got == buin.data[i]) ||
                 (0xFF == buin.data[i] && UINT16_MAX == got);
        if( !ok && 0 == mismatches++ ) first = i;
    }
    if( mismatches ) {
        fprintf( stderr, "  roundtrip(total=0x%lX, page=%lu, offset=0x%X, "
                 "force_full=%d): %lu bytes differ, first at 0x%lX "
                 "(wrote 0x%02X, read back 0x%04X)\n",
                 (unsigned long) total, (unsigned long) page,
                 (unsigned) offset, force_full, (unsigned long) mismatches,
                 (unsigned long) first, buin.data[first], bout.data[first] );
    }
    CHECK_EQ( mismatches, 0 );
    free( buin.data );
    free( bout.data );
}

static void test_roundtrip_small( void ) {
    roundtrip( 0x800, 128, 0, false, 1 );
    roundtrip( 0x800, 128, 0, true, 2 );
}

static void test_roundtrip_over_64k( void ) {
    /* 128 KiB parts (at90usb1287) need type-04 records past 0xFFFF. */
    roundtrip( 0x20000, 256, 0, false, 3 );
    roundtrip( 0x20000, 256, 0, true, 4 );
}

static void test_roundtrip_avr32( void ) {
    roundtrip( 0x40000, 512, 0x80000000, false, 5 );   /* flash */
    roundtrip( 0x200, 512, 0x80800000, true, 6 );      /* user page */
}

int main( void ) {
    RUN( test_init_buffer_out );
    RUN( test_parse_data_records );
    RUN( test_parse_crlf );
    RUN( test_parse_extended_segment );
    RUN( test_parse_extended_linear_avr32 );
    RUN( test_parse_out_of_range );
    RUN( test_parse_errors );
    RUN( test_flash_prep_buffer );
    RUN( test_validate_buffer );
    RUN( test_roundtrip_small );
    RUN( test_roundtrip_over_64k );
    RUN( test_roundtrip_avr32 );
    remove( HEX_FILE );
    remove( OUT_FILE );
    return UNIT_RESULT();
}
