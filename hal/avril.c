/* hal/avril.c */

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

/* AVR internal loader that communicates with ISP on bali.
 * The ISP can write to the flash and the eeprom.
 *
 * usage: avril hostname [hostname ...]
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "sys/defs.h"
#include "isp/ihex.h"

#define BUF_LEN 80
#define PATH_MAX 32

static int procfile(char *hostname);

FILE *portin;
FILE *portout;

char response[BUF_LEN];

int main(int argc, char **argv)
{
    char *portname = NULL;

    if ((portname = getenv("port")) != NULL) {
        portname = strdup(getenv("port"));
    } else {
        printf("$port must be set in the environment\n");
        exit(1);
    }

    portout = fopen(portname, "w");
    portin = fopen(portname, "r");

    if (portin == NULL || portout == NULL) {
        printf("failed to open port: %s\n", portname);
        exit(1);
    }

    /* Check the input mode: send an 'e' and test the response.
     * If it's in INP, switch to CLI
     */
    fputs("e\n", portout);
    fgets(response, sizeof(response), portin);
    if (!strncmp(response, "# ", strlen("# "))) {
        /* talking to the INP */
        fputs("a\n", portout); 
        fgets(response, sizeof(response), portin);
        if (strncmp(response, "in cli", strlen("in cli"))) {
            printf("failed to enter CLI\n");
            exit(1);
        }
    } else if (strncmp(response, "e ", strlen("e "))) {
        printf("not talking to the CLI\n");
        exit(1);
    }

    for (int i = 1; i < argc; i++) {
        procfile(argv[i]);
    }

    fclose(portin);
    fclose(portout);
    exit(0);
}

static int procfile(char *hostname)
{
    char *hexfilename;
    FILE *hexfile;
    char line[BUF_LEN];
    int ret = 0;
    int nlines = 0;
    int progress = 0;
    int percent;
    int prevpercent = -1;
    char cin;

    if ((hexfilename = malloc(PATH_MAX)) != NULL) {
        sprintf(hexfilename, "../%s/%s.hex", hostname, hostname);
        if ((hexfile = fopen(hexfilename, "r")) == NULL) {
            printf("failed to open %s\n", hexfilename);
            free(hexfilename);
            return(1);
        }
    } else {
        printf("cannot malloc %d bytes\n", PATH_MAX);
        return(1);
    }

    /* Read the entire hexfile, counting the lines. */
    while ((fgets(line, sizeof(line), hexfile)) != NULL) {
        if (line[0] == ':' && line[7] == '0') {
            switch (line[8] - '0') {
            case IHEX_DATA_RECORD:
            case IHEX_END_OF_FILE_RECORD:
            case IHEX_EXTENDED_LINEAR_ADDRESS_RECORD:
                nlines++;
                break;

            default:
                printf("unhandled record type %c%c at line %d\n",
                                                  line[7], line[8], nlines +1);
                fclose(hexfile);
                free(hexfilename);
                return(1);
            }
        } else {
            printf("not a hex record at line %d: %s", nlines +1, line);
            fclose(hexfile);
            free(hexfilename);
            return (1);
        }
    }

    if (nlines == 0) {
        printf("%s has zero lines\n", hexfilename);
        fclose(hexfile);
        free(hexfilename);
        return (1);
    }

    printf("%s: %d lines\n", hexfilename, nlines);

    fclose(hexfile);
    hexfile = fopen(hexfilename, "r");

    fprintf(portout, "blswitch %s\n", hostname);
    fgets(response, sizeof(response), portin);
    if (strncmp(response, "closed", strlen("closed"))) {
        if (strncmp(response, "open", strlen("open")) == 0) {
            fprintf(stderr, "%s blswitch: the switch is open: ", hostname);
            fprintf(stderr, "it needs to be closed.\n");
        } else {
            int n = strlen(response);
            if (n > 0 && response[n - 1] == '\n')
                response[n-1] = '\0';
            fprintf(stderr, "%s blswitch: expected 'closed', got '%s'\n",
                                       hostname, response);
        }
        fclose(hexfile);
        free(hexfilename);
        return(1);
    }

    fprintf(portout, "reboot %s\n", hostname);
    fgets(response, sizeof(response), portin);
    if (strncmp(response, "rebooting", strlen("rebooting"))) {
        fprintf(stderr, "expected 'rebooting', got '%s'\n", response);
        fclose(hexfile);
        free(hexfilename);
        return(1);
    }

    fprintf(portout, "isp %s\n", hostname);
    /* read bootloader version */
    fgets(response, sizeof(response), portin);
    if (strncmp(response, "TWIBOOT", strlen("TWIBOOT"))) {
        fprintf(stderr, "expected 'TWIBOOT', got '%s'\n", response);
        fclose(hexfile);
        free(hexfilename);
        return(1);
    }
    /* read chip info */
    fgets(response, sizeof(response), portin);

    if ((cin = fgetc(portin)) != '.') {
        fprintf(stderr, "expected '.', got '%c'\n", cin);
        exit(1);
    }

    while (fgets(line, sizeof(line), hexfile) != NULL) {
        progress++;
        fputs(line, portout);
        if ((cin = fgetc(portin)) != '.') {
            /* a dollar sign after the last line indicates success */
            if (progress == nlines && cin == '$')
                fprintf(stdout, "\r%3d%% ", 100);
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

    fgets(response, sizeof(response), portin);
    if (*response != '\n')
        fprintf(portout, "avril: error: %c%s", cin, response);
    fclose(hexfile);
    free(hexfilename);
    return(ret);
}

/* end code */
