/* alba/alba.h */

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

#ifndef _ALBA_H_
#define _ALBA_H_

#ifndef _MAIN_

typedef enum {
    READ_MODE = 1,
    WRITE_MODE,
    RESET_MODE
} __attribute__ ((packed)) alba_mode;

typedef struct _alba_info {
    struct _alba_info *nextp;
    ProcNumber replyTo;
    alba_mode mode;
    unsigned data_status : 1;
    uchar_t regno;
    union {
        ulong_t val;
        uchar_t ch[4];
        comms_reg comms;
        status_reg status;
        adc_control_reg adc_control;
        data_reg data;
        io_control_1_reg io_control_1;
        io_control_2_reg io_control_2;
        id_reg id;
        error_reg error;
        error_en_reg error_en;
        mclk_count_reg mclk_count;
        channel_reg channel;
        config_reg config;
        filter_reg filter;
        offset_reg offset;
        gain_reg gain;
    } rb;
} alba_info;

#else /* _MAIN_ */

PUBLIC void config_alba(void);
PUBLIC uchar_t receive_alba(message *m_ptr);

#endif /* _MAIN_ */

#endif /* _ALBA_H_ */
