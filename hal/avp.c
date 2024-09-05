/* hal/avp.c */

/* Copyright (c) 2024 Peter Welch
   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:

   * Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.
   * Redistributions in binary form must reproduce the above copyright
     notice, this list of conditions and the following disclaimer in
     the documentation and/or other materials provided with the
     distribution.
   * Neither the name of the copyright holders nor the names of
     contributors may be used to endorse or promote products derived
     from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.
*/

/* avr programmer for the ICSP and HVPP tasks.
 *
 * usage: avp [-p port]
 *            [-k lockbits]
 *            [-l lowfuses]
 *            [-h highfuses]
 *            [-e extendedfuses] 
 *            [-r flash_readbackfilename] 
 *            [-s eeprom_readbackfilename] 
 *            [-c] chip erase
 *            [file.hex]
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "sys/defs.h"
#include "isp/ihex.h"

/* instead of including avr/iom328p.h */
#define FLASHEND     0x7FFF
#define E2END        0x03FF

#define sig0_str    ":030000060000"
#define sig1_str    ":030000060001"
#define sig2_str    ":030000060002"
#define cal_str     ":030000060003"
#define lbits_str   ":030000060700"
#define lfuse_str   ":030000060701"
#define hfuse_str   ":030000060702"
#define efuse_str   ":030000060703"

#define CLI_CMD "icsp" /* via CLI */
#define INP_CMD "1L" /* via INP */

#define BUF_LEN 80
#define PATH_MAX 32

#define TRUE 1
#define FALSE 0

#define LOW 1
#define HIGH 2
#define EXTENDED 4

#define LINE_MAX 50 /* output buffer */
#define EEPROM_SEGMENT 0x0081

static int lindex;
static char *prog_cmd;
FILE *portin;
FILE *portout;
static char response[BUF_LEN];
static char lbuf[LINE_MAX];

static void usage(void);
static int procfile(FILE *hexfile);
static uchar_t get_nibble(uchar_t c);
static uchar_t get_misc_write_data_value(char *s);
static void bputc(uchar_t c);
static void put_nibble(uchar_t c);
static void print_misc_read_record(uchar_t subfunction, uchar_t selection);
static void print_misc_write_record(uchar_t subfunction, uchar_t selection,
                                                                 uchar_t val);
static void print_erase_record(uchar_t subfunction, uchar_t selection);
static void print_read_data_record(ushort_t start, ushort_t end,
                                                         uchar_t subfunction);
static void print_extended_linear_address_record(ushort_t ulba);

