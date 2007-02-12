/*
 * Copyright (C) 2006 Google. All Rights Reserved.
 * Amit Singh <singh@>
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <mach/mach_types.h>
#include <miscfs/devfs/devfs.h>

#include <libkern/libkern.h>
#include <libkern/OSMalloc.h>
#include <libkern/locks.h>

#include "fuse.h"
#include "fuse_device.h"
#include "fuse_ipc.h"
#include "fuse_locking.h"
#include "fuse_node.h"
#include "fuse_nodehash.h"
#include "fuse_sysctl.h"
#include <fuse_mount.h>

OSMallocTag  fuse_malloc_tag = NULL;

extern struct vfs_fsentry fuse_vfs_entry;
extern vfstable_t         fuse_vfs_table_ref;

kern_return_t fusefs_start(kmod_info_t *ki, void *d);
kern_return_t fusefs_stop(kmod_info_t *ki, void *d);

static void
fini_stuff(void)
{
    if (fuse_mutex) {
        lck_mtx_free(fuse_mutex, fuse_lock_group);
        fuse_mutex = NULL;
    }

    if (fuse_lock_group) {
        lck_grp_free(fuse_lock_group);
        fuse_lock_group = NULL;
    }

    if (fuse_malloc_tag) {
        OSMalloc_Tagfree(fuse_malloc_tag);
        fuse_malloc_tag = NULL;
    }

    if (fuse_lock_attr) {
        lck_attr_free(fuse_lock_attr);
        fuse_lock_attr = NULL;
    }

    if (fuse_group_attr) {
        lck_grp_attr_free(fuse_group_attr);
        fuse_group_attr = NULL;
    }
}

static kern_return_t
init_stuff(void)
{
    kern_return_t ret = KERN_SUCCESS;
    
    fuse_malloc_tag = OSMalloc_Tagalloc(MACFUSE_BUNDLE_IDENTIFIER,
                                        OSMT_DEFAULT);
    if (fuse_malloc_tag == NULL) {
        ret = KERN_FAILURE;
    }

    fuse_lock_attr = lck_attr_alloc_init();
    fuse_group_attr = lck_grp_attr_alloc_init();
    lck_attr_setdebug(fuse_lock_attr);

    if (ret == KERN_SUCCESS) {
        fuse_lock_group = lck_grp_alloc_init(MACFUSE_BUNDLE_IDENTIFIER,
                                             fuse_group_attr);
        if (fuse_lock_group == NULL) {
            ret = KERN_FAILURE;
        }
    }

    if (ret == KERN_SUCCESS) {
        fuse_mutex = lck_mtx_alloc_init(fuse_lock_group, fuse_lock_attr);
        if (fuse_mutex == NULL) {
            ret = ENOMEM;
        }
    }

    if (ret != KERN_SUCCESS) {
        fini_stuff();
    }

    return ret;
}

kern_return_t
fusefs_start(__unused kmod_info_t *ki, __unused void *d)
{
    int ret;

    ret = init_stuff();
    if (ret != KERN_SUCCESS) {
        return KERN_FAILURE;
    }

    ret = HNodeInit(fuse_lock_group, fuse_lock_attr, fuse_malloc_tag,
                    kHNodeMagic, sizeof(struct fuse_vnode_data));
    if (ret != KERN_SUCCESS) {
        goto error;
    }

    ret = fuse_devices_start();
    if (ret != KERN_SUCCESS) {
        goto error;
    }

    ret = vfs_fsadd(&fuse_vfs_entry, &fuse_vfs_table_ref);
    if (ret != 0) {
        goto error;
    }

    fuse_sysctl_start();

    IOLog("MacFUSE: starting (version %s, %s)\n",
          MACFUSE_VERSION, MACFUSE_TIMESTAMP);

    return KERN_SUCCESS;

error:
    HNodeTerm();
    fini_stuff();
    fuse_devices_stop();

    return KERN_FAILURE;
}

kern_return_t
fusefs_stop(__unused kmod_info_t *ki, __unused void *d)
{
    int ret;

    ret = fuse_devices_stop();
    if (ret != KERN_SUCCESS) {
        return KERN_FAILURE;
    }

    ret = vfs_fsremove(fuse_vfs_table_ref);
    if (ret != KERN_SUCCESS) {
        return KERN_FAILURE;
    }

    HNodeTerm();
    fini_stuff();

    fuse_sysctl_stop();

    IOLog("MacFUSE: stopping (version %s, %s)\n",
          MACFUSE_VERSION, MACFUSE_TIMESTAMP);

    return KERN_SUCCESS;
}
