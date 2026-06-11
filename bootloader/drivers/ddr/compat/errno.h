/* SPDX-License-Identifier: BSD-3-Clause */
/* Compat shim: the errno values the carried TF-A DDR code returns. */

#ifndef ERRNO_H
#define ERRNO_H

#define EPERM     1
#define ENOENT    2
#define EIO       5
#define ENOMEM    12
#define EFAULT    14
#define EBUSY     16
#define ENODEV    19
#define EINVAL    22
#define ERANGE    34
#define ENOTSUP   95
#define ETIMEDOUT 110

#endif /* ERRNO_H */