int main(int argc, char **argv)
{
    int opt;
    char *portname = NULL;
    char *flash_readbackfilename = NULL;
    char *eeprom_readbackfilename = NULL;
    FILE *hexfile = NULL;
    uchar_t sig0, sig1, sig2, calib;
    uchar_t lbits, lfuses, hfuses, efuses, tval;
    uchar_t set_lbits = FALSE;
    uchar_t set_lfuses = FALSE;
    uchar_t set_hfuses = FALSE;
    uchar_t set_efuses = FALSE;
    uchar_t chiperase = FALSE;
    ushort_t start, end;
    char cin;
    int ret;
    unsigned int tmp;

    if (argc == 1) {
        usage();
        exit(0);
    }

    while ((opt = getopt(argc, argv, "p:k:l:h:e:r:s:c")) != -1) {
        switch (opt) {
        case 'p':
            portname = optarg;
            break;

        case 'k':
            if (sscanf(optarg, "%x", &tmp) == 1) {
                lbits = (uchar_t) tmp & 0xFF;
                set_lbits = TRUE;
            }
            break;

        case 'l':
            if (sscanf(optarg, "%x", &tmp) == 1) {
                lfuses = (uchar_t) tmp & 0xFF;
                set_lfuses = TRUE;
            }
            break;

        case 'h':
            if (sscanf(optarg, "%x", &tmp) == 1) {
                hfuses = (uchar_t) tmp & 0xFF;
                set_hfuses = TRUE;
            }
            break;

        case 'e':
            if (sscanf(optarg, "%x", &tmp) == 1) {
                efuses = (uchar_t) tmp & 0xFF;
                set_efuses = TRUE;
            }
            break;

        case 'r':
            flash_readbackfilename = optarg;
            break;

        case 's':
            eeprom_readbackfilename = optarg;
            break;

        case 'c':
            chiperase = TRUE;
            break;

        default: /* '?' */
            usage();
            exit(EXIT_FAILURE);
        }
    }

    /* ----------------------------------------------------------------- *
     *                         open the serial port                      *
     * ----------------------------------------------------------------- */

    if (portname == NULL && (portname = getenv("port")) != NULL) {
        portname = strdup(getenv("port"));
    } else {
        printf("$port must be specified on the command line ");
        printf("or in the environment\n");
        exit(1);
    }

    portout = fopen(portname, "w");
    portin = fopen(portname, "r");

    if (portin == NULL || portout == NULL) {
        printf("failed to open port %s\n", portname);
        exit(1);
    }

    /* ----------------------------------------------------------------- *
     *                         open the hexfile                          *
     * ----------------------------------------------------------------- */

    if (optind < argc && argv[optind]) {
        if ((hexfile = fopen(argv[optind], "r")) == NULL) {
            printf("failed to open hexfile %s\n", argv[optind]);
            exit(1);
        }
    }

    /* ----------------------------------------------------------------- *
     *                  ascertain which mode we are in                   * 
     * ----------------------------------------------------------------- */

    fputs("e\n", portout);
    fgets(response, sizeof(response), portin);
    if (strncmp(response, "# ", strlen("# ")) == 0) {
        prog_cmd = INP_CMD;
    } else {
        prog_cmd = CLI_CMD;
    }

    /* ----------------------------------------------------------------- *
     *                         read signature                            *
     * ----------------------------------------------------------------- */

    fprintf(portout, "%s\n", prog_cmd);
    cin = fgetc(portin); /* '.' */
    if (cin != '.') {
        fprintf(stderr, "expected '.', got '%c'\n", cin);
        exit(1);
    }

    print_misc_read_record(IHEX_MISC_READ_SIGNATURE, IHEX_SIGNATURE0);
    fgets(response, sizeof(response), portin);
    if (strncmp(response, sig0_str, strlen(sig0_str))) {
        fprintf(stderr, "expected '%s', got '%s'\n", sig0_str, response);
        exit(1);
    }

    sig0 = get_misc_write_data_value(response);

    fgets(response, sizeof(response), portin);
    if (strncmp(response, "$", strlen("$"))) {
        fprintf(stderr, "expected '$', got '%s'\n", response);
        exit(1);
    }

    fprintf(portout, "%s\n", prog_cmd);
    if ((cin = fgetc(portin)) != '.') {
        fprintf(stderr, "expected '.', got '%c'\n", cin);
        exit(1);
    }
    
    print_misc_read_record(IHEX_MISC_READ_SIGNATURE, IHEX_SIGNATURE1);
    fgets(response, sizeof(response), portin);
    if (strncmp(response, sig1_str, strlen(sig1_str))) {
        fprintf(stderr, "expected '%s', got '%s'\n", sig1_str, response);
        exit(1);
    }

    sig1 = get_misc_write_data_value(response);

    fgets(response, sizeof(response), portin);
    if (strncmp(response, "$", strlen("$"))) {
        fprintf(stderr, "expected '$', got '%s'\n", response);
        exit(1);
    }

    fprintf(portout, "%s\n", prog_cmd);
    if ((cin = fgetc(portin)) != '.') {
        fprintf(stderr, "expected '.', got '%c'\n", cin);
        exit(1);
    }

    print_misc_read_record(IHEX_MISC_READ_SIGNATURE, IHEX_SIGNATURE2);
    fgets(response, sizeof(response), portin);
    if (strncmp(response, sig2_str, strlen(sig2_str))) {
        fprintf(stderr, "expected '%s', got '%s'\n", sig2_str, response);
        exit(1);
    }

    sig2 = get_misc_write_data_value(response);

    fgets(response, sizeof(response), portin);
    if (strncmp(response, "$", strlen("$"))) {
        fprintf(stderr, "expected '$', got '%s'\n", response);
        exit(1);
    }

    fprintf(stderr,"signature: 0x%02X 0x%02X 0x%02X\n", sig0, sig1, sig2);

    /* ----------------------------------------------------------------- *
     *                      read calibration value                       *
     * ----------------------------------------------------------------- */

    fprintf(portout, "%s\n", prog_cmd);
    if ((cin = fgetc(portin)) != '.') {
        fprintf(stderr, "expected '.', got '%c'\n", cin);
        exit(1);
    }

    print_misc_read_record(IHEX_MISC_READ_SIGNATURE, IHEX_CALIBRATION_BYTE);
    fgets(response, sizeof(response), portin);
    if (strncmp(response, cal_str, strlen(cal_str))) {
        fprintf(stderr, "expected '%s', got '%s'\n", cal_str, response);
        exit(1);
    }

    calib = get_misc_write_data_value(response);

    fgets(response, sizeof(response), portin);
    if (strncmp(response, "$", strlen("$"))) {
        fprintf(stderr, "expected '$', got '%s'\n", response);
        exit(1);
    }

    fprintf(stderr,"calibration byte: 0x%02X\n", calib);

    /* ----------------------------------------------------------------- *
     *                          read low fuses                           *
     * ----------------------------------------------------------------- */

    fprintf(portout, "%s\n", prog_cmd);
    if ((cin = fgetc(portin)) != '.') {
        fprintf(stderr, "expected '.', got '%c'\n", cin);
        exit(1);
    }

    print_misc_read_record(IHEX_MISC_READ_FUSES, IHEX_LOW_FUSE);
    fgets(response, sizeof(response), portin);
    if (strncmp(response, lfuse_str, strlen(lfuse_str))) {
        fprintf(stderr, "expected '%s', got '%s'\n", lfuse_str, response);
        exit(1);
    }

    tval = get_misc_write_data_value(response);

    fgets(response, sizeof(response), portin);
    if (strncmp(response, "$", strlen("$"))) {
        fprintf(stderr, "expected '$', got '%s'\n", response);
        exit(1);
    }

    fprintf(stderr,"low fuses:        0x%02X", tval);

    if (set_lfuses && lfuses != tval) {
        /* write low fuses */
        fprintf(portout, "%s\n", prog_cmd);
        if ((cin = fgetc(portin)) != '.') {
            fprintf(stderr, "expected '.', got '%c'\n", cin);
            exit(1);
        }

        print_misc_write_record(IHEX_MISC_WRITE_FUSES, IHEX_LOW_FUSE, lfuses);
        fgets(response, sizeof(response), portin);
        if (strncmp(response, "$", strlen("$"))) {
            fprintf(stderr, "expected '$', got '%s'\n", response);
            exit(1);
        }

        /* readback the newly-written low fuses */
        fprintf(portout, "%s\n", prog_cmd);
        if ((cin = fgetc(portin)) != '.') {
            fprintf(stderr, "expected '.', got '%c'\n", cin);
            exit(1);
        }

        print_misc_read_record(IHEX_MISC_READ_FUSES, IHEX_LOW_FUSE);
        fgets(response, sizeof(response), portin);
        if (strncmp(response, lfuse_str, strlen(lfuse_str))) {
            fprintf(stderr, "expected '%s', got '%s'\n", lfuse_str, response);
            exit(1);
        }

        tval = get_misc_write_data_value(response);

        fgets(response, sizeof(response), portin);
        if (strncmp(response, "$", strlen("$"))) {
            fprintf(stderr, "expected '$', got '%s'\n", response);
            exit(1);
        }

        fprintf(stderr," -> 0x%02X", tval);
    }
    fprintf(stderr,"\n");

    /* ----------------------------------------------------------------- *
     *                         read high fuses                           *
     * ----------------------------------------------------------------- */

    /* read high fuses */
    fprintf(portout, "%s\n", prog_cmd);
    if ((cin = fgetc(portin)) != '.') {
        fprintf(stderr, "expected '.', got '%c'\n", cin);
        exit(1);
    }

    print_misc_read_record(IHEX_MISC_READ_FUSES, IHEX_HIGH_FUSE);
    fgets(response, sizeof(response), portin);
    if (strncmp(response, hfuse_str, strlen(hfuse_str))) {
        fprintf(stderr, "expected '%s', got '%s'\n", hfuse_str, response);
        exit(1);
    }

    tval = get_misc_write_data_value(response);

    fgets(response, sizeof(response), portin);
    if (strncmp(response, "$", strlen("$"))) {
        fprintf(stderr, "expected '$', got '%s'\n", response);
        exit(1);
    }

    fprintf(stderr,"high fuses:       0x%02X", tval);

    if (set_hfuses && hfuses != tval) {
        fprintf(portout, "%s\n", prog_cmd);
        if ((cin = fgetc(portin)) != '.') {
            fprintf(stderr, "expected '.', got '%c'\n", cin);
            exit(1);
        }
        print_misc_write_record(IHEX_MISC_WRITE_FUSES, IHEX_HIGH_FUSE, hfuses);

        fgets(response, sizeof(response), portin);
        if (strncmp(response, "$", strlen("$"))) {
            fprintf(stderr, "expected '$', got '%s'\n", response);
            exit(1);
        }

        /* readback the newly-written high fuses */

        fprintf(portout, "%s\n", prog_cmd);
        if ((cin = fgetc(portin)) != '.') {
            fprintf(stderr, "expected '.', got '%c'\n", cin);
            exit(1);
        }

        print_misc_read_record(IHEX_MISC_READ_FUSES, IHEX_HIGH_FUSE);
        fgets(response, sizeof(response), portin);
        if (strncmp(response, hfuse_str, strlen(hfuse_str))) {
            fprintf(stderr, "expected '%s', got '%s'\n", hfuse_str, response);
            exit(1);
        }

        tval = get_misc_write_data_value(response);

        fgets(response, sizeof(response), portin);
        if (strncmp(response, "$", strlen("$"))) {
            fprintf(stderr, "expected '$', got '%s'\n", response);
            exit(1);
        }

        fprintf(stderr," -> 0x%02X", tval);
    }
    fprintf(stderr,"\n");

    /* ----------------------------------------------------------------- *
     *                      read extended fuses                          *
     * ----------------------------------------------------------------- */

    fprintf(portout, "%s\n", prog_cmd);
    if ((cin = fgetc(portin)) != '.') {
        fprintf(stderr, "expected '.', got '%c'\n", cin);
        exit(1);
    }

    print_misc_read_record(IHEX_MISC_READ_FUSES, IHEX_EXTENDED_FUSE);
    fgets(response, sizeof(response), portin);
    if (strncmp(response, efuse_str, strlen(efuse_str))) {
        fprintf(stderr, "expected '%s', got '%s'\n", efuse_str, response);
        exit(1);
    }

    tval = get_misc_write_data_value(response);

    fgets(response, sizeof(response), portin);
    if (strncmp(response, "$", strlen("$"))) {
        fprintf(stderr, "expected '$', got '%s'\n", response);
        exit(1);
    }

    fprintf(stderr,"extended fuses:   0x%02X", tval);

    if (set_efuses && efuses != tval) {
        fprintf(portout, "%s\n", prog_cmd);
        if ((cin = fgetc(portin)) != '.') {
            fprintf(stderr, "expected '.', got '%c'\n", cin);
            exit(1);
        }

        print_misc_write_record(IHEX_MISC_WRITE_FUSES, IHEX_EXTENDED_FUSE,
                                                                      efuses);
        fgets(response, sizeof(response), portin);
        if (strncmp(response, "$", strlen("$"))) {
            fprintf(stderr, "expected '$', got '%s'\n", response);
            exit(1);
        }

        /* readback the newly-written extended fuses */

        fprintf(portout, "%s\n", prog_cmd);
        if ((cin = fgetc(portin)) != '.') {
            fprintf(stderr, "expected '.', got '%c'\n", cin);
            exit(1);
        }

        print_misc_read_record(IHEX_MISC_READ_FUSES, IHEX_EXTENDED_FUSE);
        fgets(response, sizeof(response), portin);
        if (strncmp(response, efuse_str, strlen(efuse_str))) {
            fprintf(stderr, "expected '%s', got '%s'\n", efuse_str, response);
            exit(1);
        }

        tval = get_misc_write_data_value(response);

        fgets(response, sizeof(response), portin);
        if (strncmp(response, "$", strlen("$"))) {
            fprintf(stderr, "expected '$', got '%s'\n", response);
            exit(1);
        }

        fprintf(stderr," -> 0x%02X", tval);
    }
    fprintf(stderr,"\n");

    /* ----------------------------------------------------------------- *
     *                            chip erase                             *
     * ----------------------------------------------------------------- */

    if (chiperase) {
        fprintf(stderr, "erase:");

        fprintf(portout, "%s\n", prog_cmd);
        if ((cin = fgetc(portin)) != '.') {
            fprintf(stderr, "expected '.', got '%c'\n", cin);
            exit(1);
        }

        print_erase_record(IHEX_ERASE_MEMORY, IHEX_FLASH_MEMORY);
        fgets(response, sizeof(response), portin);
        if (strncmp(response, "$", strlen("$"))) {
            fprintf(stderr, "expected '$', got '%s'\n", response);
            exit(1);
        }

        /* blank check */

        fprintf(portout, "%s\n", prog_cmd);
        if ((cin = fgetc(portin)) != '.') {
            fprintf(stderr, "expected '.', got '%c'\n", cin);
            exit(1);
        }

        start = 0x0000;
        end = FLASHEND;

        fprintf(stderr,"flash:  ");

        print_read_data_record(start, end, IHEX_BLANK_CHECK);
        fgets(response, sizeof(response), portin);
        if (strncmp(response, "blank", strlen("blank"))) {
            fprintf(stderr, "expected 'blank', got '%s'\n", response);
            exit(1);
        }

        fprintf(stderr,"blank\n");

        fgets(response, sizeof(response), portin);
        if (strncmp(response, "$", strlen("$"))) {
            fprintf(stderr, "expected '$', got '%s'\n", response);
            exit(1);
        }

        /* ----------------------------------------------------------------- *
         *                           eeprom erase                            *
         * ----------------------------------------------------------------- */

        fprintf(portout, "%s\n", prog_cmd);
        if ((cin = fgetc(portin)) != '.') {
            fprintf(stderr, "expected '.', got '%c'\n", cin);
            exit(1);
        }

        start = 0x0000;
        end = E2END;

        print_extended_linear_address_record(EEPROM_SEGMENT);
        if ((cin = fgetc(portin)) != '.') {
            fprintf(stderr, "expected '.', got '%c'\n", cin);
            exit(1);
        }

        fprintf(stderr,"eeprom: ");

        print_read_data_record(start, end, IHEX_BLANK_CHECK);
        fgets(response, sizeof(response), portin);
        if (strncmp(response, "blank", strlen("blank"))) {
            fprintf(stderr, "expected 'blank', got '%s'\n", response);
            exit(1);
        }

        fprintf(stderr,"blank\n");

        fgets(response, sizeof(response), portin);
        if (strncmp(response, "$", strlen("$"))) {
            fprintf(stderr, "expected '$', got '%s'\n", response);
            exit(1);
        }
    }

    /* ----------------------------------------------------------------- *
     *                           program flash                           *
     * ----------------------------------------------------------------- */

    if (hexfile != NULL) {
        ret = procfile(hexfile);
        fclose(hexfile);
        if (ret != 0)
            exit(ret);
    }

    /* ----------------------------------------------------------------- *
     *                          readback flash                           *
     * ----------------------------------------------------------------- */

    if (flash_readbackfilename) {
        FILE *fp;
        if ((fp = fopen(flash_readbackfilename, "w")) != NULL) {

            fprintf(stdout, "readback flash\n");

            fprintf(portout, "%s\n", prog_cmd);
            if ((cin = fgetc(portin)) != '.') {
                fprintf(stderr, "expected '.', got '%c'\n", cin);
                exit(1);
            }

            ushort_t start = 0x0000;
            ushort_t end = FLASHEND;
            int progress = 0;
            int percent;
            int prevpercent = -1;
            int nlines = ((end - start + 1) >> 4) +1;

            print_read_data_record(start, end, IHEX_DISPLAY_DATA);

            while (fgets(response, sizeof(response), portin)) {
                if (response[0] != ':')
                    break;
                fputs(response, fp);
                progress++;
                percent = (int)((long)progress * 100L / nlines);
                if (prevpercent != percent) {
                    prevpercent = percent;
                    fprintf(stdout, "\r%3d%% ", percent);
                }
            }
            fputc('\n', stdout);
            fclose(fp);
        }
    }

    /* ----------------------------------------------------------------- *
     *                          readback eeprom                          *
     * ----------------------------------------------------------------- */

    if (eeprom_readbackfilename) {
        FILE *fp;
        if ((fp = fopen(eeprom_readbackfilename, "w")) != NULL) {

            fprintf(stdout, "readback eeprom\n");

            fprintf(portout, "%s\n", prog_cmd);
            if ((cin = fgetc(portin)) != '.') {
                fprintf(stderr, "expected '.', got '%c'\n", cin);
                exit(1);
            }

            print_extended_linear_address_record(EEPROM_SEGMENT);
            if ((cin = fgetc(portin)) != '.') {
                fprintf(stderr, "expected '.', got '%c'\n", cin);
                exit(1);
            }

            ushort_t start = 0x0000;
            ushort_t end = E2END;
            int progress = 0;
            int percent;
            int prevpercent = -1;
            int nlines = ((end - start + 1) >> 4) +1;
            
            print_read_data_record(start, end, IHEX_DISPLAY_DATA);
            
            while (fgets(response, sizeof(response), portin)) {
                if (response[0] != ':')
                    break;
                fputs(response, fp);
                progress++;
                percent = (int)((long)progress * 100L / nlines);
                if (prevpercent != percent) {
                    prevpercent = percent;
                    fprintf(stdout, "\r%3d%% ", percent);
                }
            }
            fputc('\n', stdout);
            fclose(fp);
        }
    }

    /* ----------------------------------------------------------------- *
     *                            set lock bits                          *
     * ----------------------------------------------------------------- */

    /* read lock bits */
    fprintf(portout, "%s\n", prog_cmd);
    if ((cin = fgetc(portin)) != '.') {
        fprintf(stderr, "expected '.', got '%c'\n", cin);
        exit(1);
    }

    print_misc_read_record(IHEX_MISC_READ_FUSES, IHEX_LOCKBITS);
    fgets(response, sizeof(response), portin);
    if (strncmp(response, lbits_str, strlen(lbits_str))) {
        fprintf(stderr, "expected '%s', got '%s'\n", lbits_str, response);
        exit(1);
    }

    tval = get_misc_write_data_value(response);

    fgets(response, sizeof(response), portin);
    if (strncmp(response, "$", strlen("$"))) {
        fprintf(stderr, "expected '$', got '%s'\n", response);
        exit(1);
    }

    fprintf(stderr,"lock bits:        0x%02X", tval);

    if (set_lbits && lbits != tval) {
        fprintf(portout, "%s\n", prog_cmd);
        if ((cin = fgetc(portin)) != '.') { 
            fprintf(stderr, "expected '.', got '%c'\n", cin);
            exit(1);
        }

        print_misc_write_record(IHEX_MISC_WRITE_FUSES, IHEX_LOCKBITS, lbits);
        fgets(response, sizeof(response), portin);
        if (strncmp(response, "$", strlen("$"))) {
            fprintf(stderr, "expected '$', got '%s'\n", response);
            exit(1);
        }

        /* read lock bits */
        fprintf(portout, "%s\n", prog_cmd);
        if ((cin = fgetc(portin)) != '.') {
            fprintf(stderr, "expected '.', got '%c'\n", cin);
            exit(1);
        }

        print_misc_read_record(IHEX_MISC_READ_FUSES, IHEX_LOCKBITS);
        fgets(response, sizeof(response), portin);
        if (strncmp(response, lbits_str, strlen(lbits_str))) {
            fprintf(stderr, "expected '%s', got '%s'\n", lbits_str, response);
            exit(1);
        }

        tval = get_nibble(toupper(response[13])) << 4 |
               get_nibble(toupper(response[14]));

        fgets(response, sizeof(response), portin);
        if (strncmp(response, "$", strlen("$"))) {
            fprintf(stderr, "expected '$', got '%s'\n", response);
            exit(1);
        }
        fprintf(stderr," -> 0x%02X", tval);
    }
    fprintf(stderr,"\n");

    fclose(portin);
    fclose(portout);
    exit(0);
}

