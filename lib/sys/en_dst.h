/* sys/en_dst.h */

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

/* daylight savings time calculation function for localtime(3)
   for England, Scotland, Wales and Ireland. Possibly others.
   Derived from <util/eu_dst.h> and ideas from Stack Exchange.

   usage:

      #include "sys/en_dst.h"
      ...
      set_zone(0);
      set_dst(en_dst);

*/

#ifndef EN_DST_H
#define EN_DST_H

#include <time.h>
#include <inttypes.h>

/* occurs at 1am GMT Sunday in the 13th and 44th weeks */

#define DST_BEGIN_WEEK        13
#define DST_END_WEEK          44
#define DST_HOUR              1
#define WEEK_STARTS_ON_SUNDAY 0

int en_dst(const time_t * timer, __attribute__ ((unused)) int32_t * z)
{
    struct tm time;
    uchar_t dst;

    /* generate the broken down time */
    gmtime_r(timer, &time);

    /* Check if we're in daylight savings time. */
    uchar_t yweek = week_of_year(&time, WEEK_STARTS_ON_SUNDAY);

    if (yweek < DST_BEGIN_WEEK || yweek > DST_END_WEEK) {
        dst = FALSE;
    } else if (yweek > DST_BEGIN_WEEK && yweek < DST_END_WEEK) {
        dst = TRUE;
    } else if (yweek == DST_BEGIN_WEEK) {
        dst = (time.tm_wday == 0 && time.tm_hour < DST_HOUR) ? FALSE : TRUE;
    } else { /* yweek == DST_END_WEEK */
        dst = (time.tm_wday == 0 && time.tm_hour < DST_HOUR) ? TRUE : FALSE;
    }

    return dst ? ONE_HOUR : 0;
}

#endif /* EN_DST_H */
