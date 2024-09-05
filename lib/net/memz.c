/* net/memz.c */

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

/* A network secretary that provides read access to the SRAM.
 *
 * The incoming data is a MEMZ_REQUEST which contains two unsigned 16-bit
 * integers which specify a SRAM address and a byte count.
 * The outgoing data is the bytes from that location, sent back in ST mode.
 */

#include "sys/defs.h"
#include "sys/msg.h"
#include "net/twi.h"
#include "net/memz.h"

/* I am .. */
#define SELF MEMZ
#define this memz

typedef enum {
    IDLE = 0,
    ENSLAVED
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    memz_request sm; /* service message */
    union {
        twi_info twi;
    } info;
} memz_t;

/* I have .. */
static memz_t this;

/* I can .. */
PRIVATE void set_address(twi_info *tp);
PRIVATE void get_request(void);

PUBLIC uchar_t receive_memz(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case REPLY_INFO:
        get_request();
        break;

    case INIT:
        {
            uchar_t result = EBUSY;
            if (this.state == IDLE) {
                get_request();
                result = EOK;
            }
            send_REPLY_RESULT(m_ptr->sender, result);
        }
        break;

    default:
        return ENOMSG;
    }
    return EOK;
}

 /* -----------------------------------------------------
              Slave Transmitter -[p.236]
    ----------------------------------------------------- */

 /* =====================================================================
  * The remote master has written an address and count value to the
  * slave's receive buffer and has issued a restart into ST mode.
  *
  * The slave must set the transmit pointer to the address and the
  * transmit count to the count value.
  * ===================================================================== */

PRIVATE void set_address(twi_info *ip)
{
    ip->tptr = (uchar_t *) this.sm.src;
    ip->tcnt = this.sm.len;
}

PRIVATE void get_request(void)
{
    this.state = ENSLAVED;
    this.sm.taskid = ANY;
    sae2_TWI_SRST(this.info.twi, MEMZ_REQUEST, this.sm, (Callback) set_address);
}

/* end code */