static void usage(void)
{
    fprintf(stderr, "Usage: avp [-p port]\n");
    fprintf(stderr, "           [-k lockbits]\n");
    fprintf(stderr, "           [-l lowfuses]\n");
    fprintf(stderr, "           [-h highfuses]\n");
    fprintf(stderr, "           [-e extendedfuses]\n");
    fprintf(stderr, "           [-r flash_readbackfilename]\n");
    fprintf(stderr, "           [-s eeprom_readbackfilename]\n");
    fprintf(stderr, "           [-c] chip erase\n");
    fprintf(stderr, "           [file.hex]\n");
}

static int procfile(FILE *hexfile)
{
    char line[BUF_LEN];
    int ret = 0;
    int nlines = 0;
    int progress = 0;
    int percent;
    int prevpercent = -1;
    ushort_t start, end;
    char cin;

    /* Read the entire hexfile, counting the lines.
     * A more thorough sanity check could be performed.
     */
    while ((fgets(line, sizeof(line), hexfile)) != NULL) {
        if (line[0] == ':' && line[7] == '0') {
            switch (line[8] - '0') {
            case IHEX_DATA_RECORD:
            case IHEX_END_OF_FILE_RECORD:
            case IHEX_EXTENDED_LINEAR_ADDRESS_RECORD:
                nlines++;
                break;
            default:
                fprintf(stderr, "unhandled record type %c%c at line %d\n",
                              line[7], line[8], nlines +1);
                return(1);
            }
        } else {
            fprintf(stderr, "not a record at line %d: %s\n", nlines +1, line);
            return(1);
        }
    }

    if (nlines == 0) {
        printf("zero lines\n");
        return(1);
    }

    fseek(hexfile, 0L, SEEK_SET);

    fprintf(portout, "%s\n", prog_cmd);
    if ((cin = fgetc(portin)) != '.') {
        fprintf(stderr, "expected '.', got '%c'\n", cin);
        return(1);
    }

    start = 0x0000;
    end = FLASHEND;

    print_read_data_record(start, end, IHEX_BLANK_CHECK);
    fgets(response, sizeof(response), portin);
    if (strncmp(response, "blank", strlen("blank"))) {
        fgets(response, sizeof(response), portin);
        if (strncmp(response, "$", strlen("$"))) {
            fprintf(stderr, "expected '$', got '%s'\n", response);
            return(1);
        }

        fprintf(stderr, "erase:");

        fprintf(portout, "%s\n", prog_cmd);
        if ((cin = fgetc(portin)) != '.') {
            fprintf(stderr, "expected '.', got '%c'\n", cin);
            return(1);
        }

        print_erase_record(IHEX_ERASE_MEMORY, IHEX_FLASH_MEMORY);
        fgets(response, sizeof(response), portin);
        if (strncmp(response, "$", strlen("$"))) {
            fprintf(stderr, "expected '$', got '%s'\n", response);
            return(1);
        }

        fprintf(portout, "%s\n", prog_cmd);
        if ((cin = fgetc(portin)) != '.') {
            fprintf(stderr, "expected '.', got '%c'\n", cin);
            return(1);
        }

        start = 0x0000;
        end = FLASHEND;

        print_read_data_record(start, end, IHEX_BLANK_CHECK);
        fgets(response, sizeof(response), portin);
        if (strncmp(response, "blank", strlen("blank"))) {
            fprintf(stderr, " failed at %s\n", response);
            return(1);
        } else {
            fprintf(stderr, " ok\n");
        }
    }

    fgets(response, sizeof(response), portin);
    if (strncmp(response, "$", strlen("$"))) {
        fprintf(stderr, "expected '$', got '%s'\n", response);
        return(1);
    }

    /* setup to write the hexfile */
    fprintf(portout, "%s\n", prog_cmd);
    if ((cin = fgetc(portin)) != '.') {
        fprintf(stderr, "expected '.', got '%c'\n", cin);
        return(1);
    }

    while ((fgets(line, sizeof(line), hexfile)) != NULL) {
        progress++;
        fputs(line, portout);
        if ((cin = fgetc(portin)) != '.') {
            /* a dollar sign after the last line indicates success */
            if (progress == nlines && cin == '$') {
                fprintf(stdout, "\r%3d%% ", 100);
                fgets(response, sizeof(response), portin);
            } else {
                ret = 1;
            }
            fputc('\n', stdout);
            break;
        } else {
            percent = (int)((long)progress * 100L / nlines);
            if (prevpercent != percent) {
                prevpercent = percent;
                fprintf(stdout, "\r%3d%% ", percent);
            }
        }
    }
    return(ret);
}

