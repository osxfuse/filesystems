/*
 * Copyright (C) 2006 Google. All Rights Reserved.
 * Amit Singh <singh@>
 */

#ifndef _FUSE_IOCTL_H_
#define _FUSE_IOCTL_H_

#include <sys/ioctl.h>

/* FUSEDEVIOCxxx */
#define FUSEDEVIOCISHANDSHAKECOMPLETE _IOR('F', 1, u_int32_t)
#define FUSEDEVIOCDAEMONISDYING       _IOW('F', 2, u_int32_t)

#endif /* _FUSE_IOCTL_H_ */
