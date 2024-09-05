/* hal/ftime.c */

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

/* compare the RV-3028-C7 interrupt with the linux ntp time
 *
 * usage: ftime
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/timeb.h>
#include <sys/time.h>

#include "sys/defs.h"
#include "isp/ihex.h"

#define BUF_LEN 80
#define PATH_MAX 32

FILE *portin;
FILE *portout;

char response[BUF_LEN];

int main(void)
{
    char *portname = NULL;
    int n;
    time_t ztime;
    int zfrac;
    int zmilli;
    struct timeval tv;

    if ((portname = getenv("port")) != NULL) {
        portname = strdup(getenv("port"));
    } else {
        fprintf(stderr,"$port must be set in the environment\n");
        exit(1);
    }

    portout = fopen(portname, "w");
    portin = fopen(portname, "r");

    if (portin == NULL || portout == NULL) {
        fprintf(stderr,"failed to open port: %s\n", portname);
        exit(1);
    }

    /* Check the input mode.
     * Send an 'e' and test the resonse.
     * If it's in INP, switch to CLI
     */
    fputs("e\n", portout);
    fgets(response, sizeof(response), portin);
    if (!strncmp(response, "# ", strlen("# "))) {
        /* talking to the INP */
        fputs("a\n", portout); 
        fgets(response, sizeof(response), portin);
        if (strncmp(response, "in cli", strlen("in cli"))) {
            fprintf(stderr,"failed to enter CLI\n");
            exit(1);
        }
    } else if (strncmp(response, "e ", strlen("e "))) {
        fprintf(stderr,"not talking to the CLI\n");
        exit(1);
    }

    while (fgets(response, sizeof(response), portin) != NULL) {
        gettimeofday(&tv, NULL);
        int msec = (tv.tv_usec + 500) / 1000;
        if (msec >= 1000)
            msec = 999;
        if ((n = sscanf(response, "ux,%ld", &ztime)) == 1) {
            time_t sdiff = ztime - tv.tv_sec;
            fprintf(stderr, "%ld, %06ld, %03d, ", tv.tv_sec, tv.tv_usec, msec);
            fprintf(stderr,"rtc: %ld, 000, ", ztime);
            int frac = msec;
            fputc('(', stderr);
            if (tv.tv_sec < ztime) {
                sdiff = ztime - tv.tv_sec;
                frac = 1000 - msec;
                if (frac > 0)
                    sdiff--;
                fputc('+', stderr);
            } else {
                sdiff = tv.tv_sec - ztime;
                frac = msec;
                fputc('-', stderr);
            }
            if (msec >= 1000)
                msec = 999;
            fprintf(stderr, "%ld.%03d)\n", sdiff, frac);
        } else if ((n = sscanf(response, "utc,%ld,%d", &ztime, &zfrac)) == 2) {
            zmilli = (zfrac * 1000L) >> 8;
            time_t sdiff;
            fprintf(stderr, "%ld, %06ld, %03d, ", tv.tv_sec, tv.tv_usec, msec);
            fprintf(stderr,"utc: %ld, %03d, %03d", ztime, zfrac, zmilli);
            int frac;
            fputc('(', stderr);
            if (tv.tv_sec < ztime) {
                sdiff = ztime - tv.tv_sec;
                frac = (1000 - msec) + zmilli;
                if (frac < 1000) {
                    sdiff--;
                } else {
                    frac -= 1000;
                }
                fputc('+', stderr);
            } else if (tv.tv_sec == ztime) {
                sdiff = 0;
                if (msec < zmilli) {
                    frac = zmilli - msec;
                    fputc('+', stderr);
                } else {
                    frac = msec - zmilli;
                    fputc('-', stderr);
                }
            } else /* (tv.tv_sec > ztime) */ {
                sdiff = tv.tv_sec - ztime;
                frac = (1000 - zmilli) + msec;
                if (frac < 1000) {
                    sdiff--;
                } else {
                    frac -= 1000;
                }
                fputc('-', stderr);
            }
            if (msec >= 1000)
                msec = 999;
            fprintf(stderr, "%ld.%03d)\n", sdiff, frac);
        } else {
            fputs(response, stderr);
        }
    }

    fclose(portin);
    fclose(portout);
    exit(0);
}

/* end code */
