/* sys/errno.h */

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

/* unsigned integers */

#ifndef _ERRNO_H_
#define _ERRNO_H_

#define EOK       0       /* Success */
#define EPERM     1       /* Operation not permitted */
#define ENOENT    2       /* No such file or directory */
#define ESRCH     3       /* No such process */
#define EINTR     4       /* Interrupted system call */
#define EIO       5       /* I/O error */
#define ENXIO     6       /* No such device or address */
#define E2BIG     7       /* Argument list too long */
#define ENOEXEC   8       /* Exec format error */
#define EBADF     9       /* Bad file number */
#define ECHILD   10       /* No child processes */
#define EAGAIN   11       /* Try again */
#define ENOMEM   12       /* Out of memory */
#define EACCES   13       /* Permission denied */
#define EFAULT   14       /* Bad address */
#define ENOTBLK  15       /* Block device required */
#define EBUSY    16       /* Device or resource busy */
#define EEXIST   17       /* File exists */
#define EXDEV    18       /* Cross-device link */
#define ENODEV   19       /* No such device */
#define ENOTDIR  20       /* Not a directory */
#define EISDIR   21       /* Is a directory */
#define EINVAL   22       /* Invalid argument */
#define ENFILE   23       /* File table overflow */
#define EMFILE   24       /* Too many open files */
#define ENOTTY   25       /* Not a typewriter */
#define ETXTBSY  26       /* Text file busy */
#define EFBIG    27       /* File too large */
#define ENOSPC   28       /* No space left on device */
#define ESPIPE   29       /* Illegal seek */
#define EROFS    30       /* Read-only file system */
#define EMLINK   31       /* Too many links */
#define EPIPE    32       /* Broken pipe */
#define EDOM     33       /* Math argument out of domain of func */
#define ERANGE   34       /* Math result not representable */
#define EDEADLK  35       /* Resource deadlock would occur */
#define ENAMETOOLONG 36   /* File name too long */
#define ENOLCK   37       /* No record locks available */
#define ENOSYS   38       /* Invalid system call number */
#define ENOTEMPTY 39      /* Directory not empty */

#define ELOOP    40       /* Too many symbolic links encountered */
#define EWOULDBLOCK 41    /* Operation would block -> EAGAIN */
#define ENOMSG   42       /* No message of desired type */
#define EIDRM    43       /* Identifier removed */
#define ECHRNG   44       /* Channel number out of range */
#define EL2NSYNC 45       /* Level 2 not synchronized */
#define EL3HLT   46       /* Level 3 halted */
#define EL3RST   47       /* Level 3 reset */
#define ELNRNG   48       /* Link number out of range */
#define EUNATCH  49       /* Protocol driver not attached */

#define ENOCSI   50       /* No CSI structure available */
#define EL2HLT   51       /* Level 2 halted */
#define EBADE    52       /* Invalid exchange */
#define EBADR    53       /* Invalid request descriptor */
#define EXFULL   54       /* Exchange full */
#define ENOANO   55       /* No anode */
#define EBADRQC  56       /* Invalid request code */
#define EBADSLT  57       /* Invalid slot */

/* from /usr/include/asm-generic/errno.h */
#define ENETDOWN        100     /* Network is down */
#define ECONNABORTED    103     /* Software caused connection abort */
#define ECONNREFUSED    111     /* Connection refused */
#define EHOSTDOWN       112     /* Host is down */
#endif /* _ERRNO_H_ */
