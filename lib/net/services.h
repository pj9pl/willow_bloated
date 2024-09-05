/* net/services.h */

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

#ifndef _SERVICES_H_
#define _SERVICES_H_

/* valid first-byte command values (Service numbers) */
#define VOLTAGE_NOTIFY       138
#define TEMPERATURE_NOTIFY   139
#define PRESSURE_NOTIFY      140
#define DATE_NOTIFY          141
#define BATTERY_NOTIFY       142
#define UTC_REQUEST          143  /* combined transaction */
#define MEMZ_REQUEST         144  /* combined transaction */
/* not used                  145 */
#define HC05_REQUEST         146
#define HC05_REPLY           147
#define FSD_REQUEST          148
#define FSD_REPLY            149
/* not used                  150 */
#define BAROMETER_NOTIFY     151
#define SETUPD_REQUEST       152
#define SETUPD_REPLY         153
/* not used                  154 */
/* not used                  155 */
#define VITZ_NOTIFY          156
/* not used                  157 */
/* not used                  158 */
/* not used                  159 */
#define SYSCON_REQUEST       160
#define SYSCON_REPLY         161
#define OSETUP_REQUEST       162
#define OSETUP_REPLY         163
#define RWR_REQUEST          164
#define RWR_REPLY            165
#define MEMP_REQUEST         166
#define MEMP_REPLY           167
#define ISTREAM_REQUEST      168
#define ISTREAM_REPLY        169
#define OSTREAM_REQUEST      170
#define OSTREAM_REPLY        171
/* not used                  172 */
/* not used                  173 */
#define FSU_REQUEST          174
#define FSU_REPLY            175
#define KEY_REQUEST          176
#define KEY_REPLY            177

#endif /* _SERVICES_H_ */
