/* fs/sfa.h */

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

/* little file system, type 0xfa partition. */

#ifndef _SFA_H_
#define _SFA_H_

#include <time.h>

#define PATH_SEPARATOR        '/'
#define ROOT_INODE_NR           1
#define PARTITION_TABLE_SECTOR  0
#define LFS_PARTITION_TYPE      0xFA /* 250 */

#define MASK_OF(n)              ((n) -1)

#define BLOCK_SIZE_SHIFT        9
#define BLOCK_SIZE              (1 << BLOCK_SIZE_SHIFT) /* 512 */
#define BLOCK_SIZE_MASK         (MASK_OF(BLOCK_SIZE))

#define INODES_PER_BLOCK        (BLOCK_SIZE / sizeof(inode_t))
#define INODES_PER_BLOCK_SHIFT  5
#define INODES_PER_BLOCK_MASK   (MASK_OF(INODES_PER_BLOCK))
#define INODE_SIZE_SHIFT        4
#define ITABLE_SECTOR(n)        ((n) >> INODES_PER_BLOCK_SHIFT)

#define BITS_PER_BYTE_SHIFT     3
#define BITS_PER_BYTE           (1 << BITS_PER_BYTE_SHIFT)
#define BITS_PER_BYTE_MASK      (MASK_OF(BITS_PER_BYTE))

#define BITS_PER_BLOCK_SHIFT    12
#define BITS_PER_BLOCK          (1 << BITS_PER_BLOCK_SHIFT)
#define BITS_PER_BLOCK_MASK     (MASK_OF(BITS_PER_BLOCK))

#define DIRENT_PER_BLOCK        (BLOCK_SIZE / sizeof(dir_struct))
#define DIRENT_PER_BLOCK_SHIFT  5
#define DIRENT_PER_BLOCK_MASK   (MASK_OF(DIRENT_PER_BLOCK))
#define DIRENT_SIZE_SHIFT       4
#define DIRENT_SECTOR(n)        ((n) >> DIRENT_PER_BLOCK_SHIFT)
#define DIRENT_ITEMS(n)         ((n) >> DIRENT_SIZE_SHIFT)

/* derive the byte offset of a dirent index */
#define DIRENT_OFFSET(n)        ((n) << DIRENT_SIZE_SHIFT)

#define ZONE_SHIFT              4
#define ZONE_SIZE               (BLOCK_SIZE << ZONE_SHIFT)
#define ZONE_BYTES_SHIFT        (BLOCK_SIZE_SHIFT + ZONE_SHIFT) /* 13 */
#define SUPER_MAGIC             0xcafe

#define BYTE_SECTOR(x)          ((x) >> BLOCK_SIZE_SHIFT) 
#define BYTE_ZONE(x)            ((x) >> ZONE_BYTES_SHIFT) 
#define ZONE_SECTORS(x)         ((x) << ZONE_SHIFT) 
#define ZONE_BYTES(x)           ((x) << ZONE_BYTES_SHIFT) 

#define NAME_SIZE               14
#define PATH_MAX                255
#define MAX_LINKS               255

/* maximum location using little dimensions: (((257 * 256) +3) * 512) */
#define MAX_FILE_SIZE           33687040L
#define NR_BOOT_SECTORS         1
#define NR_SUPER_SECTORS        1
#define NR_IMAP_SECTORS         1
#define NR_ZMAP_SECTORS         1
#define FIRST_ITABLE_SECTOR     (NR_BOOT_SECTORS + NR_SUPER_SECTORS + \
                                 NR_IMAP_SECTORS + NR_ZMAP_SECTORS)

#define BOOT_SECTOR_NUMBER      0
#define SUPER_SECTOR_NUMBER     (BOOT_SECTOR_NUMBER + NR_BOOT_SECTORS)
#define IMAP_SECTOR_NUMBER      (SUPER_SECTOR_NUMBER + NR_SUPER_SECTORS)
#define ZMAP_SECTOR_NUMBER      (IMAP_SECTOR_NUMBER + NR_IMAP_SECTORS)
#define ITABLE_SECTOR_NUMBER    (ZMAP_SECTOR_NUMBER + NR_ZMAP_SECTORS)
#define NR_ITABLE_SECTORS       (ZONE_SIZE / BLOCK_SIZE - ITABLE_SECTOR_NUMBER) 

#define FIRST_DATA_ZONE         1
#define INVALID_INODE_NR        0

#ifdef SEEK_SET
#undef SEEK_SET
#endif
#define SEEK_SET                0

#ifdef SEEK_CUR
#undef SEEK_CUR
#endif
/* there is no SEEK_CUR in a stateless file server */

#ifdef SEEK_END
#undef SEEK_END
#endif
#define SEEK_END                1

/* [p.444] */
typedef ushort_t         block_nr;
typedef ushort_t         zone_nr;
typedef ushort_t         bit_nr;
typedef uchar_t          mode_t;
typedef uchar_t          links_t; /* signed in MINIX */
typedef ushort_t         inum_t;

#define ZONE_NUM_SIZE    sizeof(zone_nr)
#define INODE_SIZE       sizeof(inode_t)
#define SUPER_SIZE       sizeof(super_t)
#define DIRENT_SIZE      sizeof(dir_struct)

/* inode i_mode bits */
#define I_TYPE           0xF0 /* this field gives inode type */
#define I_REGULAR        0x80 /* regular file, not dir or special */
#define I_BLOCK_SPECIAL  0x60 /* block special file */
#define I_DIRECTORY      0x40 /* file is a directory */
#define I_CHAR_SPECIAL   0x20 /* character special file */
#define ALL_MODES        0x0F /* all bits for trwx */
#define I_STICKY_BIT     0x08 /* set sticky bit */
#define RWX_MODES        0x07 /* mode bits for RWX only */
#define R_BIT            0x04 /* Rwx protection bit */
#define W_BIT            0x02 /* rWx protection bit */
#define X_BIT            0x01 /* rwX protection bit */
#define I_NOT_ALLOC      0x00 /* this inode is free */

typedef struct {
    ushort_t   s_ninodes;
    zone_nr    s_nzones;
    off_t      s_max_size;
    ushort_t   s_magic;
} super_t;

typedef struct {
    mode_t     i_mode;                /* 1 byte */
    links_t    i_nlinks;              /* 1 byte */
    inum_t     i_inum;                /* 2 bytes */
    off_t      i_size;                /* 4 bytes */
    time_t     i_mtime;               /* 4 bytes */
    zone_nr    i_zone;                /* 2 bytes */
    zone_nr    i_nzones;              /* 2 bytes */
} inode_t;

typedef struct {
    inum_t     d_inum;               /* 2 bytes */
    char       d_name[NAME_SIZE];    /* 14 bytes */
} dir_struct;

#endif /* _SFA_H_ */
