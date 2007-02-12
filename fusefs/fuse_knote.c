/*
 * Copyright (C) 2007 Google. All Rights Reserved.
 * Amit Singh <singh@>
 */

#include <fuse_param.h>

#if MACFUSE_ENABLE_UNSUPPORTED

#include "fuse.h"
#include "fuse_knote.h"
#include "fuse_node.h"

struct filterops fuseread_filtops =
    { 1, NULL, filt_fusedetach, filt_fuseread };
struct filterops fusewrite_filtops =
    { 1, NULL, filt_fusedetach, filt_fusewrite };
struct filterops fusevnode_filtops =
    { 1, NULL, filt_fusedetach, filt_fusevnode };

/* Like HFS+ ... */

void
filt_fusedetach(struct knote *kn)
{
    struct vnode *vp;
        
    vp = (struct vnode *)kn->kn_hook;
    if (vnode_getwithvid(vp, kn->kn_hookid)) {
        return;
    }

    if (1) {  /* !KNDETACH_VNLOCKED */
        if (/* take exclusive lock */ 1) {
            (void)KNOTE_DETACH(&VTOFUD(vp)->c_knotes, kn);
            /* release lock */
        }
    }

    vnode_put(vp);
}

int
filt_fuseread(struct knote *kn, long hint)
{
    vnode_t vp = (vnode_t)kn->kn_hook;
    int dropvp = 0;
    int result = 0;

    if (hint == 0)  {
        if ((vnode_getwithvid(vp, kn->kn_hookid) != 0)) {
            hint = NOTE_REVOKE;
        } else  {
            dropvp = 1;
        }
    }

    if (hint == NOTE_REVOKE) {
        /*
         * filesystem is gone, so set the EOF flag and schedule 
         * the knote for deletion.
         */
        kn->kn_flags |= (EV_EOF | EV_ONESHOT);
        return (1);
    }

    /* poll(2) semantics dictate always saying there is data */
    if (kn->kn_flags & EV_POLL) {
        kn->kn_data = 1;
        result = 1;
    } else {
        /* I'm not going to look inside kn_fp now... to hell with it. */
        kn->kn_data = 1;
        result = 1;
    }

    if (dropvp) {
        vnode_put(vp);
    }

    return (kn->kn_data != 0);
}

int
filt_fusewrite(struct knote *kn, long hint)
{
    if (hint == 0)  {
        if ((vnode_getwithvid((vnode_t)kn->kn_hook, kn->kn_hookid) != 0)) {
            hint = NOTE_REVOKE;
        } else 
            vnode_put((vnode_t)kn->kn_hook);
    }

    if (hint == NOTE_REVOKE) {
        /*
         * filesystem is gone, so set the EOF flag and schedule 
         * the knote for deletion.
         */
        kn->kn_data = 0;
        kn->kn_flags |= (EV_EOF | EV_ONESHOT);
        return (1);
    }

    kn->kn_data = 0;

    return (1);
}

int
filt_fusevnode(struct knote *kn, long hint)
{
    if (hint == 0)  {
        if ((vnode_getwithvid((vnode_t)kn->kn_hook, kn->kn_hookid) != 0)) {
            hint = NOTE_REVOKE;
        } else {
            vnode_put((vnode_t)kn->kn_hook);
        }
    }

    if (kn->kn_sfflags & hint) {
        kn->kn_fflags |= hint;
    }

    if ((hint == NOTE_REVOKE)) {
        kn->kn_flags |= (EV_EOF | EV_ONESHOT);
        return (1);
    }
        
    return (kn->kn_fflags != 0);
}

#endif /* MACFUSE_ENABLE_UNSUPPORTED */
