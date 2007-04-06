/*
 * Copyright (C) 2006 Google. All Rights Reserved.
 * Amit Singh <singh@>
 */

#include "fuse.h"
#include "fuse_ipc.h"
#include "fuse_node.h"
#include "fuse_locking.h"

lck_attr_t     *fuse_lock_attr  = NULL;
lck_grp_attr_t *fuse_group_attr = NULL;
lck_grp_t      *fuse_lock_group = NULL;
lck_mtx_t      *fuse_mutex      = NULL;

/*
 * Largely identical to HFS+ locking. Much of the code is from hfs_cnode.c.
 */

/*
 * Lock a fusenode.
 */
__private_extern__
int
fusefs_lock(fusenode_t cp, enum fusefslocktype locktype)
{
    void *thread = current_thread();

    if (locktype == FUSEFS_SHARED_LOCK) {
        lck_rw_lock_shared(cp->nodelock);
        cp->nodelockowner = FUSEFS_SHARED_OWNER;
    } else {
        lck_rw_lock_exclusive(cp->nodelock);
        cp->nodelockowner = thread;
    }

    /*
     * Skip nodes that no longer exist (were deleted).
     */
    if ((locktype != FUSEFS_FORCE_LOCK) && (cp->c_flag & C_NOEXISTS)) {
        fusefs_unlock(cp);
        return (ENOENT);
    }

    return (0);
}

/*
 * Lock a pair of fusenodes.
 */
__private_extern__
int
fusefs_lockpair(fusenode_t cp1, fusenode_t cp2, enum fusefslocktype locktype)
{
    fusenode_t first, last;
    int error;

    /*
     * If cnodes match then just lock one.
     */
    if (cp1 == cp2) {
        return fusefs_lock(cp1, locktype);
    }

    /*
     * Lock in cnode parent-child order (if there is a relationship);
     * otherwise lock in cnode address order.
     */
    if ((cp1->vtype == VDIR) && (cp1->nid == cp2->parent_nid)) {
        first = cp1;
        last = cp2;
    } else if (cp1 < cp2) {
        first = cp1;
        last = cp2;
    } else {
        first = cp2;
        last = cp1;
    }

    if ( (error = fusefs_lock(first, locktype))) {
        return (error);
    }

    if ( (error = fusefs_lock(last, locktype))) {
        fusefs_unlock(first);
        return (error);
    }

    return (0);
}

/*
 * Check ordering of two fusenodes. Return true if they are are in-order.
 */
static int
fusefs_isordered(fusenode_t cp1, fusenode_t cp2)
{
    if (cp1 == cp2) {
        return (0);
    }

    if (cp1 == NULL || cp2 == (fusenode_t )0xffffffff) {
        return (1);
    }

    if (cp2 == NULL || cp1 == (fusenode_t )0xffffffff) {
        return (0);
    }

    if (cp1->nid == cp2->parent_nid) {
        return (1);  /* cp1 is the parent and should go first */
    }

    if (cp2->nid == cp1->parent_nid) {
        return (0);  /* cp1 is the child and should go last */
    }

    return (cp1 < cp2);  /* fall-back is to use address order */
}

/*
 * Acquire 4 fusenode locks.
 *   - locked in fusenode parent-child order (if there is a relationship)
 *     otherwise lock in fusenode address order (lesser address first).
 *   - all or none of the locks are taken
 *   - only one lock taken per fusenode (dup fusenodes are skipped)
 *   - some of the fusenode pointers may be null
 */
__private_extern__
int
fusefs_lockfour(fusenode_t cp1, fusenode_t cp2, fusenode_t cp3, fusenode_t cp4,
                enum fusefslocktype locktype)
{
    fusenode_t a[3];
    fusenode_t b[3];
    fusenode_t list[4];
    fusenode_t tmp;
    int i, j, k;
    int error;

    if (fusefs_isordered(cp1, cp2)) {
        a[0] = cp1; a[1] = cp2;
    } else {
        a[0] = cp2; a[1] = cp1;
    }

    if (fusefs_isordered(cp3, cp4)) {
        b[0] = cp3; b[1] = cp4;
    } else {
        b[0] = cp4; b[1] = cp3;
    }

    a[2] = (fusenode_t )0xffffffff;  /* sentinel value */
    b[2] = (fusenode_t )0xffffffff;  /* sentinel value */

    /*
     * Build the lock list, skipping over duplicates
     */
    for (i = 0, j = 0, k = 0; (i < 2 || j < 2); ) {
        tmp = fusefs_isordered(a[i], b[j]) ? a[i++] : b[j++];
        if (k == 0 || tmp != list[k-1])
            list[k++] = tmp;
    }