static uchar_t get_nibble(uchar_t c) {
    return(c > '9' ? c - 'A' + 10 : c - '0');
}

static uchar_t get_misc_write_data_value(char *s)
{
    return (get_nibble(toupper(s[13])) << 4 | get_nibble(toupper(s[14])));
}


static void bputc(uchar_t c)
{
    if (lindex < LINE_MAX)
        lbuf[lindex++] = c;
}

static void put_nibble(uchar_t v)
{
    bputc((v < 10 ? '0' : '7') + v);
}

static void puthex(uchar_t ch)
{
#define HIGH_NIBBLE(c)         ((c) >> 4 & 0x0f)
#define LOW_NIBBLE(c)          ((c) & 0x0f)

    put_nibble(HIGH_NIBBLE(ch));
    put_nibble(LOW_NIBBLE(ch));
}

static void print_erase_record(uchar_t subfunction, uchar_t selection)
{
    lindex = 0;
    uchar_t len = 2;
    bputc(':');
    puthex(len);
    uchar_t sum = len;
    puthex(0x00);
    sum += 0x00;
    puthex(0x00);
    sum += 0x00;
    puthex(IHEX_ERASE_RECORD);
    sum += IHEX_ERASE_RECORD;
    puthex(subfunction);
    sum += subfunction;
    puthex(selection);
    sum += selection;
    puthex(-sum);

    bputc('\n');

    fputs(lbuf, portout);
}

