/*
 * Copyright (C) 2006 Google. All Rights Reserved.
 * Amit Singh <singh@>
 */

#ifndef _FUSE_KLUDGES_H_
#define _FUSE_KLUDGES_H_

#include "fuse.h"
#include "fuse_sysctl.h"

#include <sys/cdefs.h>
#include <sys/mount.h>
#include <sys/types.h>

/*
 * For user-space code, this comes from <AvailabilityMacros.h>
 */
#ifndef MAC_OS_X_VERSION_10_5
#define MAC_OS_X_VERSION_10_5 1050
#endif

#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= MAC_OS_X_VERSION_10_5

/* Leopard and above. */

#define FUSE_KL_vfs_setlocklocal(mp) vfs_setlocklocal((mp))

#else

/* Tiger. Well, Tiger and below, really, but we only work on Tiger. */

void  FUSE_KL_vfs_setlocklocal(mount_t mp);
int   FUSE_KL_vfs_unmount_in_progress(mount_t mp);

#endif

#define FUSE_KL_create_with_acl_in_finder(mp, cnp) \
        (fuse_isextendedsecurity_mp((mp)) &&       \
         ((cnp)->cn_nameptr[0] == '.' && (cnp)->cn_nameptr[1] == '_'))

#define FUSE_KL_skiprevoke(vp, action) \
    (vp && (vnode_vtype(vp) == VDIR) && \
     (action & (KAUTH_VNODE_READ_ATTRIBUTES | KAUTH_VNODE_READ_SECURITY)))

#endif /* _FUSE_KLUDGES_H_ */
