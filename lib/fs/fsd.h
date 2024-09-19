/* fs/fsd.h */

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

#ifndef _FSD_H_
#define _FSD_H_

#ifndef _MAIN_

/* FSD REQUEST operations */
#define  OP_MKNOD   1
#define  OP_IFETCH  2
#define  OP_IWRITE  3
#define  OP_READ    4
#define  OP_MKFS    5
#define  OP_SECTOR  6
#define  OP_BUFFER_ADDRESS 7
#define  OP_LINK    8
#define  OP_UNLINK  9
#define  OP_PATH    10
#define  OP_INDIR   11

typedef struct {
    char *src;
    ushort_t len;
    ushort_t nzones;
    uchar_t mode;
    inum_t p_inum;
} mknod_request;

typedef struct {
    inum_t inum;
    inode_t *ip;
} ifetch_request;

typedef struct {
    inum_t inum;
    inode_t *ip;
} iwrite_request;

typedef struct {
    inum_t inum;
    void *dst;
    unsigned int use_cache : 1;
    unsigned int whence : 1;
    off_t offset;
    ushort_t len;
} readf_request;

typedef struct {
    ushort_t num; /* sector number within the partition */
} sectf_request;

typedef struct {
    char *src;
    ushort_t len;
    inum_t base_inum;
    inum_t dir_inum;
} link_request;

typedef struct {
    char *src;
    ushort_t len;
    inum_t dir_inum;
} unlink_request;

typedef struct {
    char *src;
    ushort_t len;
    inum_t cwd;       /* cwd inode number */
    inode_t *ip;
} path_request;

typedef struct {
    char *bp;         /* client memory address to receive the basename */
    inum_t base_inum; /* inode number of basename */
    inum_t dir_inum;  /* inode number of parent directory */
} indir_request;

/* replies */

typedef struct {
    off_t fpos;       /* the next unread location */
    ushort_t nbytes;  /* number of bytes delivered */
} readf_reply;

typedef struct {
    uchar_t *bufp;    /* address of the sd_admin sector buffer */
} bufaddr_reply;

typedef struct {
    inum_t base_inum; /* basename inode number */
    inum_t dir_inum;  /* dirname inode number */
} path_reply;

typedef struct {
    ushort_t d_idx;
} indir_reply;

typedef struct {
    ProcNumber taskid;
    jobref_t jobref;
    hostid_t sender_addr;
    uchar_t op;
    union {
        mknod_request mknod;
        ifetch_request ifetch;
        iwrite_request iwrite;
        readf_request readf;
        sectf_request sectf;
        link_request link;
        unlink_request unlink;
        path_request path;
        indir_request indir;
    } p;
} fsd_request;

typedef struct {
    ProcNumber taskid;
    jobref_t jobref;
    hostid_t sender_addr;
    uchar_t result;
    union {
        readf_reply readf;
        bufaddr_reply bufaddr;
        path_reply path;
        indir_reply indir;
    } p;
} fsd_reply;

typedef union {
    fsd_request request;
    fsd_reply reply;
} fsd_msg;                 /* 17 bytes */

#else /* _MAIN_ */

PUBLIC uchar_t receive_fsd(message *m_ptr);

#endif /* _MAIN_ */

#endif /* _FSD_H_ */
