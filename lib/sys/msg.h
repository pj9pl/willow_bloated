/* sys/msg.h */

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

#ifndef _MSG_H_
#define _MSG_H_

#include "host.h"
#include "sys/errno.h"

typedef enum {
    NOT_EMPTY = 1,    /* producer -> consumer send_m3() */ 
    READ,             /* client -> server    send_m2() */
    WRITE,            /* client -> server    send_m4() */
    REPLY_DATA,       /* client <- server    send_m4() */
    REPLY_RESULT,     /* client <- server    send_m2() */
    START,            /* client -> server    send_m1() */
    STOP,             /* client -> server    send_m1() */
    RESET,            /* client -> server    send_m1() */
    SYNC,             /* client -> server    send_m1() */
    SET_ALARM,        /* client -> clk       send_m3() */
    CANCEL,           /* client -> server    send_m3() */
    ALARM,            /* client <- clk       send_m5() */
    SET_IOCTL,        /* client -> server    send_m4() */
    GET_IOCTL,        /* client -> server    send_m2() */
    JOB,              /* client -> server    send_m3() */
    INIT,             /* client -> server    send_m1() */
    MASTER_COMPLETE,  /* server <- interrupt send_m2() */
    SLAVE_COMPLETE,   /* server <- interrupt send_m2() */
    MEDIA_CHANGE,     /* server <- hardware  send_m1() */
    RDY_REQUEST,      /* client -> server    send_m1() */
    ADC_RDY,          /* client <- server    send_m2() */
    INIT_OK,          /* server <- server    send_m1() */
    NOT_BUSY,         /* self   <- interrupt send_m1() */
    REFRESH,          /*        ->           send_m1() */ 
    UPDATE,           /*        ->           send_m1() */ 
    BUTTON_CHANGE,    /* server <- hardware  send_m2() */
    RTC_INTR,         /* server <- hardware  send_m2() */
    PERIODIC_ALARM,   /* client <- clk       send_m2() */
    TERM,             /* client -> server    send_m1() */
    REPLY_INFO,       /* client <- server    send_m5() */
    EOC,              /* self   <- interrupt send_m1() */
    READ_BUTTON,      /* client -> server    send_m2() */
    NR_OPCODES
} __attribute__((packed)) MsgNumber;

typedef struct {
    void  *m3p1;
    ushort_t m3s1;
} mess_3;

typedef struct {
    ulong_t m5l1;
} mess_5;

typedef struct {
    ProcNumber sender;
    ProcNumber receiver;
    MsgNumber opcode;
    uchar_t mtype;
    union {
        mess_3 m_m3;
        mess_5 m_m5;
    } m_u;
} message;
 
typedef uchar_t (*MsgProc) (message *msg);

/* [Minix p.445] */

#define m3_m3p1 m_u.m_m3.m3p1
#define m3_m3s1 m_u.m_m3.m3s1
#define m5_m5l1 m_u.m_m5.m5l1

#define VPTR   m3_m3p1
#define INFO   m3_m3p1
#define LCOUNT m5_m5l1

#define RESULT mtype
#define IOCTL  mtype

/* Ensure the correct arguments are used for the particular opcode. */
#define send_NOT_EMPTY(d,f)         send_m3(SELF,(d),NOT_EMPTY,(f))
#define send_READ(d,m)              send_m2(SELF,(d),READ,(m))
#define send_WRITE(d,m,l)           send_m5(SELF,(d),WRITE,(m),(l))
#define send_REPLY_DATA(d,m,l)      send_m5(SELF,(d),REPLY_DATA,(m),(l))
#define send_REPLY_RESULT(d,m)      send_m2(SELF,(d),REPLY_RESULT,(m))
#define send_START(d)               send_m1(SELF,(d),START)
#define send_STOP(d)                send_m1(SELF,(d),STOP)
#define send_RESET(d)               send_m1(SELF,(d),RESET)
#define send_SYNC(d)                send_m1(SELF,(d),SYNC)
#define send_SET_ALARM(d,p)         send_m3(SELF,(d),SET_ALARM,(p))
#define send_CANCEL(d,p)            send_m3(SELF,(d),CANCEL,(p))
#define send_ALARM(d,m,p)           send_m4(SELF,(d),ALARM,(m),(p))
#define send_SET_IOCTL(d,m,l)       send_m5(SELF,(d),SET_IOCTL,(m),(l))
#define send_GET_IOCTL(d,m)         send_m2(SELF,(d),GET_IOCTL,(m))
#define send_JOB(d,p)               send_m3(SELF,(d),JOB,(p))
#define send_INIT(d)                send_m1(SELF,(d),INIT)
#define send_MASTER_COMPLETE(m)     send_m2(SELF,SELF,MASTER_COMPLETE,(m))
#define send_SLAVE_COMPLETE(m)      send_m2(SELF,SELF,SLAVE_COMPLETE,(m))
#define send_MEDIA_CHANGE(d)        send_m1(SELF,(d),MEDIA_CHANGE)
#define send_RDY_REQUEST(d)         send_m1(SELF,(d),RDY_REQUEST)
#define send_ADC_RDY(d,m)           send_m2(SELF,(d),ADC_RDY,(m))
#define send_INIT_OK(d)             send_m1(SELF,(d),INIT_OK)
#define send_NOT_BUSY(d)            send_m1(SELF,(d),NOT_BUSY)
#define send_UPDATE(d)              send_m1(SELF,(d),UPDATE)
#define send_BUTTON_CHANGE(d,m)     send_m2(SELF,(d),BUTTON_CHANGE,(m))
#define send_RTC_INTR(d)            send_m1(SELF,(d),RTC_INTR)
#define send_PERIODIC_ALARM(d,m)    send_m2(SELF,(d),PERIODIC_ALARM,(m))
#define send_TERM(d)                send_m1(SELF,(d),TERM)
#define send_REPLY_INFO(d,m,p)      send_m4(SELF,(d),REPLY_INFO,(m),(p))
#define send_EOC(d)                 send_m1(SELF,(d),EOC)
#define send_READ_BUTTON(d,m)       send_m2(SELF,(d),READ_BUTTON,(m))

PUBLIC void config_msg(void);
PUBLIC void extract_msg(message *m_ptr);

PUBLIC void send_m1(ProcNumber sender, ProcNumber receiver, MsgNumber opcode);
PUBLIC void send_m2(ProcNumber sender, ProcNumber receiver, MsgNumber opcode,
                                                                uchar_t mtype);
PUBLIC void send_m3(ProcNumber sender, ProcNumber receiver, MsgNumber opcode,
                                                                    void *ptr);
PUBLIC void send_m4(ProcNumber sender, ProcNumber receiver, MsgNumber opcode,
                                                     uchar_t mtype, void *ptr);
PUBLIC void send_m5(ProcNumber sender, ProcNumber receiver, MsgNumber opcode,
                                                uchar_t mtype, ulong_t lcount);

PUBLIC uchar_t msg_depth(void);
PUBLIC ulong_t msg_count(void);
PUBLIC ulong_t msg_lost(void);
PUBLIC uchar_t msg_slots_available(void);

#endif /* _MSG_H_ */
