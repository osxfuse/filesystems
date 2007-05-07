/*
 * Copyright (C) 2006 Google. All Rights Reserved.
 * Amit Singh <singh@>
 */

#include "fuse.h"
#include "fuse_file.h"
#include "fuse_internal.h"
#include "fuse_ipc.h"
#include "fuse_node.h"
#include "fuse_sysctl.h"

int
fuse_filehandle_get(vnode_t       vp,
                    vfs_context_t context,
                    fufh_type_t   fufh_type,
                    int           mode)
{
    struct fuse_vnode_data *fvdat = VTOFUD(vp);
    struct fuse_dispatcher  fdi;
    struct fuse_open_in    *foi;
    struct fuse_open_out   *foo;
    struct fuse_filehandle *fufh;

    int err = 0;
    int isdir = 0;
    int op = FUSE_OPEN;
    int oflags;

    fuse_trace_printf("fuse_filehandle_get(vp=%p, fufh_type=%d, mode=%x)\n",
                      vp, fufh_type, mode);

    /*
     * Note that this means we are effectively FILTERING OUT open flags.
     */
    (void)mode;
    oflags = fuse_filehandle_xlate_to_oflags(fufh_type);
    
    fufh = &(fvdat->fufh[fufh_type]);
    if (fufh->fufh_flags & FUFH_VALID) {
        IOLog("MacFUSE: fufh (type=%d) already valid... called in vain\n",
              fufh_type);
        return 0;
    }

    if (vnode_vtype(vp) == VDIR) {
        isdir = 1;
        op = FUSE_OPENDIR;
        if (fufh_type != FUFH_RDONLY) {
            IOLog("MacFUSE: non-rdonly fufh requested for directory\n");
            fufh_type = FUFH_RDONLY;
        }
    }

    fdisp_init(&fdi, sizeof(*foi));
    fdisp_make_vp(&fdi, op, vp, context);

    foi = fdi.indata;
    foi->flags = oflags;

    FUSE_OSAddAtomic(1, (SInt32 *)&fuse_fh_upcall_count);
    if ((err = fdisp_wait_answ(&fdi))) {
        IOLog("MacFUSE: OUCH! daemon did not give fh (type=%d, err=%d)\n",
              fufh_type, err);
        if (err == ENOENT) {
            /*
             * See comment in fuse_vnop_reclaim().
             */
            cache_purge(vp);
        }
        return err;
    }
    FUSE_OSAddAtomic(1, (SInt32 *)&fuse_fh_current);

    foo = fdi.answ;

    fufh->fufh_flags |= (0 | FUFH_VALID);
    fufh->open_count = 1;
    fufh->open_flags = oflags;
    fufh->fuse_open_flags = foo->open_flags;
    fufh->type = fufh_type;
    fufh->fh_id = foo->fh;
    
    fuse_ticket_drop(fdi.tick);

    return 0;
}

int
fuse_filehandle_put(vnode_t vp, vfs_context_t context, fufh_type_t fufh_type,
                    int foregrounded)
{
    struct fuse_dispatcher  fdi;
    struct fuse_release_in *fri;
    struct fuse_vnode_data *fvdat = VTOFUD(vp);
    struct fuse_filehandle *fufh = NULL;

    int op = FUSE_RELEASE;
    int err = 0;
    int isdir = 0;

    fuse_trace_printf("fuse_filehandle_put(vp=%p, fufh_type=%d)\n",
                      vp, fufh_type);

    fufh = &(fvdat->fufh[fufh_type]);
    if (!(fufh->fufh_flags & FUFH_VALID)) {
        IOLog("MacFUSE: filehandle is already invalid (type=%d)\n", fufh_type);
        return 0;
    }

    if (fufh->open_count != 0) {
        panic("MacFUSE: trying to put fufh with open count %d (type=%d)\n",
              fufh->open_count, fufh_type);
        /* NOTREACHED */
    }

    if (fufh->fufh_flags & FUFH_MAPPED) {
        panic("MacFUSE: trying to put mapped fufh (type=%d)\n", fufh_type);
        /* NOTREACHED */
    }

    if (fuse_isdeadfs(vp)) {
        goto out;
    }

    if (vnode_vtype(vp) == VDIR) {
        op = FUSE_RELEASEDIR;
        isdir = 1;
    }

    fdisp_init(&fdi, sizeof(*fri));
    fdisp_make_vp(&fdi, op, vp, context);
    fri = fdi.indata;
    fri->fh = fufh->fh_id;
    fri->flags = fufh->open_flags;

    if (foregrounded) {
        if ((err = fdisp_wait_answ(&fdi))) {
            goto out;
        } else {
            FUSE_OSAddAtomic(-1, (SInt32 *)&fuse_fh_current);
            fuse_ticket_drop(fdi.tick);
        }
    } else {
        fuse_insert_callback(fdi.tick, NULL);
        fuse_insert_message(fdi.tick);
        FUSE_OSAddAtomic(-1, (SInt32 *)&fuse_fh_current);
    }

out:
    fufh->fufh_flags &= ~FUFH_VALID;

    return (err);
}
