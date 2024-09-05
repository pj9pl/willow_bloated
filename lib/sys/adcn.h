/* sys/adcn.h */

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

#ifndef _ADCN_H_
#define _ADCN_H_

#ifndef _MAIN_

#define INTERNAL_REF     (_BV(REFS1) | _BV(REFS0))
#define EXTERNAL_REF     0x00
#define AVCC_REF         _BV(REFS0)

#define CHANNEL_0        0  /* pin C0 */
#define CHANNEL_1        1  /* pin C1 */
#define CHANNEL_2        2  /* pin C2 */
#define CHANNEL_3        3  /* pin C3 */
#define CHANNEL_4        4  /* pin C4 - used by TWI_SDA */
#define CHANNEL_5        5  /* pin C5 - used by TWI_SCL */
#define CHANNEL_6        6  /* pin C6 - absent from the 28 pin DIP */
#define CHANNEL_7        7  /* pin C7 - absent from the 28 pin DIP */
#define CHANNEL_8        8  /* Temperature sensor */
/* 9 .. 13 reserved */
#define CHANNEL_14      14  /* 1.1v (V bandgap) */
#define CHANNEL_15      15  /* GND */ 

typedef struct _adcn_info {
    struct _adcn_info *nextp;   
    ProcNumber replyTo;
    uchar_t admux;              /* Multiplexer Selection Register [p.257] */
    ushort_t result;            /* 10-bit conversion value */
} adcn_info;

#else /* _MAIN_ */

PUBLIC uchar_t receive_adcn(message *m_ptr);

#endif /* _MAIN_ */

#endif /* _ADCN_H_ */
