/* sys/tty.h */

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

#ifndef _TTY_H_
#define _TTY_H_

#ifndef _MAIN_

#include <avr/pgmspace.h>

/* Raw mode outputs each character as soon as possible.
 * Cooked mode accumulates characters until either
 * a newline is written or the buffer gets too full.
 */
#define OMODE_RAW 1
#define OMODE_COOKED 0

PUBLIC void tty_putc(char ch);
PUBLIC void tty_flush(void);
PUBLIC void tty_puts(char *s);
PUBLIC void tty_puts_P(PGM_P str);
PUBLIC void tty_puthex(uchar_t ch);
PUBLIC void tty_printl(long n);
PUBLIC uchar_t tty_can_xmt (void);

#else /* _MAIN_ */

PUBLIC uchar_t receive_tty(message *m_ptr);

#endif /* _MAIN_ */

#endif /* _TTY_H_ */