static void print_misc_read_record(uchar_t subfunction, uchar_t selection)
{
    lindex = 0;
    uchar_t len = 2;
    bputc(':');
    puthex(len);
    uchar_t sum = len;
    puthex(0x00);
    sum += 0x00;
    puthex(0x00);
    sum += 0x00;
    puthex(IHEX_MISC_READ_RECORD);
    sum += IHEX_MISC_READ_RECORD;
    puthex(subfunction);
    sum += subfunction;
    puthex(selection);
    sum += selection;
    puthex(-sum);

    bputc('\n');

    fputs(lbuf, portout);
}

static void print_misc_write_record(uchar_t subfunction, uchar_t selection,
                                                                 uchar_t val)
{
    lindex = 0;
    uchar_t len = 3;
    bputc(':');
    puthex(len);
    uchar_t sum = len;
    puthex(0x00);
    sum += 0x00;
    puthex(0x00);
    sum += 0x00;
    puthex(IHEX_MISC_WRITE_RECORD);
    sum += IHEX_MISC_WRITE_RECORD;
    puthex(subfunction);
    sum += subfunction;
    puthex(selection);
    sum += selection;
    puthex(val);
    sum += val;
    puthex(-sum);

    bputc('\n');

    fputs(lbuf, portout);
}

static void print_extended_linear_address_record(ushort_t ulba)
{
    lindex = 0;
    uchar_t len = 5;
    bputc(':');
    puthex(len);
    uchar_t sum = len;
    puthex(0x00);
    sum += 0x00;
    puthex(0x00);
    sum += 0x00;
    puthex(IHEX_EXTENDED_LINEAR_ADDRESS_RECORD);
    sum += IHEX_EXTENDED_LINEAR_ADDRESS_RECORD;
    uchar_t c = ulba >> 8;
    puthex(c);
    sum += c;
    c = ulba & 0xFF;
    puthex(c);
    sum += c;
    puthex(-sum);

    bputc('\n');

    fputs(lbuf, portout);
}

static void print_read_data_record(ushort_t start, ushort_t end,
                                                        uchar_t subfunction)
{
    lindex = 0;
    uchar_t len = 5;
    bputc(':');
    puthex(len);
    uchar_t sum = len;
    puthex(0x00);
    sum += 0x00;
    puthex(0x00);
    sum += 0x00;
    puthex(IHEX_READ_DATA_RECORD);
    sum += IHEX_READ_DATA_RECORD;
    uchar_t c = start >> 8;
    puthex(c);
    sum += c;
    c = start & 0xFF;
    puthex(c);
    sum += c;
    c = end >> 8;
    puthex(c);
    sum += c;
    c = end & 0xFF;
    puthex(c);
    sum += c;

    puthex(subfunction);
    sum += subfunction;
    puthex(-sum);

    bputc('\n');

    fputs(lbuf, portout);
}

/* end code */
