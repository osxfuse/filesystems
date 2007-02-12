/*
 * Copyright (C) 2006 Google. All Rights Reserved.
 * Amit Singh <singh@>
 */

#ifndef _FUSE_IOCTL_H_
#define _FUSE_IOCTL_H_

#include <sys/ioctl.h>

/* FUSEDEVIOCxxx */

/* Tell the kernel which operations the daemon implements. */
#define FUSEDEVIOCSETIMPLEMENTEDBITS  _IOW('F', 1,  u_int64_t)

/* Check if FUSE_INIT kernel-user handshake is complete. */
#define FUSEDEVIOCISHANDSHAKECOMPLETE _IOR('F', 2,  u_int32_t)

/* Mark the daemon as dead. */
#define FUSEDEVIOCDAEMONISDYING       _IOW('F', 3,  u_int32_t)

/*
 * The 'AVFI' (alter-vnode-for-inode) ioctls all require an inode number
 * as an argument. In the user-space library, you can get the inode number
 * from a path by using something like:
 *
 * fuse_ino_t
 * find_fuse_inode_for_path(const char *path)
 * {
 *     struct fuse_context *context = fuse_get_context();
 *     struct fuse *the_fuse = context->fuse;
 *     struct node *node find_node(the_fuse, FUSE_ROOT_ID, path);
 *     if (!node) {
 *         return 0;
 *     }
 *     return (node->nodeid);
 * }
 */

struct fuse_avfi_ioctl {
    uint64_t inode;
    uint32_t cmd;
    uint32_t flags;
};

/* Alter the vnode (if any) specified by the given inode. */
#define FUSEDEVIOCALTERVNODEFORINODE  _IOW('F', 4,  struct fuse_avfi_ioctl)
#define FSCTLALTERVNODEFORINODE       IOCBASECMD(FUSEDEVIOCALTERVNODEFORINODE)

/*
 * Possible cmd values for AVFI.
 */

#define FUSE_AVFI_MARKGONE       0x00000001 /* no flags   */
#define FUSE_AVFI_PURGEATTRCACHE 0x00000002 /* no flags   */
#define FUSE_AVFI_PURGEVNCACHE   0x00000004 /* no flags   */
#define FUSE_AVFI_UBC            0x00000008 /* uses flags */

#define FUSE_SETACLSTATE              _IOW('F', 5, int32_t)
#define FSCTLSETACLSTATE              IOCBASECMD(FUSE_SETACLSTATE)

#endif /* _FUSE_IOCTL_H_ */
