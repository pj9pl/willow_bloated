/* sys/clk.h */

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

#ifndef _CLK_H_
#define _CLK_H_

#ifndef _MAIN_

/* A clk_info structure is declared by each task that wants an alarm call.
 * The uval serves a dual purpose in that the client first initializes it to a
 * millisecond value and then the CLK overwrites this with the corresponding
 * number of ticks relative to the current clk.ticks.
 */

typedef struct _clk_info {
    struct _clk_info *nextp;
    ProcNumber replyTo;
    ulong_t uval;             /* millisecond value */
} clk_info;

/* convenience functions */

PUBLIC void send_CLK_SET_ALARM (
    ProcNumber sender,
    clk_info *cp,
    ulong_t delay
);

PUBLIC void send_CLK_CANCEL (
    ProcNumber sender,
    clk_info *cp
);

/* convenience macros */

#define sae_CLK_SET_ALARM(a,b)      send_CLK_SET_ALARM(SELF, &(a),(b))
#define sae_CLK_CANCEL(a)           send_CLK_CANCEL(SELF, &(a))

#else /* _MAIN_ */

PUBLIC uchar_t receive_clk(message *m_ptr);

#endif /* _MAIN_ */

#endif /* _CLK_H_ */
