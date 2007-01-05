/*
 * Copyright (C) 2006 Google. All Rights Reserved.
 * Amit Singh <singh@>
 */

#include "fuse.h"
#include "fuse_file.h"
#include "fuse_ipc.h"
#include "fuse_node.h"
#include "fuse_sysctl.h"

int
fuse_filehandle_get(vnode_t vp, vfs_context_t context, fufh_type_t fufh_type)
{
    struct fuse_vnode_data *fvdat = VTOFUD(vp);
    struct fuse_dispatcher  fdi;
    struct fuse_open_in    *foi;
    struct fuse_open_out   *foo;
    struct fuse_filehandle *fufh;

    int err = 0;
    int isdir = 0;
    int op = FUSE_OPEN;
    int oflags = fuse_filehandle_xlate_to_oflags(fufh_type);

    fuse_trace_printf("fuse_filehandle_get(vp=%p, fufh_type=%d)\n",
                      vp, fufh_type);

    fufh = &(fvdat->fufh[fufh_type]);
    if (fufh->fufh_flags & FUFH_VALID) {
        printf("the given fufh type is already valid ... called in vain\n");
        return 0;
    }

    if (vnode_vtype(vp) == VDIR) {
        isdir = 1;
        op = FUSE_OPENDIR;
        if (fufh_type != FUFH_RDONLY) {
            printf("non-rdonly fh requested for a directory?\n");
            fufh_type = FUFH_RDONLY;
        }
    }

    fdisp_init(&fdi, sizeof(*foi));
    fdisp_make_vp(&fdi, op, vp, context);

    foi = fdi.indata;
    foi->flags = oflags;

    fuse_fh_upcall_count++;
    if ((err = fdisp_wait_answ(&fdi))) {
        debug_printf("OUCH ... daemon didn't give fh (err = %d)\n", err);
        return err;
    }

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
        printf("trying to put invalid filehandle?\n");
        return 0;
    }

    if (fufh->open_count != 0) {
        panic("trying to put fufh with open count %d\n", fufh->open_count);
    }

    if (fufh->fufh_flags & FUFH_MAPPED) {
        panic("trying to put mapped fufh\n");
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
            fuse_ticket_drop(fdi.tick);
        }
    } else {
        fuse_insert_callback(fdi.tick, NULL);
        fuse_insert_message(fdi.tick);
    }

out:
    fufh->fufh_flags &= ~FUFH_VALID;

    return (err);
}
