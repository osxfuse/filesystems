/*
 * Copyright (C) 2006 Google. All Rights Reserved.
 * Amit Singh <singh@>
 */

#ifndef _FUSE_VERSION_H_
#define _FUSE_VERSION_H_

#define FUSE_MACOSX_VERSION "0.1.0b005"

#define FUSE_KPI_GEQ(M, m) \
    (FUSE_KERNEL_VERSION > (M) || \
    (FUSE_KERNEL_VERSION == (M) && FUSE_KERNEL_MINOR_VERSION >= (m)))

#endif /* _FUSE_VERSION_H_ */
