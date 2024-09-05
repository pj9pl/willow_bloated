/* hal/rlcat.c */

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

/* to build: cc rlcat.c -lreadline -o rlcat */

/* adapted from readline info examples */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <readline/readline.h>
#include <readline/history.h>

/* A static variable for holding the line. */
static char *line_read = NULL;


#define STREQ(a, b) ((a)[0] == (b)[0] && strcmp((a), (b)) == 0)
#define STREQN(a, b, n) ((n) == 0) ? (1) \
                          : ((a)[0] == (b)[0] && strncmp((a), (b), (n)) == 0))

extern int history_offset;

char * rl_gets (void);
int hist_erasedups (void);

extern char *optarg;
extern int optind, opterr, optopt;

int main(int argc, char **argv)
{
char *cp;
FILE *sendf = NULL;
FILE *teefile = NULL;
FILE *historyfile = NULL;
char *fn;
int r;
char *portname = NULL;
int opt;

    fn = ".history";
    historyfile = fopen(fn, "a+");
    if (historyfile == NULL) {
        fprintf(stderr, "rlcat: cannot open %s\n", fn);
        exit(1);
    }
    fclose(historyfile);

    if ((r = read_history(fn)) != 0) {
        fprintf (stderr, "rlcat: read_history: %s: %s\n", fn, strerror(r));
        exit (1);
    }

    while ((opt = getopt(argc, argv, "a:p:")) != -1) {
        switch (opt) {
        case 'a':
            teefile = fopen(optarg, "a+");
            break;

        case 'p':
            portname = optarg;
            break;

        default: /* '?' */
            fprintf(stderr, "Usage: %s [-a append_file] [-p port]\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    if (teefile == NULL) {
        fprintf(stderr, "teefile in NULL\n");
    }

    if (portname == NULL) {
        if (getenv("port") != NULL) {
            portname = strdup(getenv("port"));
        } else {
            fprintf(stderr,
                     "-p port must be set, or set $port in the environment\n");
            exit(1);
        }
    }

    sendf = fopen(portname, "w");

    if (sendf == NULL) {
        fprintf(stderr, "failed to open %s\n", portname);
        exit(1);
    }

    if (sendf != NULL) {
        while ((cp = rl_gets()) != NULL) {
            if (strcmp(cp, "quit") == 0) {
                break;
            } else if (strcmp(cp, "nodups") == 0) {
                hist_erasedups();
                continue;
            }
            if (teefile) {
                fprintf(teefile, "$ %s\n", cp);
                fflush(teefile);
            }
            int ret = fprintf(sendf, "%s\n", cp);
            if (ret == EOF || ret == 0) {
                break;
            }
        }

        hist_erasedups();
        if ((r = write_history(fn)) != 0) {
            fprintf(stderr, "rlcat: write_history: %s: %s\n", fn, strerror(r));
            exit(1);
        }

        fclose(sendf);

        if (teefile)
            fclose(teefile);
    }
    return 0;
}

/* Read a string, and return a pointer to it.  Returns NULL on EOF. */
char *rl_gets(void)
{
    /* If the buffer has already been allocated,
     * return the memory to the free pool.
     */
    if (line_read) {
        free(line_read);
        line_read = (char *)NULL;
    }
    /* Get a line from the user. */
    line_read = readline("");

    /* If the line has any text in it, save it on the history. */
    if (line_read && *line_read)
        add_history(line_read);

    return(line_read);
}

int hist_erasedups(void)
{
int r, n;
HIST_ENTRY *h, *temp;

    using_history();
    while ((h = previous_history()) != NULL) {
        r = where_history();
        for (n = 0; n < r; n++) {
            temp = history_get(n + history_base);
            if (STREQ (h->line, temp->line)) {
                remove_history(n);
                r--;                      /* have to get one fewer now */
                n--;                      /* compensate for above increment */
                history_offset--;         /* moving backwards in history list */
            }
        }
    }
    using_history();

    return(r);
}

/* end code */
