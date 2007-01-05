/*
 * Copyright (C) 2006 Google. All Rights Reserved.
 * Amit Singh <singh@>
 */

#ifndef _FUSE_VFSOPS_H_
#define _FUSE_VFSOPS_H_

#include <sys/kernel_types.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/vnode.h>

struct fuse_data;
struct fuse_ticket;

extern struct vnodeopv_entry_desc fuse_vnode_operation_entries[];

/* VFS operations */

static errno_t
fuse_vfs_mount(mount_t mp, vnode_t devvp, user_addr_t data,
               vfs_context_t context);

static errno_t
fuse_vfs_unmount(mount_t mp, int mntflags, vfs_context_t context);

static errno_t
fuse_vfs_root(mount_t mp, vnode_t *vpp, vfs_context_t context);

static errno_t
fuse_vfs_getattr(mount_t mp, struct vfs_attr *attr, vfs_context_t context);

static errno_t
fuse_vfs_sync(mount_t mp, int waitfor, vfs_context_t context);

static errno_t
fuse_vfs_setattr(mount_t mp, struct vfs_attr *fsap,
                 __unused vfs_context_t context);

#endif /* _FUSE_VFSOPS_H_ */