    /*
     * Now we can lock using list[0 - k].
     * Skip over NULL entries.
     */
    for (i = 0; i < k; ++i) {
        if (list[i])
            if ((error = fusefs_lock(list[i], locktype))) {
                /* Drop any locks we acquired. */
                while (--i >= 0) {
                    if (list[i])
                        fusefs_unlock(list[i]);
                }
                return (error);
            }
    }

    return (0);
}

/*
 * Unlock a fusenode.
 */
__private_extern__
void
fusefs_unlock(fusenode_t cp)
{
    u_int32_t c_flag;
    vnode_t vp = NULLVP;
#if M_MACFUSE_RSRC_FORK
    vnode_t rvp = NULLVP;
#endif

    c_flag = cp->c_flag;
    cp->c_flag &= ~(C_NEED_DVNODE_PUT | C_NEED_DATA_SETSIZE);
#if M_MACFUSE_RSRC_FORK
    cp->c_flag &= ~(C_NEED_RVNODE_PUT | C_NEED_RSRC_SETSIZE);
#endif

    if (c_flag & (C_NEED_DVNODE_PUT | C_NEED_DATA_SETSIZE)) {
        vp = cp->c_vp;
    }

#if M_MACFUSE_RSRC_FORK
    if (c_flag & (C_NEED_RVNODE_PUT | C_NEED_RSRC_SETSIZE)) {
        rvp = cp->c_rsrc_vp;
    }
#endif

    cp->nodelockowner = NULL;
    fusefs_lck_rw_done(cp->nodelock);

    /* Perform any vnode post processing after fusenode lock is dropped. */
    if (vp) {
        if (c_flag & C_NEED_DATA_SETSIZE) {
            ubc_setsize(vp, 0);
        }
        if (c_flag & C_NEED_DVNODE_PUT) {
            vnode_put(vp);
        }
    }

#if M_MACFUSE_RSRC_FORK
    if (rvp) {
        if (c_flag & C_NEED_RSRC_SETSIZE) {
            ubc_setsize(rvp, 0);
        }
        if (c_flag & C_NEED_RVNODE_PUT) {
            vnode_put(rvp);
        }
    }
#endif
}

/*
 * Unlock a pair of fusenodes.
 */
__private_extern__
void
fusefs_unlockpair(fusenode_t cp1, fusenode_t cp2)
{
    fusefs_unlock(cp1);
    if (cp2 != cp1) {
        fusefs_unlock(cp2);
    }
}

/*
 * Unlock a group of fusenodes.
 */
__private_extern__
void
fusefs_unlockfour(fusenode_t cp1, fusenode_t cp2,
                  fusenode_t cp3, fusenode_t cp4)
{
    fusenode_t list[4];
    int i, k = 0;

    if (cp1) {
        fusefs_unlock(cp1);
        list[k++] = cp1;
    }

    if (cp2) {
        for (i = 0; i < k; ++i) {
            if (list[i] == cp2)
                goto skip1;
        }
        fusefs_unlock(cp2);
        list[k++] = cp2;
    }

skip1:
    if (cp3) {
        for (i = 0; i < k; ++i) {
            if (list[i] == cp3)
                goto skip2;
        }
        fusefs_unlock(cp3);
        list[k++] = cp3;
    }

skip2:
    if (cp4) {
        for (i = 0; i < k; ++i) {
            if (list[i] == cp4)
                return;
        }
        fusefs_unlock(cp4);
    }
}

/*
 * Protect a fusenode against truncation.
 *
 * Used mainly by read/write since they don't hold the fusenode lock across
 * calls to the cluster layer.
 *
 * The process doing a truncation must take the lock exclusive. The read/write
 * processes can take it non-exclusive.
 */
__private_extern__
void
fusefs_lock_truncate(fusenode_t cp, lck_rw_type_t lck_rw_type)
{
    if (cp->nodelockowner == current_thread()) {
        panic("MacFUSE: fusefs_lock_truncate: cnode 0x%08x locked!", cp);
    }

    lck_rw_lock(cp->truncatelock, lck_rw_type);
}

__private_extern__
void
fusefs_unlock_truncate(fusenode_t cp)
{
    fusefs_lck_rw_done(cp->truncatelock);
}

#include <IOKit/IOLocks.h>

__private_extern__
void
fusefs_lck_rw_done(lck_rw_t *lock)
{
    IORWLockUnlock((IORWLock *)lock);
}
