/* fs/mbr.h */

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

/* master boot record */

#ifndef _MBR_H_
#define _MBR_H_

typedef struct {
    uchar_t    boot;
    uchar_t    beginHead;
    unsigned   beginSector       : 6;
    unsigned   beginCylinderHigh : 2;
    uchar_t    beginCylinderLow;
    uchar_t    type;
    uchar_t    endHead;
    unsigned   endSector       : 6;
    unsigned   endCylinderHigh : 2;
    uchar_t    endCylinderLow;
    ulong_t    firstSector;
    ulong_t    totalSectors;
} part_t;

typedef struct {
    uchar_t    codearea[440];
    ulong_t    diskSignature;
    ushort_t   usuallyZero;
    part_t     part[4];
    uchar_t    mbrSig0;
    uchar_t    mbrSig1;
} mbr_t;

#endif /* _MBR_H_ */

