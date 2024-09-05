/* hal/printfont.c */

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

/* Print the array of shorts an an array of unsigned hex bytes. */

#include <stdio.h>

const short bigfont[] = {
    0x1FF8, 0x3FFC, 0x7E0E, 0x6706, 0x6386,
    0x61C6, 0x60E6, 0x707E, 0x3FFC, 0x1FF8, /* 0 */
    0x0000, 0x0000, 0x6018, 0x601C, 0x7FFE,
    0x7FFE, 0x6000, 0x6000, 0x0000, 0x0000, /* 1 */
    0x6018, 0x701C, 0x780E, 0x7C06, 0x6E06,
    0x6706, 0x6386, 0x61CE, 0x60FC, 0x6078, /* 2 */
    0x1806, 0x3806, 0x7006, 0x6006, 0x6066,
    0x60F6, 0x61FE, 0x739E, 0x3F0E, 0x1E06, /* 3 */
    0x0780, 0x07C0, 0x06E0, 0x0670, 0x0638,
    0x061C, 0x7FFE, 0x7FFE, 0x0600, 0x0600, /* 4 */
    0x187E, 0x387E, 0x7066, 0x6066, 0x6066,
    0x6066, 0x6066, 0x70E6, 0x3FC6, 0x1F86, /* 5 */
    0x1FE0, 0x3FF0, 0x73B8, 0x619C, 0x618E,
    0x6186, 0x6186, 0x7386, 0x3F00, 0x1E00, /* 6 */
    0x001E, 0x001E, 0x0006, 0x0006, 0x7E06,
    0x7F06, 0x0386, 0x01C6, 0x00FE, 0x007E, /* 7 */
    0x1E78, 0x3E7C, 0x73CE, 0x6186, 0x6186,
    0x6186, 0x6186, 0x73CE, 0x3E7C, 0x1E78, /* 8 */
    0x0078, 0x00FC, 0x61CE, 0x6186, 0X6186,
    0x7186, 0x3986, 0x1D8E, 0x0FFC, 0x07F8, /* 9 */
    0x0000, 0x0000, 0x0000, 0x3000, 0x7800,
    0x7800, 0x3000, 0x0000, 0x0000, 0x0000, /* . */
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, /*   */
    0x0180, 0x0180, 0x0180, 0x0180, 0x0180,
    0x0180, 0x0180, 0x0180, 0x0180, 0x0180 /* - */
};


static void put_nibble(unsigned char v);
static void puthex(unsigned char ch);

int main(int argc, char **argv)
{
    char *cp = (char *)bigfont;
    printf("const uchar_t __flash bigfont[] = {\n    ");
    for (int i = 0; i < sizeof(bigfont); i++) {
        printf("0x");
        puthex(*cp++);
        printf(",");
        if ((i+1) % 10) {
            putchar(' ');
        } else {
            putchar('\n');
            if (i + 1 < sizeof(bigfont))
                printf("    ");
        }
    }
    printf("};\n");
}

static void put_nibble(unsigned char v)
{
    putchar((v < 10 ? '0' : '7') + v);
}

static void puthex(unsigned char ch)
{
#define HIGH_NIBBLE(c)         ((c) >> 4 & 0x0f)
#define LOW_NIBBLE(c)          ((c) & 0x0f)

    put_nibble(HIGH_NIBBLE(ch));
    put_nibble(LOW_NIBBLE(ch));
}

/* end code */
