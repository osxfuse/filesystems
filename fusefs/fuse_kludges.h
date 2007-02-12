/*
 * Copyright (C) 2006 Google. All Rights Reserved.
 * Amit Singh <singh@>
 */

#ifndef _FUSE_KLUDGES_H_
#define _FUSE_KLUDGES_H_

#include "fuse.h"
#include "fuse_sysctl.h"

#include <sys/mount.h>
#include <sys/types.h>

void vfs_setlocklocal(mount_t mp);
int  fuse_is_unmount_in_progress(mount_t mp);

#endif /* _FUSE_KLUDGES_H_ */
