/*
 * Copyright (C) 2006 Google. All Rights Reserved.
 * Amit Singh <singh@>
 */

#include <sys/param.h>
#include <kern/assert.h>
#include <libkern/libkern.h>
#include <libkern/OSMalloc.h>
#include <libkern/locks.h>
#include <mach/mach_types.h>
#include <sys/dirent.h>
#include <sys/disk.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/kernel_types.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/stat.h>
#include <sys/ubc.h>
#include <sys/unistd.h>
#include <sys/vnode.h>
#include <sys/vnode_if.h>
#include <sys/xattr.h>
#include <sys/buf.h>
#include <sys/namei.h>
#include <sys/mman.h>
#include <vfs/vfs_support.h>

#include "fuse.h"
#include "fuse_file.h"
#include "fuse_internal.h"
#include <fuse_ioctl.h>
#include "fuse_ipc.h"
#include "fuse_kludges.h"
#include "fuse_knote.h"
#include "fuse_locking.h"
#include "fuse_node.h"
#include "fuse_nodehash.h"
#include <fuse_param.h>
#include "fuse_sysctl.h"
#include "fuse_vnops.h"

/*
    struct vnop_access_args {
        struct vnodeop_desc *a_desc;
        vnode_t              a_vp;
        int                  a_action;
        vfs_context_t        a_context;
    };
*/
static int
fuse_vnop_access(struct vnop_access_args *ap)
{
    vnode_t vp = ap->a_vp;
    int action = ap->a_action;
    vfs_context_t context = ap->a_context;
    struct fuse_access_param facp;
    struct fuse_vnode_data *fvdat = VTOFUD(vp);
    struct fuse_data *data = fuse_get_mpdata(vnode_mount(vp));

    fuse_trace_printf_vnop();

    if (fuse_isdeadfs(vp)) {
        if (vnode_isvroot(vp)) {
            return 0;
        } else {
            return EBADF;
        }
    }

    if (!(data->dataflags & FSESS_INITED)) {
        if (vnode_isvroot(vp)) {
            if (fuse_vfs_context_issuser(context) ||
               (fuse_match_cred(data->daemoncred,
                                vfs_context_ucred(context)) == 0)) {
                return 0;
            }
        }
        return EBADF;
    }

    bzero(&facp, sizeof(facp));

    if (fvdat->flags & FVP_ACCESS_NOOP) {
        fvdat->flags &= ~FVP_ACCESS_NOOP;
    } else {
        facp.facc_flags |= FACCESS_DO_ACCESS;
    }   

    return fuse_internal_access(vp, action, context, &facp);
}       

/*
    struct vnop_blktooff_args {
        struct vnodeop_desc *a_desc;
        vnode_t              a_vp;
        daddr64_t            a_lblkno;
        off_t               *a_offset;
    };
*/
static int 
fuse_vnop_blktooff(struct vnop_blktooff_args *ap)
{       
    vnode_t    vp;
    daddr64_t  lblkno;
    off_t     *offsetPtr;
    struct fuse_data *data; 
        
    fuse_trace_printf_vnop();

    vp        = ap->a_vp;
    lblkno    = ap->a_lblkno;
    offsetPtr = ap->a_offset;

    if (fuse_isdeadfs_nop(vp)) {
        return EIO;
    }

    data = fuse_get_mpdata(vnode_mount(vp));

    *offsetPtr = lblkno * (off_t)(data->blocksize);

    return 0;
}

/*
    struct vnop_blockmap_args {
        struct vnodeop_desc *a_desc;
        vnode_t              a_vp;
        off_t                a_foffset;
        size_t               a_size;
        daddr64_t           *a_bpn;
        size_t              *a_run;
        void                *a_poff;
        int                  a_flags;
        vfs_context_t        a_context;
    };
*/
static int
fuse_vnop_blockmap(struct vnop_blockmap_args *ap)
{
    vnode_t       vp;
    off_t         foffset;
    size_t        size;
    daddr64_t    *bpnPtr;
    size_t       *runPtr;
    int          *poffPtr;
    int           flags;
    vfs_context_t context;
    off_t         contiguousPhysicalBytes;
    struct fuse_vnode_data *fvdat;
    struct fuse_data *data;

    vp      = ap->a_vp;
    foffset = ap->a_foffset;
    size    = ap->a_size;
    bpnPtr  = ap->a_bpn;
    runPtr  = ap->a_run;
    poffPtr = (int *)ap->a_poff;
    flags   = ap->a_flags;
    context = ap->a_context;

    fuse_trace_printf_vnop();

    if (fuse_isdeadfs_nop(vp)) {
        return EIO;
    }

    fvdat = VTOFUD(vp);
    data = fuse_get_mpdata(vnode_mount(vp));

    *bpnPtr = foffset / data->blocksize;

    contiguousPhysicalBytes = \
        fvdat->filesize - (off_t)(*bpnPtr * data->blocksize);

    if (contiguousPhysicalBytes > size) {
        contiguousPhysicalBytes = (off_t)size;
    }

    *runPtr = (size_t)contiguousPhysicalBytes;

    if (poffPtr != NULL) {
        *poffPtr = 0;
    }

    debug_printf("offset=%lld, size=%ld, bpn=%lld, run=%ld, filesize=%lld\n",
                 foffset, size, *bpnPtr, *runPtr, fvdat->filesize);

    return 0;
}

/*
    struct vnop_close_args {
        struct vnodeop_desc *a_desc;
        vnode_t              a_vp;
        int                  a_fflag;
        vfs_context_t        a_context;
    };
*/
static int
fuse_vnop_close(struct vnop_close_args *ap)
{
    vnode_t vp = ap->a_vp;
    int err = 0, isdir = (vnode_vtype(vp) == VDIR) ? 1 : 0;

    fufh_type_t             fufh_type;
    struct fuse_data       *data;
    struct fuse_vnode_data *fvdat = VTOFUD(vp);
    struct fuse_filehandle *fufh = NULL;

    fuse_trace_printf_vnop();

    if (fuse_isdeadfs_nop(vp)) {
        return 0;
    }

    /* vclean() calls VNOP_CLOSE with fflag set to IO_NDELAY. */
    if (ap->a_fflag == IO_NDELAY) {
        return 0;
    }

    if (isdir) {
        fufh_type = FUFH_RDONLY;
    } else {
        fufh_type = fuse_filehandle_xlate_from_fflags(ap->a_fflag);
    }

    fufh = &(fvdat->fufh[fufh_type]);

    if (!(fufh->fufh_flags & FUFH_VALID)) {
        if ((fufh->open_count == 0) && !(fufh->fufh_flags & FUFH_MAPPED)) {
            return 0;
        }
        panic("MacFUSE: fufh invalid in close [type=%d oc=%d vtype=%d cf=%d]\n",
              fufh_type, fufh->open_count, vnode_vtype(vp), ap->a_fflag);
    }

    fufh->open_count--;

    if (isdir) {
        goto skipdir;
    }

    /* Enforce sync on close unless explicitly told not to. */
    if (vnode_hasdirtyblks(vp) && !fuse_isnosynconclose(vp)) {
        (void)cluster_push(vp, IO_SYNC | IO_CLOSE);
    }

    data = fuse_get_mpdata(vnode_mount(vp));
    if (!(data->noimplflags & FSESS_NOIMPL(FLUSH))) {

        struct fuse_dispatcher  fdi;
        struct fuse_flush_in   *ffi;

        fdisp_init(&fdi, sizeof(*ffi));
        fdisp_make_vp(&fdi, FUSE_FLUSH, vp, ap->a_context);

        ffi = fdi.indata;
        ffi->fh = fufh->fh_id;
        ffi->unused = 0;
        ffi->padding = 0;
        ffi->lock_owner = 0;

        err = fdisp_wait_answ(&fdi);

        if (!err) {
            fuse_ticket_drop(fdi.tick);
        } else {
            if (err == ENOSYS) {
                data->noimplflags |= FSESS_NOIMPL(FLUSH);
                err = 0;
            }
        }
    }

skipdir:

    if ((fufh->open_count == 0) &&
        !(fufh->fufh_flags & FUFH_MAPPED)) {
        (void)fuse_filehandle_put(vp, ap->a_context, fufh_type, 0);
    }

    return err;
}

/*
    struct vnop_create_args {
        struct vnodeop_desc  *a_desc;
        vnode_t               a_dvp;
        vnode_t              *a_vpp;
        struct componentname *a_cnp;
        struct vnode_attr    *a_vap;
        vfs_context_t         a_context;
    };
*/
static int
fuse_vnop_create(struct vnop_create_args *ap)
{
    vnode_t  dvp = ap->a_dvp;
    vnode_t *vpp = ap->a_vpp;
    vfs_context_t context = ap->a_context;

    struct componentname *cnp = ap->a_cnp;
    struct vnode_attr    *vap = ap->a_vap;

    struct fuse_dispatcher  fdi;
    struct fuse_dispatcher *fdip = &fdi;
    struct fuse_entry_out  *feo;
    struct fuse_mknod_in    fmni;
    struct fuse_open_in    *foi;

    int err;
    int gone_good_old = 0;

    mount_t mp = vnode_mount(dvp);
    uint64_t parentnid = VTOFUD(dvp)->nid;
    mode_t mode = MAKEIMODE(vap->va_type, vap->va_mode);

    fuse_trace_printf_vnop();

    if (fuse_isdeadfs_nop(dvp)) {
        panic("MacFUSE: fuse_vnop_create(): called on a dead file system");
    }

    CHECK_BLANKET_DENIAL(dvp, context, EPERM);

    if (fuse_skip_apple_special_mp(mp, cnp->cn_nameptr, cnp->cn_namelen)) {
        return EPERM;
    }

    bzero(&fdi, sizeof(fdi));

    // XXX: Will we ever want devices?
    if ((vap->va_type != VREG) ||
        fuse_get_mpdata(mp)->noimplflags & FSESS_NOIMPL(CREATE)) {
        goto good_old;
    }

    debug_printf("parent nid = %llu, mode = %x\n", parentnid, mode);

    fdisp_init(fdip, sizeof(*foi) + cnp->cn_namelen + 1);
    if (fuse_get_mpdata(vnode_mount(dvp))->noimplflags & FSESS_NOIMPL(CREATE)) {
        debug_printf("eh, daemon doesn't implement create?\n");
        goto good_old;
    }

    fdisp_make(fdip, FUSE_CREATE, vnode_mount(dvp), parentnid, context);

    foi = fdip->indata;
    foi->mode = mode;
    foi->flags = O_CREAT | O_RDWR; // XXX: We /always/ creat() like this.

    memcpy((char *)fdip->indata + sizeof(*foi), cnp->cn_nameptr,
           cnp->cn_namelen);
    ((char *)fdip->indata)[sizeof(*foi) + cnp->cn_namelen] = '\0';

    err = fdisp_wait_answ(fdip);

    if (err == ENOSYS) {
        debug_printf("create: got ENOSYS from daemon\n");
        fuse_get_mpdata(vnode_mount(dvp))->noimplflags |= FSESS_NOIMPL(CREATE);
        fdip->tick = NULL;
        goto good_old;
    } else if (err) {
        debug_printf("create: darn, got err=%d from daemon\n", err);
        goto undo;
    }

    goto bringup;

good_old:
    gone_good_old = 1;
    fmni.mode = mode; /* fvdat->flags; */
    fmni.rdev = 0;
    fuse_internal_newentry_makerequest(vnode_mount(dvp), parentnid, cnp,
                                       FUSE_MKNOD, &fmni, sizeof(fmni),
                                       fdip, context);
    err = fdisp_wait_answ(fdip);
    if (err) {
        goto undo;
    }

bringup:
    feo = fdip->answ;

    if ((err = fuse_internal_checkentry(feo, VREG))) {
        fuse_ticket_drop(fdip->tick);
        goto undo;
    }

    err = FSNodeGetOrCreateFileVNodeByID(mp,
                                         context,
                                         feo->nodeid,
                                         dvp,
                                         VREG,
                                         FUSE_ZERO_SIZE,
                                         vpp,
                                         (gone_good_old) ? 0 : FN_CREATING,
                                         NULL); // oflags
    if (err) {
       if (gone_good_old) {
           fuse_internal_forget_send(mp, context, feo->nodeid, 1, fdip);
       } else {
           struct fuse_release_in *fri;
           uint64_t nodeid = feo->nodeid;
           uint64_t fh_id = ((struct fuse_open_out *)(feo + 1))->fh;

           fdisp_init(fdip, sizeof(*fri));
           fdisp_make(fdip, FUSE_RELEASE, mp, nodeid, context);
           fri = fdip->indata;
           fri->fh = fh_id;
           fri->flags = OFLAGS(mode);
           fuse_insert_callback(fdip->tick, fuse_internal_forget_callback);
           fuse_insert_message(fdip->tick);
       }
       return err;
    }

    fdip->answ = gone_good_old ? NULL : feo + 1;

    if (!gone_good_old) {
        uint64_t x_fh_id = ((struct fuse_open_out *)(feo + 1))->fh;
        uint32_t x_open_flags = ((struct fuse_open_out *)(feo + 1))->open_flags;
        struct fuse_vnode_data *fvdat = VTOFUD(*vpp);
        struct fuse_filehandle *fufh = &(fvdat->fufh[FUFH_RDWR]);

        fufh->fh_id = x_fh_id;
        fufh->open_flags = x_open_flags;

        FUSE_OSAddAtomic(1, (SInt32 *)&fuse_fh_current);

#if M_MACFUSE_EXPERIMENTAL_JUNK
        struct fuse_dispatcher x_fdi;
        struct fuse_release_in *x_fri;
        fdisp_init(&x_fdi, sizeof(*x_fri));
        fdisp_make_vp(&x_fdi, FUSE_RELEASE, *vpp, context);
        x_fri = x_fdi.indata;
        x_fri->fh = x_fh_id;
        x_fri->flags = O_WRONLY;
        fuse_insert_callback(x_fdi.tick, NULL);
        fuse_insert_message(x_fdi.tick);
#endif

    }

#if M_MACFUSE_EXPERIMENTAL_JUNK
    if (fuse_isnovncache_mp(mp) ||
        /*
         * This is to work around the Finder giving up with error -43 when
         * you try to copy a folder into a MacFUSE volume that has extended
         * security (ACLs) enabled. The Finder doesn't do this in the case
         * of non-directories. The following "fix" fixes the problem. Well,
         * what can I say?
         */
        FUSE_KL_create_with_acl_in_finder(mp, cnp)) {
        /* no name cache */

    } else {
        fuse_vncache_enter(dvp, *vpp, cnp);
    }
#endif

    fuse_ticket_drop(fdip->tick);

    FUSE_KNOTE(dvp, NOTE_WRITE);

    return (0);

undo:
    return (err);
}

/*
 * Our vnop_fsync roughly corresponds to the FUSE_FSYNC method. The Linux
 * version of FUSE also has a FUSE_FLUSH method.
 *
 * On Linux, fsync() synchronizes a file's complete in-core state with that
 * on disk. The call is not supposed to return until the system has completed 
 * that action or until an error is detected.
 *
 * Linux also has an fdatasync() call that is similar to fsync() but is not
 * required to update the metadata such as access time and modification time.
 */

/*
    struct vnop_fsync_args {
        struct vnodeop_desc *a_desc;
        vnode_t              a_vp;
        int                  a_waitfor;
        vfs_context_t        a_context;
    };
*/
static int
fuse_vnop_fsync(struct vnop_fsync_args *ap)
{
    vnode_t vp = ap->a_vp;
    vfs_context_t context = ap->a_context;
    struct fuse_dispatcher fdi;
    struct fuse_filehandle *fufh;
    struct fuse_vnode_data *fvdat = VTOFUD(vp);
    int type;
#if M_MACFUSE_EXPERIMENTAL_JUNK
    int err = 0;
    int waitfor = ap->a_waitfor;
    struct timeval tv;
    int wait = (waitfor == MNT_WAIT);
#endif
    fuse_trace_printf_vnop();

    if (fuse_isdeadfs_nop(vp)) {
        return 0;
    }

    cluster_push(vp, 0);

#if M_MACFUSE_EXPERIMENTAL_JUNK
    buf_flushdirtyblks(vp, wait, 0, (char *)"fuse_fsync");
    microtime(&tv);
#endif

    // vnode and ubc are in lock-step.
    // can call vnode_isinuse().
    // can call ubc_sync_range().

    if (fuse_get_mpdata(vnode_mount(vp))->noimplflags &
        ((vnode_vtype(vp) == VDIR) ?
            FSESS_NOIMPL(FSYNCDIR) : FSESS_NOIMPL(FSYNC))) {
        goto out;
    }

    fdisp_init(&fdi, 0);
    for (type = 0; type < FUFH_MAXTYPE; type++) {
        fufh = &(fvdat->fufh[type]);
        if (fufh->fufh_flags & FUFH_VALID) {
            fuse_internal_fsync(vp, context, fufh, &fdi);
        }
    }

out:
    return 0;
}

#define fusetimespeccmp(tvp, uvp, cmp)           \
        (((tvp)->tv_sec == (uvp)->tv_sec) ?     \
         ((tvp)->tv_nsec cmp (uvp)->tv_nsec) :  \
         ((tvp)->tv_sec cmp (uvp)->tv_sec))

/*
    struct vnop_getattr_args {
        struct vnodeop_desc *a_desc;
        vnode_t              a_vp;
        struct vnode_attr   *a_vap;
        vfs_context_t        a_context;
    };
*/
static int
fuse_vnop_getattr(struct vnop_getattr_args *ap)
{
    int err = 0;
    int dataflags;
    struct timespec uptsp;
    struct fuse_dispatcher fdi;
    struct fuse_data *data;

    vnode_t vp = ap->a_vp;
    struct vnode_attr *vap = ap->a_vap;
    vfs_context_t context = ap->a_context;

    fuse_trace_printf_vnop();

    if (!vnode_isvroot(vp) || !fuse_vfs_context_issuser(context)) {
        CHECK_BLANKET_DENIAL(vp, context, ENOENT);
    }

    data = fuse_get_mpdata(vnode_mount(vp));
    dataflags = data->dataflags;

    /* Note that we are not bailing out on a dead file system just yet. */
    /* look for cached attributes */
    nanouptime(&uptsp);
    if (fusetimespeccmp(&uptsp, &VTOFUD(vp)->cached_attrs_valid, <=)) {
        if (vap != VTOVA(vp)) {
            fuse_internal_attr_loadvap(vp, vap);
        }
        debug_printf("fuse_getattr a: returning 0\n");
        return (0);
    }

    if (!(dataflags & FSESS_INITED)) {
        if (!vnode_isvroot(vp)) {
            fdata_kick_set(data);
            err = ENOTCONN;
            debug_printf("fuse_getattr b: returning ENOTCONN\n");
            return (err);
        } else {
            goto fake;
        }
    }

    if ((err = fdisp_simple_putget_vp(&fdi, FUSE_GETATTR, vp, context))) {
        if ((err == ENOTCONN) && vnode_isvroot(vp)) {
            /* see comment at similar place in fuse_statfs() */
            goto fake;
        }
        debug_printf("fuse_getattr c: returning ENOTCONN\n");
        return (err);
    }

    cache_attrs(vp, (struct fuse_attr_out *)fdi.answ);

    fuse_internal_attr_loadvap(vp, vap);

#if M_MACFUSE_EXPERIMENTAL_JUNK
    if (vap != VTOVA(vp)) {
        memcpy(vap, VTOVA(vp), sizeof(*vap));
    }
#endif

    /* ATTR_FUDGE_CASE */
    if (vnode_isreg(vp) && fuse_isnoubc(vp)) {
        /*
         * This is for those cases when the file size changed without us
         * knowing, and we want to catch up.
         *
         * For the sake of sanity, we don't want to do it with UBC.
         * We also don't want to do it when we have asynchronous writes
         * enabled because we might have pending writes on *our* side.
         * We're not researching distributed file systems here!
         *
         */

        struct fuse_vnode_data *fvdat = VTOFUD(vp);
        off_t new_filesize = ((struct fuse_attr_out *)fdi.answ)->attr.size;
        fvdat->filesize = new_filesize;
    }

    fuse_ticket_drop(fdi.tick);

    if (vnode_vtype(vp) != vap->va_type) {
        if (vnode_vtype(vp) == VNON && vap->va_type != VNON) {
            /*
             * We should be doing the following:
             *
             * vp->vtype = vap->v_type
             */
        } else {

            /*
             * STALE vnode, ditch
             *
             * The vnode has changed its type "behind our back". There's
             * nothing really we can do, so let us just force an internal
             * revocation and tell the caller to try again, if interested.
             */

            fuse_internal_vnode_disappear(vp, context);
            return EAGAIN;

            /*
             * Historical note:
             *
             * debug_printf("fuse_getattr d: returning ENOTCONN\n");
             * return (ENOTCONN);
             */
        }
    }

    debug_printf("fuse_getattr e: returning 0\n");

    return (0);

fake:
    bzero(vap, sizeof(*vap));
    VATTR_RETURN(vap, va_type, vnode_vtype(vp));
    VATTR_RETURN(vap, va_uid, data->daemoncred->cr_uid);
    VATTR_RETURN(vap, va_gid, data->daemoncred->cr_gid);
    VATTR_RETURN(vap, va_mode, S_IRWXU);

    return (0);
}

#if M_MACFUSE_ENABLE_XATTR
/*
    struct vnop_getxattr_args {
        struct vnodeop_desc *a_desc;
        vnode_t              a_vp;
        char                *a_name;
        uio_t                a_uio;
        size_t              *a_size;
        int                  a_options;
        vfs_context_t        a_context;
    };
*/
static int
fuse_vnop_getxattr(struct vnop_getxattr_args *ap)
{
    vnode_t vp = ap->a_vp;
    uio_t uio = ap->a_uio;
    vfs_context_t context = ap->a_context;

    struct fuse_dispatcher    fdi;
    struct fuse_getxattr_in  *fgxi; 
    struct fuse_getxattr_out *fgxo;
    struct fuse_data         *data;

    int err = 0;
    int namelen;      
    
    fuse_trace_printf_vnop();

    if (fuse_isdeadfs_nop(vp)) {
        return EBADF;
    }

    CHECK_BLANKET_DENIAL(vp, context, ENOENT);

    data = fuse_get_mpdata(vnode_mount(vp));
    if (data->noimplflags & FSESS_NOIMPL(GETXATTR)) {
        return ENOTSUP;
    }

    if (ap->a_name == NULL || ap->a_name[0] == '\0') {
        return EINVAL;
    }

    if (fuse_is_shortcircuit_xattr(ap->a_name)) {
        return ENOTSUP;
    }

    namelen = strlen(ap->a_name);

    fdisp_init(&fdi, sizeof(*fgxi) + namelen + 1);
    fdisp_make_vp(&fdi, FUSE_GETXATTR, vp, ap->a_context);
    fgxi = fdi.indata;
    
    if (uio) {
        fgxi->size = uio_resid(uio);
    } else {
        fgxi->size = 0;
    }
    
    memcpy((char *)fdi.indata + sizeof(*fgxi), ap->a_name, namelen);
    ((char *)fdi.indata)[sizeof(*fgxi) + namelen] = '\0';

    err = fdisp_wait_answ(&fdi);
    if (err) {
        if (err == ENOSYS) {
            data->noimplflags |= FSESS_NOIMPL(GETXATTR);
            return ENOTSUP;
        }
        return err;  
    }                
                     
    if (uio) {       
        *ap->a_size = fdi.iosize;
        if (fdi.iosize > uio_resid(uio)) {
            err = ERANGE;
        } else {
            err = uiomove((char *)fdi.answ, fdi.iosize, uio);
        }
    } else {
        fgxo = (struct fuse_getxattr_out *)fdi.answ;
        *ap->a_size = fgxo->size;
    }

    fuse_ticket_drop(fdi.tick);

    return err;
}
#endif /* M_MACFUSE_ENABLE_XATTR */

/*
    struct vnop_inactive_args {
        struct vnodeop_desc *a_desc;
        vnode_t              a_vp;
        vfs_context_t        a_context;
    };
*/
static int
fuse_vnop_inactive(struct vnop_inactive_args *ap)
{
    vnode_t vp = ap->a_vp;
    struct fuse_vnode_data *fvdat = VTOFUD(vp);
    struct fuse_filehandle *fufh = NULL;
    int fufh_type;

    fuse_trace_printf_vnop();

    for (fufh_type = 0; fufh_type < FUFH_MAXTYPE; fufh_type++) {

        fufh = &(fvdat->fufh[fufh_type]);

        if (fufh->fufh_flags & FUFH_VALID) {
            fufh->fufh_flags &= ~FUFH_MAPPED;
            fufh->open_count = 0;
            (void)fuse_filehandle_put(vp, ap->a_context, fufh_type, 0);
        }
    }

    return 0;
}

/*
    struct vnop_ioctl_args {
        struct vnodeop_desc *a_desc;
        vnode_t              a_vp;
        u_long               a_command;
        caddr_t              a_data;
        int                  a_fflag;
        vfs_context_t        a_context;
    };
*/
static int
fuse_vnop_ioctl(struct vnop_ioctl_args *ap)
{
    int ret = EINVAL;

    vnode_t vp = ap->a_vp;
    u_long cmd = ap->a_command;
    vfs_context_t context = ap->a_context;

    fuse_trace_printf_vnop();

    if (fuse_isdeadfs_nop(vp)) {
        return EBADF;
    }

    switch (cmd) {
    case FSCTLSETACLSTATE:
        {
            int state;
            mount_t mp;
            struct fuse_data *data;

            if (ap->a_data == NULL) {
                return EINVAL;
            }

            mp = vnode_mount(vp);
            data = fuse_get_mpdata(mp);

            if (!fuse_vfs_context_issuser(context) &&
                !(fuse_match_cred(data->daemoncred,
                                  vfs_context_ucred(context)))) {
                return EPERM;
            }

            state = *(int *)ap->a_data;

            if (state == 0) {
                vfs_clearextendedsecurity(mp);
            } else if (state == 1) {
                vfs_setextendedsecurity(mp);
            } else {
                return EINVAL;
            }

            return 0;
        }
        break;

    case FSCTLALTERVNODEFORINODE:
        /*
         * This is the fsctl() version of the AVFI device ioctl's in
         * fuse_device.c. Since the device ioctl's must be used from
         * within the file system (we don't allow multiple device opens),
         * it's rather painful to test/experiment with them. The fsctl
         * version is easier to use. To simplify things, the "path" in
         * the fsctl() call /must/ be the root of the file system.
         */
        if (!vnode_isvroot(vp)) {
            return EINVAL;
        }

        ret = fuse_internal_ioctl_avfi(vp, context,
                                       (struct fuse_avfi_ioctl *)(ap->a_data));
        break;

    default:
        break;
    }

    return ret;
}


#if M_MACFUSE_ENABLE_UNSUPPORTED

#include "fuse_knote.h"

/*
    struct vnop_kqfilt_add_args {
        struct vnodeop_desc  *a_desc;
        vnode_t               a_vp;
        struct knote         *a_kn;
        struct proc          *p;
        vfs_context_t         a_context;
    };
 */
static int
fuse_vnop_kqfilt_add(struct vnop_kqfilt_add_args *ap)
{
    vnode_t vp = ap->a_vp;
    struct knote *kn = ap->a_kn;

    fuse_trace_printf_vnop();

    switch (kn->kn_filter) {
    case EVFILT_READ:
        if (vnode_isreg(vp)) {
            kn->kn_fop = &fuseread_filtops;
        } else {
            return EINVAL;
        }
        break;

    case EVFILT_WRITE:
        if (vnode_isreg(vp)) {
            kn->kn_fop = &fusewrite_filtops;
        } else {
            return EINVAL;
        }
        break;

    case EVFILT_VNODE:
        kn->kn_fop = &fusevnode_filtops;
        break;

    default:
        return 1;
    }

    kn->kn_hook = (caddr_t)vp;
    kn->kn_hookid = vnode_vid(vp);

    /* lock */
    KNOTE_ATTACH(&VTOFUD(vp)->c_knotes, kn);
    /* unlock */

    return 0;
}


/*
    struct vnop_kqfilt_add_args {
        struct vnodeop_desc  *a_desc;
        vnode_t               a_vp;
        uintptr_t             ident;
        vfs_context_t         a_context;
    };
*/
static int
fuse_vnop_kqfilt_remove(__unused struct vnop_kqfilt_remove_args *ap)
{
    fuse_trace_printf_vnop();

    return ENOTSUP;
}

#endif /* M_MACFUSE_ENABLE_UNSUPPORTED */


/*
    struct vnop_link_args {
        struct vnodeop_desc  *a_desc;
        vnode_t               a_vp;
        vnode_t               a_tdvp;
        struct componentname *a_cnp;
        vfs_context_t         a_context;
    };
*/
static int
fuse_vnop_link(struct vnop_link_args *ap)
{
    vnode_t vp = ap->a_vp;
    vnode_t tdvp = ap->a_tdvp;
    struct componentname *cnp = ap->a_cnp;
    vfs_context_t context = ap->a_context;

    int err;
    struct fuse_dispatcher fdi;
    struct fuse_entry_out *feo;
    struct fuse_link_in    fli;

    fuse_trace_printf_vnop();

    if (fuse_isdeadfs_nop(vp)) {
        panic("MacFUSE: fuse_vnop_link(): called on a dead file system");
    }

    if (vnode_mount(tdvp) != vnode_mount(vp)) {
        return (EXDEV);
    }

    CHECK_BLANKET_DENIAL(vp, context, EPERM);

    fli.oldnodeid = VTOI(vp);

    fdisp_init(&fdi, 0);
    fuse_internal_newentry_makerequest(vnode_mount(tdvp), VTOI(tdvp), cnp,
                                       FUSE_LINK, &fli, sizeof(fli), &fdi,
                                       context);
    if ((err = fdisp_wait_answ(&fdi))) {
        return (err);
    }

    feo = fdi.answ;

    err = fuse_internal_checkentry(feo, vnode_vtype(vp));
    fuse_ticket_drop(fdi.tick);
    fuse_invalidate_attr(tdvp);
    fuse_invalidate_attr(vp);
    
    if (err == 0) {
        FUSE_KNOTE(vp, NOTE_LINK);
        FUSE_KNOTE(tdvp, NOTE_WRITE);
    }

    return (err);
}

#if M_MACFUSE_ENABLE_XATTR
/*
    struct vnop_listxattr_args {
        struct vnodeop_desc *a_desc;
        vnode_t              a_vp;
        uio_t                a_uio;
        size_t              *a_size;
        int                  a_options;
        vfs_context_t        a_context;
    };
*/
static int
fuse_vnop_listxattr(struct vnop_listxattr_args *ap)
/*
    struct vnop_listxattr_args {
        struct vnodeop_desc *a_desc;
        vnode_t              a_vp;
        uio_t                a_uio;
        size_t              *a_size;
        int                  a_options;
        vfs_context_t        a_context;
    };
*/
{
    int err = 0;
    vnode_t vp = ap->a_vp;
    uio_t uio = ap->a_uio;
    vfs_context_t context = ap->a_context;

    struct fuse_dispatcher    fdi;
    struct fuse_getxattr_in  *fgxi;
    struct fuse_getxattr_out *fgxo;
    struct fuse_data         *data;

    fuse_trace_printf_vnop();

    if (fuse_isdeadfs_nop(vp)) {
        return EBADF;
    }

    CHECK_BLANKET_DENIAL(vp, context, ENOENT);

    data = fuse_get_mpdata(vnode_mount(vp));
    if (data->noimplflags & FSESS_NOIMPL(LISTXATTR)) {
        return ENOTSUP;
    }

    fdisp_init(&fdi, sizeof(*fgxi));
    fdisp_make_vp(&fdi, FUSE_LISTXATTR, vp, ap->a_context);
    fgxi = fdi.indata;
    if (uio) {
        fgxi->size = uio_resid(uio);
    } else {
        fgxi->size = 0;
    }

    err = fdisp_wait_answ(&fdi);
    if (err) {
        if (err == ENOSYS) {
            data->noimplflags |= FSESS_NOIMPL(LISTXATTR);
            return ENOTSUP;
        }
        return err;
    }

    if (uio) {
        *ap->a_size = fdi.iosize;
        if (fdi.iosize > uio_resid(uio)) {
            err = ERANGE;
        } else {
            err = uiomove((char *)fdi.answ, fdi.iosize, uio);
        }
    } else {
        fgxo = (struct fuse_getxattr_out *)fdi.answ;
        *ap->a_size = fgxo->size;
    }

    fuse_ticket_drop(fdi.tick);

    return err;
}
#endif /* M_MACFUSE_ENABLE_XATTR */

/*
    struct vnop_lookup_args {
        struct vnodeop_desc  *a_desc;
        vnode_t               a_dvp;
        vnode_t              *a_vpp;
        struct componentname *a_cnp;
        vfs_context_t         a_context;
    };
*/
static int
fuse_vnop_lookup(struct vnop_lookup_args *ap)
{
    struct componentname *cnp = ap->a_cnp;
    int nameiop               = cnp->cn_nameiop;
    int flags                 = cnp->cn_flags;
    int wantparent            = flags & (LOCKPARENT|WANTPARENT);
    int islastcn              = flags & ISLASTCN;
    int isdot                 = FALSE;
    int isdotdot              = FALSE;
    vnode_t dvp               = ap->a_dvp;
    vnode_t *vpp              = ap->a_vpp;
    vfs_context_t context     = ap->a_context;
    mount_t mp                = vnode_mount(dvp);

    fuse_trace_printf_vnop();

    int err                   = 0;
    int lookup_err            = 0;
    vnode_t vp                = NULL;
    vnode_t pdp               = (vnode_t)NULL;
    struct fuse_attr *fattr   = NULL;
    struct fuse_dispatcher fdi;
    enum fuse_opcode op;
    uint64_t nid, parent_nid;
    struct fuse_access_param facp;
    uint64_t size = FUSE_ZERO_SIZE;

    *vpp = NULLVP;

    if (fuse_isdeadfs(dvp)) {
        return ENXIO;
    }

    if (fuse_skip_apple_special_mp(mp, cnp->cn_nameptr, cnp->cn_namelen)) {
        return ENOENT;
    }

    if (vnode_vtype(dvp) != VDIR) {
        return ENOTDIR;
    }

    if (islastcn && vfs_isrdonly(mp) &&
        ((nameiop == DELETE) || (nameiop == RENAME) || (nameiop == CREATE))) {
        return EROFS;
    }

    bzero(&facp, sizeof(facp));
    if (vnode_isvroot(dvp)) { /* early permission check hack */
        if ((err = fuse_internal_access(dvp, KAUTH_VNODE_GENERIC_EXECUTE_BITS,
                                        context, &facp))) {
            return err;
        }
    }

    if (flags & ISDOTDOT) {
        isdotdot = TRUE;
        isdot = FALSE;
    } else if ((cnp->cn_nameptr[0] == '.') && (cnp->cn_namelen == 1)) {
        isdotdot = FALSE;
        isdot = TRUE;
    } 

    if (isdotdot) {
        pdp = VTOFUD(dvp)->parent;
        nid = VTOI(pdp);
        parent_nid = VTOFUD(dvp)->parent_nid;
        fdisp_init(&fdi, 0);
        op = FUSE_GETATTR;
        goto calldaemon;
    } else if (isdot) {
        nid = VTOI(dvp);
        parent_nid = VTOFUD(dvp)->parent_nid;
        fdisp_init(&fdi, 0);
        op = FUSE_GETATTR;
        goto calldaemon;
    } else {
        err = fuse_vncache_lookup(dvp, vpp, cnp);
        switch (err) {

        case -1: /* positive match */
            if (fuse_isnovncache(*vpp)) {
                fuse_vncache_purge(*vpp);
                vnode_put(*vpp);
                *vpp = NULL;
                FUSE_OSAddAtomic(1, (SInt32 *)&fuse_lookup_cache_overrides);
                break; /* pretend it's a miss */
            }
            FUSE_OSAddAtomic(1, (SInt32 *)&fuse_lookup_cache_hits);
            return 0;

        case 0: /* no match in cache (or aged out) */
            FUSE_OSAddAtomic(1, (SInt32 *)&fuse_lookup_cache_misses);
            break;

        case ENOENT: /* negative match */
             /* fall through */
        default:
             return err;
        }
    }

    nid = VTOI(dvp);
    parent_nid = VTOI(dvp);
    fdisp_init(&fdi, cnp->cn_namelen + 1);
    op = FUSE_LOOKUP;

calldaemon:
    fdisp_make(&fdi, op, mp, nid, context);

    if (op == FUSE_LOOKUP) {
        memcpy(fdi.indata, cnp->cn_nameptr, cnp->cn_namelen);
        ((char *)fdi.indata)[cnp->cn_namelen] = '\0';
    }

    lookup_err = fdisp_wait_answ(&fdi);

    if ((op == FUSE_LOOKUP) && !lookup_err) { /* lookup call succeeded */
        nid = ((struct fuse_entry_out *)fdi.answ)->nodeid;
        size = ((struct fuse_entry_out *)fdi.answ)->attr.size;
        if (!nid) {
            lookup_err = ENOENT;
        } else if (nid == FUSE_ROOT_ID) {
            lookup_err = EINVAL;
        }
    }

    /*
     * If we get (lookup_err != 0), that means we didn't find what we were
     * looking for. This can still be OK if we're creating or renaming and
     * are at the end of the pathname.
     */

    if (lookup_err &&
        (!fdi.answ_stat || lookup_err != ENOENT || op != FUSE_LOOKUP)) {
        return (lookup_err);
    }

    if (lookup_err) {

        if ((nameiop == CREATE || nameiop == RENAME) &&
            islastcn /* && directory dvp has not been removed */) {

            if (vfs_isrdonly(mp)) { /* redundant */
                err = EROFS;
                goto out;
            }

#if M_MACFUSE_EXPERIMENTAL_JUNK // THINK_ABOUT_THIS
            if ((err = fuse_internal_access(dvp, VWRITE, context, &facp))) {
                goto out;
            }
#endif
            /* BSD: we could set SAVENAME in cnp->cn_flags here. */

            err = EJUSTRETURN;
            goto out;
        }

        /* Could do negative caching here. */

#if M_MACFUSE_EXPERIMENTAL_JUNK
        if ((cnp->cn_flags & MAKEENTRY) && nameiop != CREATE) {
            DEBUG("inserting NULL into cache\n");
            cxche_enter(dvp, *vpp, cnp);
        }
#endif
        err = ENOENT;
        goto out;

    } else {

        /* !lookup_err */

        if (op == FUSE_GETATTR) {
            fattr = &((struct fuse_attr_out *)fdi.answ)->attr;
        } else {
            fattr = &((struct fuse_entry_out *)fdi.answ)->attr;
        }

        /* DELETE and at the end of the path. */
        if ((nameiop == DELETE) && islastcn) {

            /* Check for write access on directory. */
            facp.xuid = fattr->uid;
            facp.facc_flags |= FACCESS_STICKY;
            err = fuse_internal_access(dvp, KAUTH_VNODE_DELETE_CHILD,
                                       context, &facp);
            facp.facc_flags &= ~FACCESS_XQUERIES;

            if (err) {
                goto out;
            }

            if (nid == VTOI(dvp)) { /* isdot */
                err = vnode_get(dvp);
                if (err == 0) {
                    *vpp = dvp;
                }
                goto out;
            }

            if ((err  = fuse_vget_i(mp,
                                    nid,
                                    context,
                                    dvp,
                                    &vp,
                                    cnp,
                                    IFTOVT(fattr->mode),
                                    size,
                                    VG_NORMAL,
                                    0))) {
                goto out;
            }

            *vpp = vp;
        
            goto out;

        } /* DELETE && islastcn */

        /* RENAME and at the end of the path. */
        if ((nameiop == RENAME) && wantparent && islastcn) {

#if M_MACFUSE_EXPERIMENTAL_JUNK // THINK_ABOUT_THIS
            if ((err = fuse_internal_access(dvp, VWRITE, context, &facp))) {
                goto out;
            }
#endif
            if (nid == VTOI(dvp)) { /* isdot */
                err = EISDIR;
                goto out;
            }

            if ((err  = fuse_vget_i(mp,
                                    nid,
                                    context,
                                    dvp,
                                    &vp,
                                    cnp,
                                    IFTOVT(fattr->mode),
                                    size,
                                    VG_NORMAL,
                                    0))) {
                goto out;
            }

            *vpp = vp;

            /* BSD: we could set SAVENAME in cnp->cn_flags here. */

            goto out;

        } /* RENAME && islastcn */

        if (isdotdot) {
            err = vnode_get(pdp);
            if (err == 0) {
                *vpp = pdp;
            }
        } else if (nid == VTOI(dvp)) { /* isdot */
            err = vnode_get(dvp);
            if (err == 0) {
                *vpp = dvp;
            }
        } else {
            if ((err  = fuse_vget_i(mp,
                                    nid,
                                    context,
                                    dvp,
                                    &vp,
                                    cnp,
                                    IFTOVT(fattr->mode),
                                    size,
                                    VG_NORMAL,
                                    0))) {
                goto out;
            }
            if (vnode_vtype(vp) == VDIR) {
                VTOFUD(vp)->parent = dvp; /* vnode_setparent */
            }
            *vpp = vp;
        }

        if (op == FUSE_GETATTR) {

            /* ATTR_FUDGE_CASE */
            if (vnode_isreg(*vpp) && fuse_isnoubc(vp)) {
                VTOFUD(*vpp)->filesize =
                    ((struct fuse_attr_out *)fdi.answ)->attr.size;
            }

            cache_attrs(*vpp, (struct fuse_attr_out *)fdi.answ);
        } else {

            /* ATTR_FUDGE_CASE */
            if (vnode_isreg(*vpp) && fuse_isnoubc(vp)) {
                VTOFUD(*vpp)->filesize =
                    ((struct fuse_entry_out *)fdi.answ)->attr.size;
            }

            cache_attrs(*vpp, (struct fuse_entry_out *)fdi.answ);
        }

#if M_MACFUSE_EXPERIMENTAL_JUNK
        /* Insert name into cache if appropriate. */
        if (cnp->cn_flags & MAKEENTRY) {
            cxche_enter(dvp, *vpp, cnp);
        }
#endif

    }
out:
    if (!lookup_err) {
        // as the looked up thing was simply found, the cleanup is left for us
        if (err) {
            // though inode found, err exit with no vnode
            if (op == FUSE_LOOKUP)
                fuse_internal_forget_send(vnode_mount(dvp), context, nid, 1, &fdi);
            return (err);
        } else {
            // if (islastcn && flags & ISOPEN) // XXXXXXXXXXXXXXXXXXXXXXXX XXXXXXXXXXXXXXXX
            //if (islastcn)
                //VTOFUD(*vpp)->flags |= FVP_ACCESS_NOOP;

#ifndef NO_EARLY_PERM_CHECK_HACK
            if (!islastcn) {
                /* We have the attributes of the next item
                 * *now*, and it's a fact, and we do not have
                 * to do extra work for it (ie, beg the
                 * daemon), and it neither depends on such
                 * accidental things like attr caching. So the
                 * big idea: check credentials *now*, not at
                 * the beginning of the next call to lookup.
                 *
                 * The first item of the lookup chain (fs root)
                 * won't be checked then here, of course, as
                 * its never "the next". But go and see that
                 * the root is taken care about at the very
                 * beginning of this function.
                 *
                 * Now, given we want to do the access check
                 * this way, one might ask: so then why not do
                 * the access check just after fetching the
                 * inode and its attributes from the daemon?
                 * Why bother with producing the corresponding
                 * vnode at all if something is not OK? We know
                 * what's the deal as soon as we get those
                 * attrs... There is one bit of info though not
                 * given us by the daemon: whether his response
                 * is authorative or not... His response should
                 * be ignored if something is mounted over the
                 * dir in question. But that can be known only
                 * by having the vnode...
                 */
                int tmpvtype = vnode_vtype(*vpp);

                bzero(&facp, sizeof(facp));
                // the early perm check hack
                facp.facc_flags |= FACCESS_VA_VALID;

                //if ((*vpp)->v_type != VDIR && (*vpp)->v_type != VLNK)
                if (tmpvtype != VDIR && tmpvtype != VLNK)
                    err = ENOTDIR;

                if (!err && !vnode_mountedhere(*vpp)) {
                    err = fuse_internal_access(*vpp,
                                               KAUTH_VNODE_GENERIC_EXECUTE_BITS,
                                               context,
                                               &facp);
                }

                if (err) {
                    //if ((*vpp)->v_type == VLNK)
                    if (tmpvtype == VLNK)
                        ; // DEBUG("weird, permission error with a symlink?\n");
                    vnode_put(*vpp);
                    // vput(*vpp);
                    *vpp = NULL;
                }
            }
#endif
        }
            
        fuse_ticket_drop(fdi.tick);
    }

    return (err);
}

/*
    struct vnop_mkdir_args {
        struct vnodeop_desc  *a_desc;
        vnode_t               a_dvp;
        vnode_t              *a_vpp;
        struct componentname *a_cnp;
        struct vnode_attr    *a_vap;
        vfs_context_t         a_context;
    };
*/
static int
fuse_vnop_mkdir(struct vnop_mkdir_args *ap)
{
    int err = 0;
    vnode_t dvp = ap->a_dvp;
    vnode_t *vpp = ap->a_vpp;
    struct componentname *cnp = ap->a_cnp;
    struct vnode_attr *vap = ap->a_vap;
    vfs_context_t context = ap->a_context;

    struct fuse_mkdir_in fmdi;

    fuse_trace_printf_vnop();

    CHECK_BLANKET_DENIAL(dvp, context, EPERM);

    if (fuse_isdeadfs_nop(dvp)) {
        panic("MacFUSE: fuse_vnop_mkdir(): called on a dead file system");
    }

    fmdi.mode = MAKEIMODE(vap->va_type, vap->va_mode);

    err = fuse_internal_newentry(dvp, vpp, cnp, FUSE_MKDIR, &fmdi,
                                 sizeof(fmdi), VDIR, context);

    if (err == 0) {
        FUSE_KNOTE(dvp, NOTE_WRITE | NOTE_LINK);
    }

    return err;
}

/*
    struct vnop_mknod_args {
        struct vnodeop_desc  *a_desc;
        vnode_t               a_dvp;
        vnode_t              *a_vpp;
        struct componentname *a_cnp;
        struct vnode_attr    *a_vap;
        vfs_context_t         a_context;
    };
*/
static int
fuse_vnop_mknod(struct vnop_mknod_args *ap)
{
    int err;
    vnode_t dvp = ap->a_dvp;
    vnode_t *vpp = ap->a_vpp;
    struct componentname *cnp = ap->a_cnp;
    struct vnode_attr *vap = ap->a_vap;
    vfs_context_t context = ap->a_context;

    struct fuse_mknod_in fmni;

    fuse_trace_printf_vnop();

    CHECK_BLANKET_DENIAL(dvp, context, EPERM);

    if (fuse_isdeadfs_nop(dvp)) {
        panic("MacFUSE: fuse_vnop_mknod(): called on a dead file system");
    }

    fmni.mode = MAKEIMODE(vap->va_type, vap->va_mode);
    fmni.rdev = vap->va_rdev;

    err = fuse_internal_newentry(dvp, vpp, cnp, FUSE_MKNOD, &fmni,
                                 sizeof(fmni), vap->va_type, context);

    if (err== 0) {
        FUSE_KNOTE(dvp, NOTE_WRITE);
    }

    return err;
}

/*
    struct vnop_mmap_args {
        struct vnodeop_desc *a_desc;
        vnode_t              a_vp;
        int                  a_fflags;
        vfs_context_t        a_context;
    };
*/
static int
fuse_vnop_mmap(struct vnop_mmap_args *ap)
{
    int err;
    vnode_t vp = ap->a_vp;
    int fflags = ap->a_fflags;
    vfs_context_t context = ap->a_context;
    fufh_type_t fufh_type = fuse_filehandle_xlate_from_mmap(fflags);
    struct fuse_vnode_data *fvdat = VTOFUD(vp);
    struct fuse_filehandle *fufh = NULL;

    fuse_trace_printf_vnop();

    if (fuse_isdeadfs_nop(vp)) {
        panic("MacFUSE: fuse_vnop_mmap(): called on a dead file system");
    }

    CHECK_BLANKET_DENIAL(vp, context, ENOENT);

    if (fufh_type == FUFH_INVALID) { // nothing to do
        return 0;
    }

    /* XXX: For PROT_WRITE, we should only care if file is mapped MAP_SHARED. */

    fufh = &(fvdat->fufh[fufh_type]);

    if (fufh->fufh_flags & FUFH_VALID) {
        FUSE_OSAddAtomic(1, (SInt32 *)&fuse_fh_reuse_count);
        goto out;
    }

    err = fuse_filehandle_get(vp, ap->a_context, fufh_type, 0 /* mode */);
    if (err) {
        IOLog("MacFUSE: filehandle_get failed in mmap (type=%d, err=%d)\n",
              fufh_type, err);
        return err;
    }

out:
    fufh->fufh_flags |= FUFH_MAPPED;

    return 0;
}

/*
    struct vnop_mnomap_args {
        struct vnodeop_desc *a_desc;
        vnode_t              a_vp;
        vfs_context_t        a_context;
    };
*/
static int
fuse_vnop_mnomap(struct vnop_mnomap_args *ap)
{
    int type;
    vnode_t vp = ap->a_vp;
    struct fuse_vnode_data *fvdat = VTOFUD(vp);
    struct fuse_filehandle *fufh = NULL;

    fuse_trace_printf_vnop();

    if (fuse_isdeadfs_nop(vp)) {
        return 0;
    }

#if M_MACFUSE_EXPERIMENTAL_JUNK
    /*
     * sync() is not going to help here. What behavior do we want?
     */

    ubc_sync_range(vp, (off_t)0, ubc_getsize(vp), UBC_PUSHDIRTY);
#endif

    for (type = 0; type < FUFH_MAXTYPE; type++) {
        fufh = &(fvdat->fufh[type]);
        if ((fufh->fufh_flags & FUFH_VALID) &&
            (fufh->fufh_flags & FUFH_MAPPED)) {
            fufh->fufh_flags &= ~FUFH_MAPPED;
            if (fufh->open_count == 0) {
                (void)fuse_filehandle_put(vp, ap->a_context, type, 0);
            }
        }
    }

    return 0;
}

/*
    struct vnop_offtoblk_args {
        struct vnodeop_desc *a_desc;
        vnode_t              a_vp;
        off_t                a_offset;
        daddr64_t           *a_lblkno;
    };
*/
static int
fuse_vnop_offtoblk(struct vnop_offtoblk_args *ap)
{
    vnode_t    vp;
    off_t      offset;
    daddr64_t *lblknoPtr;
    struct fuse_data *data;

    fuse_trace_printf_vnop();

    vp        = ap->a_vp;
    offset    = ap->a_offset;
    lblknoPtr = ap->a_lblkno;

    if (fuse_isdeadfs_nop(vp)) {
        return EIO;
    }

    data = fuse_get_mpdata(vnode_mount(vp));

    *lblknoPtr = offset / data->blocksize;

    return 0;
}

/*
    struct vnop_open_args {
        struct vnodeop_desc *a_desc;
        vnode_t              a_vp;
        int                  a_mode;
        vfs_context_t        a_context;
    };
*/
static int
fuse_vnop_open(struct vnop_open_args *ap)
{
    vnode_t                 vp;
    vfs_context_t           context;
    fufh_type_t             fufh_type;
    struct fuse_vnode_data *fvdat;
    struct fuse_filehandle *fufh = NULL;
    struct fuse_filehandle *fufh_rw = NULL;

    int error, isdir = 0, mode;

    fuse_trace_printf_vnop();

    vp = ap->a_vp;

    if (fuse_isdeadfs(vp)) {
        return ENXIO;
    }

    mode    = ap->a_mode;
    context = ap->a_context;

    CHECK_BLANKET_DENIAL(vp, context, ENOENT);

    fvdat   = VTOFUD(vp);
    if (vnode_vtype(vp) == VDIR) {
        isdir = 1;
    }

    if (isdir) {
        fufh_type = FUFH_RDONLY;
    } else {
        debug_printf("fuse_open(VREG)\n");
        fufh_type = fuse_filehandle_xlate_from_fflags(mode);
    }

    fufh = &(fvdat->fufh[fufh_type]);

    if (!isdir && (fvdat->flag & FN_CREATING)) {

        fuse_lck_mtx_lock(fvdat->createlock);

        if (fvdat->flag & FN_CREATING) { // check again
            if (fvdat->creator == current_thread()) {

                /*
                 * For testing the race condition we want to prevent here,
                 * try something like the following:
                 *
                 *     int dummyctr = 0;
                 *
                 *     for (; dummyctr < 2048000000; dummyctr++);
                 */

                fufh_rw = &(fvdat->fufh[FUFH_RDWR]);

                fufh->fufh_flags |= FUFH_VALID;
                fufh->fufh_flags &= ~FUFH_MAPPED;
                fufh->open_count = 1;
                fufh->open_flags = fufh_rw->open_flags;
                fufh->type = fufh_type;
                fufh->fh_id = fufh_rw->fh_id;
                debug_printf("creator picked up stashed handle, moved to %d\n",
                             fufh_type);
                
                fvdat->flag &= ~FN_CREATING;

                fuse_lck_mtx_unlock(fvdat->createlock);
                wakeup((caddr_t)fvdat->creator); // wake up all
                goto ok; /* return 0 */
            } else {
                debug_printf("contender going to sleep\n");
                error = msleep(fvdat->creator, fvdat->createlock,
                               PDROP | PINOD | PCATCH, "fuse_open", 0);
                /*
                 * msleep will drop the mutex. since we have PDROP specified,
                 * it will NOT regrab the mutex when it returns.
                 */
                debug_printf("contender awake (error = %d)\n", error);

                if (error) {
                    /*
                     * Since we specified PCATCH above, we'll be woken up in
                     * case a signal arrives. The value of error could be
                     * EINTR or ERESTART.
                     */
                    return error;
                }
            }
        } else {
            fuse_lck_mtx_unlock(fvdat->createlock);
            /* Can proceed from here. */
        }
    }

    if (fufh->fufh_flags & FUFH_VALID) {
        fufh->open_count++;
        FUSE_OSAddAtomic(1, (SInt32 *)&fuse_fh_reuse_count);
        goto ok; /* return 0 */
    }

    error = fuse_filehandle_get(vp, context, fufh_type, mode);
    if (error) {
        if (error == ENOENT) {
            cache_purge(vp);
        }
        return error;
    }

ok:
    /*
     * Doing this here because when a vnode goes inactive, things like
     * no-cache and no-readahead are cleared by the kernel.
     */

    if ((fufh->fuse_open_flags & FOPEN_DIRECT_IO) || (fuse_isdirectio(vp))) {
        /* 
         * direct_io for a vnode implies:
         * - no ubc for the vnode
         * - no readahead for the vnode
         * - nosyncwrites disabled FOR THE ENTIRE MOUNT
         * - no vncache for the vnode (handled in lookup)
         */
        ubc_sync_range(vp, (off_t)0, ubc_getsize(vp),
                       UBC_PUSHALL | UBC_INVALIDATE);
        vnode_setnocache(vp);
        vnode_setnoreadahead(vp);
        fuse_clearnosyncwrites_mp(vnode_mount(vp));
        fvdat->flag |= FN_DIRECT_IO;
        goto out;
    }

    if (fuse_isnoreadahead(vp)) {
        vnode_setnoreadahead(vp);
    }

    if (fuse_isnoubc(vp)) {
        vnode_setnocache(vp);
    }

out:
    return 0;
}

/*
    struct vnop_pagein_args {
        struct vnodeop_desc *a_desc;
        vnode_t              a_vp;
        upl_t                a_pl;
        vm_offset_t          a_pl_offset;
        off_t                a_f_offset;
        size_t               a_size;
        int                  a_flags;
        vfs_context_t        a_context;
    };
*/
static int
fuse_vnop_pagein(struct vnop_pagein_args *ap)
{
    int            err;
    vnode_t        vp;
    upl_t          pl;
    vm_offset_t    pl_offset;
    off_t          f_offset;
    size_t         size;
    int            flags;
    vfs_context_t  context;
    struct fuse_vnode_data *fvdat;

    fuse_trace_printf_vnop();

    vp        = ap->a_vp;
    pl        = ap->a_pl;
    pl_offset = ap->a_pl_offset;
    f_offset  = ap->a_f_offset;
    size      = ap->a_size;
    flags     = ap->a_flags;
    context   = ap->a_context;

    if (fuse_isdeadfs_nop(vp)) {
        if (!(flags & UPL_NOCOMMIT)) {
            ubc_upl_abort_range(pl, pl_offset, size,
                                UPL_ABORT_FREE_ON_EMPTY | UPL_ABORT_ERROR);
        }
        return ENOTSUP;
    }

    fvdat = VTOFUD(vp);
    if (!fvdat) {
        return EIO;
    }

    err = cluster_pagein(vp, pl, pl_offset, f_offset, size,
                         fvdat->filesize, flags);

    return err;
}

/*
    struct vnop_pageout_args {
        struct vnodeop_desc *a_desc;
        vnode_t              a_vp;
        upl_t                a_pl;
        vm_offset_t          a_pl_offset;
        off_t                a_f_offset;
        size_t               a_size;
        int                  a_flags;
        vfs_context_t        a_context;
    };
*/
static int
fuse_vnop_pageout(struct vnop_pageout_args *ap)
{
    vnode_t vp = ap->a_vp;
    struct fuse_vnode_data *fvdat = VTOFUD(vp);
    int error;

    fuse_trace_printf_vnop();

    if (fuse_isdeadfs_nop(vp)) {
        if (!(ap->a_flags & UPL_NOCOMMIT)) {
            ubc_upl_abort_range(ap->a_pl, ap->a_pl_offset, ap->a_size,
                                UPL_ABORT_FREE_ON_EMPTY | UPL_ABORT_ERROR);
        }
        return ENOTSUP;
    }

    error = cluster_pageout(vp, ap->a_pl, ap->a_pl_offset, ap->a_f_offset,
                            ap->a_size, (off_t)fvdat->filesize, ap->a_flags);

    return error;
}

/*
    struct vnop_pathconf_args {
        struct vnodeop_desc *a_desc;
        vnode_t              a_vp;
        int                  a_name;
        register_t          *a_retval;
        vfs_context_t        a_context;
    };
*/
static int
fuse_vnop_pathconf(struct vnop_pathconf_args *ap)
{
    int            err;
    vnode_t        vp;
    int            name;
    register_t    *retvalPtr;
    vfs_context_t  context;

    fuse_trace_printf_vnop();

    vp        = ap->a_vp;
    name      = ap->a_name;
    retvalPtr = ap->a_retval;
    context   = ap->a_context;

    if (fuse_isdeadfs_nop(vp)) {
        return EBADF;
    }

    CHECK_BLANKET_DENIAL(vp, context, ENOENT);

    err = 0;
    switch (name) {
        case _PC_LINK_MAX:
            *retvalPtr = FUSE_LINK_MAX;
            break;
        case _PC_NAME_MAX:
            *retvalPtr = __DARWIN_MAXNAMLEN;
            break;
        case _PC_PATH_MAX:
            *retvalPtr = MAXPATHLEN;
            break;
        case _PC_PIPE_BUF:
            *retvalPtr = PIPE_BUF;
            break;
        case _PC_CHOWN_RESTRICTED:
            *retvalPtr = 1;
            break;
        case _PC_NO_TRUNC:
            *retvalPtr = 0;
            break;
        case _PC_NAME_CHARS_MAX:
            *retvalPtr = 255;   // *** what's this about?
            break;
        case _PC_CASE_SENSITIVE:
            *retvalPtr = 1;
            break;
        case _PC_CASE_PRESERVING:
            *retvalPtr = 1;
            break;

        /*
         * _PC_EXTENDED_SECURITY_NP and _PC_AUTH_OPAQUE_NP are handled
         * by the VFS.
         */

        // The following are terminal device stuff that we don't support:

        case _PC_MAX_CANON:
        case _PC_MAX_INPUT:
        case _PC_VDISABLE:
        default:
            err = EINVAL;
            break;
    }

    return err;
}

/*
    struct vnop_read_args {
        struct vnodeop_desc *a_desc;
        vnode_t              a_vp;
        struct uio          *a_uio;
        int                  a_ioflag;
        vfs_context_t        a_context;
    };
*/
static int
fuse_vnop_read(struct vnop_read_args *ap)
{
    vnode_t         vp;
    uio_t           uio;
    int             ioflag;
    vfs_context_t   context;
    off_t           orig_resid;
    off_t           orig_offset;
    int             err = EIO;

    struct fuse_vnode_data *fvdat;
    struct fuse_data       *data;

    /*
     * XXX: Locking
     *
     * lock_shared(truncatelock)
     * call the cluster layer (note that we are always block-aligned)
     * lock(nodelock)
     * do cleanup 
     * unlock(nodelock)
     * unlock(truncatelock)
     */

    fuse_trace_printf_vnop();

    vp           = ap->a_vp;
    uio          = ap->a_uio;
    ioflag       = ap->a_ioflag;
    context      = ap->a_context;

    if (fuse_isdeadfs_nop(vp)) {
        if (vnode_vtype(vp) != VCHR) {
            return EIO;
        } else {
            return 0;
        }
    }

    if (!vnode_isreg(vp)) {
        if (vnode_isdir(vp)) {
            return EISDIR;
        } else {
            return EPERM;
        }
    }

    if (uio_offset(uio) < 0) {
        return EINVAL;
    }

    /*
     * if (uio_offset(uio) > SOME_MAXIMUM_SIZE) {
     *     return 0;
     * }
     */

    orig_resid = uio_resid(uio);
    if (orig_resid == 0) {
        return 0;
    }

    orig_offset = uio_offset(uio);
    if (orig_offset < 0) {
        return EINVAL;
    }

    fvdat = VTOFUD(vp);
    if (!fvdat) {
        return EINVAL;
    }

    /* Protect against size change here. */

    data = fuse_get_mpdata(vnode_mount(vp));

    if (!fuse_isdirectio(vp)) {
        return cluster_read(vp, uio, fvdat->filesize, 0);
    }

    /* direct_io */
    {
        fufh_type_t             fufh_type = FUFH_RDONLY;
        struct fuse_dispatcher  fdi;
        struct fuse_filehandle *fufh = NULL;
        struct fuse_read_in    *fri = NULL;
        off_t                   rounded_iolength;

        fufh = &(fvdat->fufh[fufh_type]);
        if (!(fufh->fufh_flags & FUFH_VALID)) {
            fufh_type = FUFH_RDWR;
            fufh = &(fvdat->fufh[fufh_type]);
            if (!(fufh->fufh_flags & FUFH_VALID)) {
                fufh = NULL;
            } else {
                debug_printf("read falling back to FUFH_RDWR ... OK\n");
            }
        }

        if (!fufh) {
            debug_printf("READ: no fufh: failing direct I/O\n");
            return EIO;
        } else {
           debug_printf("READ: using existing fufh of type %d\n", fufh_type);
        }

        rounded_iolength = (off_t)round_page_64(uio_offset(uio) +
                                                uio_resid(uio));
        fdisp_init(&fdi, 0);

        while (uio_resid(uio) > 0) {
            fdi.iosize = sizeof(*fri);
            fdisp_make_vp(&fdi, FUSE_READ, vp, context);
            fri = fdi.indata;
            fri->fh = fufh->fh_id;
            fri->offset = uio_offset(uio);
            fri->size = min(uio_resid(uio), data->iosize);

            if ((err = fdisp_wait_answ(&fdi))) {
                return err;
            }

            if ((err = uiomove(fdi.answ, min(fri->size, fdi.iosize), uio))) {
                break;
            }

            if (fdi.iosize < fri->size) {
                err = -1;
                break;
            }
        }

        fuse_ticket_drop(fdi.tick);

    } /* direct_io */

    return ((err == -1) ? 0 : err);
}

/*
    struct vnop_readdir_args {
        struct vnodeop_desc *a_desc;
        vnode_t              a_vp;
        struct uio          *a_uio;
        int                  a_flags;
        int                 *a_eofflag;
        int                 *a_numdirent;
        vfs_context_t        a_context;
    };
*/
static int
fuse_vnop_readdir(struct vnop_readdir_args *ap)
{
    int           freefufh = 0;
    errno_t       err = 0;
    vnode_t       vp;
    struct uio   *uio;
    int           flags;
    int          *eofflagPtr;
    int          *numdirentPtr;
    vfs_context_t context;

    struct fuse_iov cookediov;
    struct fuse_filehandle *fufh = NULL;
    struct fuse_vnode_data *fvdat;

    fuse_trace_printf_vnop();

    vp           = ap->a_vp;
    uio          = ap->a_uio;
    flags        = ap->a_flags;
    eofflagPtr   = ap->a_eofflag;
    numdirentPtr = ap->a_numdirent;
    context      = ap->a_context;

    if (fuse_isdeadfs_nop(vp)) {
        return EBADF;
    }

    CHECK_BLANKET_DENIAL(vp, context, EPERM);

    /* Sanity check the uio data. */
    if ((uio_iovcnt(uio) > 1) ||
        (uio_resid(uio) < (int)sizeof(struct dirent))) {
        return (EINVAL);
    }

#if M_MACFUSE_EXPERIMENTAL_JUNK
    struct get_filehandle_param gefhp;

    debug_printf("ap=%p\n", ap);

    bzero(&gefhp, sizeof(gefhp));
    gefhp.opcode = FUSE_OPENDIR;
    if ((err = fuse_filehandle_get(vp, context, FREAD, &fufh, &gefhp))) {
        return (err);
    }
#endif

    fvdat = VTOFUD(vp);

    fufh = &(fvdat->fufh[FUFH_RDONLY]);
    if (!(fufh->fufh_flags & FUFH_VALID)) {
        err = fuse_filehandle_get(vp, context, FUFH_RDONLY, 0 /* mode */);
        if (err) {
            return err;
        }
        freefufh = 1;
    } else {
        FUSE_OSAddAtomic(1, (SInt32 *)&fuse_fh_reuse_count);
    }

#define DIRCOOKEDSIZE FUSE_DIRENT_ALIGN(FUSE_NAME_OFFSET + MAXNAMLEN + 1)
    fiov_init(&cookediov, DIRCOOKEDSIZE);

#if M_MACFUSE_EXPERIMENTAL_JUNK
    bzero(&fioda, sizeof(fioda));
    fioda.vp = vp;
    fioda.fufh = fufh;
    fioda.uio = uio;
    fioda.context = context;
    fioda.opcode = FUSE_READDIR;
    fioda.buffeater = fuse_dir_buffeater;
    fioda.param = &cookediov;
#endif

    err = fuse_internal_readdir(vp, uio, context, fufh, &cookediov);

    fiov_teardown(&cookediov);
    // fufh->useco--;
    if (freefufh) {
        fufh->open_count--;
        (void)fuse_filehandle_put(vp, context, FUFH_RDONLY, 0);
    }
    fuse_invalidate_attr(vp);

    return err;
}

/*
    struct vnop_readlink_args {
        struct vnodeop_desc *a_desc;
        vnode_t              a_vp;
        struct uio          *a_uio;
        vfs_context_t        a_context;
    };
*/
static int
fuse_vnop_readlink(struct vnop_readlink_args *ap)
{
    int err;
    vnode_t vp;
    uio_t uio;
    vfs_context_t context;
    struct fuse_dispatcher fdi;

    fuse_trace_printf_vnop();

    vp = ap->a_vp;
    uio = ap->a_uio;
    context = ap->a_context;

    if (fuse_isdeadfs_nop(vp)) {
        return EBADF;
    }

    CHECK_BLANKET_DENIAL(vp, context, ENOENT);

    if (vnode_vtype(vp) != VLNK) {
        return EINVAL;
    }

    if ((err = fdisp_simple_putget_vp(&fdi, FUSE_READLINK, vp, context))) {
        return err;
    }

    if (((char *)fdi.answ)[0] == '/' &&
        fuse_get_mpdata(vnode_mount(vp))->dataflags & FSESS_JAIL_SYMLINKS) {
            char *mpth = vfs_statfs(vnode_mount(vp))->f_mntonname;
            err = uiomove(mpth, strlen(mpth), uio);
    }

    if (!err) {
        err = uiomove(fdi.answ, fdi.iosize, uio);
    }

    fuse_ticket_drop(fdi.tick);
    fuse_invalidate_attr(vp);

    return (err);
}

/*
    struct vnop_reclaim_args {
        struct vnodeop_desc *a_desc;
        vnode_t              a_vp;
        vfs_context_t        a_context;
    };
*/
static int
fuse_vnop_reclaim(struct vnop_reclaim_args *ap)
{
    int type;
    vnode_t vp = ap->a_vp;
    HNodeRef hn;
    struct fuse_vnode_data *fvdat = VTOFUD(vp);
    struct fuse_filehandle *fufh = NULL;

    fuse_trace_printf_vnop();

    if (!fvdat) {
        panic("MacFUSE: no vnode data during recycling");
    }

    for (type = 0; type < FUFH_MAXTYPE; type++) {
        fufh = &(fvdat->fufh[type]);
        if (fufh->fufh_flags & FUFH_VALID) {
            int open_count = fufh->open_count;
            fufh->fufh_flags &= ~FUFH_MAPPED;
            fufh->open_count = 0;
            if ((fufh->fufh_flags & FUFH_STRATEGY) ||
                vfs_isforce(vnode_mount(vp))) {
                (void)fuse_filehandle_put(vp, ap->a_context, type, 0);
            } else {
                /*
                 * This is not a forced unmount. So why is the vnode being
                 * reclaimed if a fufh is valid? Well, unless we are dead.
                 */
                if (!fuse_isdeadfs(vp) && !(fvdat->flag & FN_REVOKING)) {
                    /*
                     * This needs to be figured out. Looks like we can get
                     * here if there's a race between open and vflush (latter
                     * happening because of an unmount). This leads to the
                     * following behavior:
                     * 
                     * open
                     * close
                     * inactive
                     * open
                     * reclaim <-- ?????
                     *
                    panic("MacFUSE: vnode reclaimed with valid fufh "
                          "(type=%d, vtype=%d, open_count=%d)",
                          type, vnode_vtype(vp), open_count);
                     */
                    IOLog("MacFUSE: vnode reclaimed with valid fufh "
                          "(type=%d, vtype=%d, open_count=%d, busy=%d)\n",
                          type, vnode_vtype(vp), open_count,
                          vnode_isinuse(vp, 0));
                }
            }
        }
    }

    if ((!fuse_isdeadfs(vp)) && (fvdat->nlookup)) {
        struct fuse_dispatcher fdi;
        fdi.tick = NULL;
        fuse_internal_forget_send(vnode_mount(vp), ap->a_context, VTOI(vp),
                                  fvdat->nlookup, &fdi);
    }

    fuse_vncache_purge(vp);

    hn = HNodeFromVNode(vp);
    if (HNodeDetachVNode(hn, vp)) {
        FSNodeScrub(fvdat);
        HNodeScrubDone(hn);
        FUSE_OSAddAtomic(-1, (SInt32 *)&fuse_vnodes_current);
    }

    return 0;
}

/*
    struct vnop_remove_args {
        struct vnodeop_desc  *a_desc;
        vnode_t               a_dvp;
        vnode_t               a_vp;
        struct componentname *a_cnp;
        int                   a_flags;
        vfs_context_t         a_context;
    };
*/
static int
fuse_vnop_remove(struct vnop_remove_args *ap)
{
    int err;

    vnode_t               dvp     = ap->a_dvp;
    vnode_t               vp      = ap->a_vp;
    struct componentname *cnp     = ap->a_cnp;
    int                   flags   = ap->a_flags;
    vfs_context_t         context = ap->a_context;

    fuse_trace_printf_vnop();

    if (fuse_isdeadfs_nop(vp)) {
        panic("MacFUSE: fuse_vnop_remove(): called on a dead file system");
    }

    CHECK_BLANKET_DENIAL(vp, context, ENOENT);

    if (vnode_isdir(vp)) {
        return EPERM;
    }

    /* Check for Carbon delete semantics. */
    if ((flags & VNODE_REMOVE_NODELETEBUSY) && vnode_isinuse(vp, 0)) {
        return EBUSY;
    }

    fuse_vncache_purge(vp);

    err = fuse_internal_remove(dvp, vp, cnp, FUSE_UNLINK, context);

    if (err == 0) {
        FUSE_KNOTE(vp, NOTE_DELETE);
        FUSE_KNOTE(dvp, NOTE_WRITE);
    }

    return err;
}

#if M_MACFUSE_ENABLE_XATTR
/*
    struct vnop_removexattr_args {
        struct vnodeop_desc *a_desc;
        vnode_t              a_vp;
        char                *a_name;
        int                  a_options;
        vfs_context_t        a_context;
    };
*/
static int
fuse_vnop_removexattr(struct vnop_removexattr_args *ap)
{
    int err = 0;
    int namelen;
    struct fuse_dispatcher fdi;
    struct fuse_data      *data;

    vnode_t vp = ap->a_vp;
    vfs_context_t context = ap->a_context;

    fuse_trace_printf_vnop();

    if (fuse_isdeadfs_nop(vp)) {
        return EBADF;
    }

    CHECK_BLANKET_DENIAL(vp, context, ENOENT);

    data = fuse_get_mpdata(vnode_mount(vp));
    if (data->noimplflags & FSESS_NOIMPL(REMOVEXATTR)) {
        return ENOTSUP;
    }

    if (ap->a_name == NULL || ap->a_name[0] == '\0') {
        return (EINVAL);  /* invalid name */
    }

    if (fuse_is_shortcircuit_xattr(ap->a_name)) {
        return ENOTSUP;
    }

    namelen = strlen(ap->a_name);

    fdisp_init(&fdi, namelen + 1);
    fdisp_make_vp(&fdi, FUSE_REMOVEXATTR, vp, ap->a_context);

    memcpy((char *)fdi.indata, ap->a_name, namelen);
    ((char *)fdi.indata)[namelen] = '\0';

    err = fdisp_wait_answ(&fdi);
    if (!err) {
        fuse_ticket_drop(fdi.tick);
    } else {
        if (err == ENOSYS) {
            data->noimplflags |= FSESS_NOIMPL(REMOVEXATTR);
            return ENOTSUP;
        }
    }

    return err;
}
#endif /* M_MACFUSE_ENABLE_XATTR */

/*
    struct vnop_rename_args {
        struct vnodeop_desc  *a_desc;
        vnode_t               a_fdvp;
        vnode_t               a_fvp;
        struct componentname *a_fcnp;
        vnode_t               a_tdvp;
        vnode_t               a_tvp;
        struct componentname *a_tcnp;
        vfs_context_t         a_context;
    };
*/
static int
fuse_vnop_rename(struct vnop_rename_args *ap)
{
    int err = 0;

    vnode_t fdvp               = ap->a_fdvp;
    vnode_t fvp                = ap->a_fvp;
    struct componentname *fcnp = ap->a_fcnp;
    vnode_t tdvp               = ap->a_tdvp;
    vnode_t tvp                = ap->a_tvp;
    struct componentname *tcnp = ap->a_tcnp;
    vfs_context_t context      = ap->a_context;

    fuse_trace_printf_vnop();

    if (fuse_isdeadfs_nop(fdvp)) {
        panic("MacFUSE: fuse_vnop_rename(): called on a dead file system");
    }

    CHECK_BLANKET_DENIAL(fdvp, context, ENOENT);

    fuse_vncache_purge(fvp);

    err = fuse_internal_rename(fdvp, fvp, fcnp, tdvp, tvp, tcnp, ap->a_context);

    if (err == 0) {
        FUSE_KNOTE(fdvp, NOTE_WRITE);
        if (tdvp != fdvp) {
            FUSE_KNOTE(tdvp, NOTE_WRITE);
        }
    }

    if (tvp != NULLVP) {
        if (tvp != fvp) {
            fuse_vncache_purge(tvp);
            FUSE_KNOTE(tvp, NOTE_DELETE);
        }
        if (err == 0) {

            /*
             * If we want the file to just "disappear" from the standpoint
             * of those who might have it open, we can do a revoke/recycle
             * here. Otherwise, don't do anything. Only doing a recycle will
             * make our fufh-checking code in reclaim unhappy, leading us to
             * proactively panic.
             */

            /*
             * 1. revoke
             * 2. recycle
             */
        }
    }

    if (vnode_isdir(fvp)) {
        if ((tvp != NULLVP) && vnode_isdir(tvp)) {
            fuse_vncache_purge(tdvp);
        }
        fuse_vncache_purge(fdvp);
    }

    if (err == 0) {
        FUSE_KNOTE(fvp, NOTE_RENAME);
    }

    return err;
}

/*
 *  struct vnop_revoke_args {
 *      struct vnodeop_desc  *a_desc;
 *      vnode_t               a_vp;
 *      int                   a_flags;
 *      vfs_context_t         a_context;
 *  };
 */
static int
fuse_vnop_revoke(struct vnop_revoke_args *ap)
{
    vnode_t vp = ap->a_vp;
    vfs_context_t context = ap->a_context;

    fuse_trace_printf_vnop();

    CHECK_BLANKET_DENIAL(vp, context, ENOENT);

    return fuse_internal_revoke(ap->a_vp, ap->a_flags, ap->a_context);
}

/*
    struct vnop_rmdir_args {
        struct vnodeop_desc  *a_desc;
        vnode_t               a_dvp;
        vnode_t               a_vp;
        struct componentname *a_cnp;
        vfs_context_t         a_context;
    };
*/
static int
fuse_vnop_rmdir(struct vnop_rmdir_args *ap)
{
    int err;
    vnode_t dvp = ap->a_dvp;
    vnode_t vp = ap->a_vp;
    vfs_context_t context = ap->a_context;

    fuse_trace_printf_vnop();

    if (fuse_isdeadfs_nop(vp)) {
        panic("MacFUSE: fuse_vnop_rmdir(): called on a dead file system");
    }

    CHECK_BLANKET_DENIAL(vp, context, ENOENT);

    if (VTOFUD(vp) == VTOFUD(dvp)) {
        return EINVAL;
    }

    fuse_vncache_purge(vp);

    err = fuse_internal_remove(dvp, vp, ap->a_cnp, FUSE_RMDIR, ap->a_context);

    if (err == 0) {
        FUSE_KNOTE(dvp, NOTE_WRITE | NOTE_LINK);
        FUSE_KNOTE(vp, NOTE_DELETE);
    }

    return err;
}

/*
struct vnop_select_args {
    struct vnodeop_desc *a_desc;
    vnode_t              a_vp;
    int                  a_which;
    int                  a_fflags;
    void                *a_wql;
    vfs_context_t        a_context;
};
*/
static int
fuse_vnop_select(__unused struct vnop_select_args *ap)
{
    fuse_trace_printf_vnop();
    return (1);
}

/*
    struct vnop_setattr_args {
        struct vnodeop_desc *a_desc;
        vnode_t              a_vp;
        struct vnode_attr   *a_vap;
        vfs_context_t        a_context;
    };
*/
static int
fuse_vnop_setattr(struct vnop_setattr_args *ap)
{
    vnode_t vp = ap->a_vp;
    struct vnode_attr *vap = ap->a_vap;
    vfs_context_t context = ap->a_context;

    struct fuse_dispatcher   fdi;
    struct fuse_setattr_in  *fsai;
    struct fuse_access_param facp;

    int err = 0;
    int action;
    enum vtype vtyp;
    uid_t nuid;
    gid_t ngid;
    int sizechanged = 0;
    uint64_t newsize = 0;

    /*
     * XXX: Locking
     *
     * We need to worry about the file size changing in setattr. If the call
     * is indeed altering the size, then:
     *
     * lock_exclusive(truncatelock)
     * lock(nodelock)
     * set the new size
     * unlock(nodelock)
     * adjust ubc
     * lock(nodelock)
     * do cleanup
     * unlock(nodelock)
     * unlock(truncatelock)
     * ...
     */
 
    fuse_trace_printf_vnop();

    if (fuse_isdeadfs_nop(vp)) {
        return EBADF;
    }

    CHECK_BLANKET_DENIAL(vp, context, ENOENT);

#define VAP_ENSURE_EQUALITY(lhs, rhs, message) \
    if ((lhs) != (rhs)) { \
        debug_printf(message); \
        return (EINVAL); \
    }

#if M_MACFUSE_EXPERIMENTAL_JUNK
    if ((vap->va_type != VNON) || (vap->va_nlink != VNOVAL) ||
        (vap->va_fsid != VNOVAL) || (vap->va_fileid != VNOVAL) ||
        (vap->va_iosize != VNOVAL) || (vap->va_rdev != VNOVAL) ||
        ((int)vap->va_total_size != VNOVAL) || (vap->va_gen != VNOVAL) ||
         (vap->va_flags != VNOVAL))
        return (EINVAL);
#endif

    fdisp_init(&fdi, sizeof(*fsai));
    fdisp_make_vp(&fdi, FUSE_SETATTR, vp, context);
    fsai = fdi.indata;

    bzero(&facp, sizeof(facp));

#define FUSEATTR(x) x

    facp.xuid = vap->va_uid;
    facp.xgid = vap->va_gid;

    nuid = VATTR_IS_ACTIVE(vap, va_uid) ? vap->va_uid : (uid_t)VNOVAL;
    ngid = VATTR_IS_ACTIVE(vap, va_gid) ? vap->va_gid : (gid_t)VNOVAL;

    if (nuid != (uid_t)VNOVAL) {
        facp.facc_flags |= FACCESS_CHOWN;
        fsai->FUSEATTR(uid) = nuid;
        fsai->valid |= FATTR_UID;
    }

    if (ngid != (gid_t)VNOVAL) {
        facp.facc_flags |= FACCESS_CHOWN;
        fsai->FUSEATTR(gid) = ngid;
        fsai->valid |= FATTR_GID;
    }

    VATTR_SET_SUPPORTED(vap, va_uid);
    VATTR_SET_SUPPORTED(vap, va_gid);

    if (VATTR_IS_ACTIVE(vap, va_data_size)) {

        struct fuse_filehandle *fufh = NULL;
        fufh_type_t fufh_type = FUFH_WRONLY;
        struct fuse_vnode_data *fvdat = VTOFUD(vp);

        // Truncate to a new value.
        fsai->FUSEATTR(size) = vap->va_data_size;
        sizechanged = 1;
        newsize = vap->va_data_size;
        fsai->valid |= FATTR_SIZE;      

        fufh = &(fvdat->fufh[fufh_type]);
        if (!(fufh->fufh_flags & FUFH_VALID)) {
            fufh_type = FUFH_RDWR;
            fufh = &(fvdat->fufh[fufh_type]);
            if (!(fufh->fufh_flags & FUFH_VALID)) {
                fufh = NULL;
            }
        }

        if (fufh) {
            fsai->fh = fufh->fh_id;
            fsai->valid |= FATTR_FH;
        }
    }
    VATTR_SET_SUPPORTED(vap, va_data_size);

    /*
     * Possible timestamps:
     *
     * Mac OS X                                          Linux
     *  
     * va_access_time    last access time                atime
     * va_backup_time    last backup time                -
     * va_change_time    last metadata change time       ctime*
     * va_create_time    creation time                   ctime*
     * va_modify_time    last data modification time     mtime
     *
     * FUSE has knowledge of atime, ctime, and mtime. A setattr call to
     * the daemon can take atime and mtime.
     */

    if (VATTR_IS_ACTIVE(vap, va_access_time)) {
        fsai->FUSEATTR(atime) = vap->va_access_time.tv_sec;
        fsai->FUSEATTR(atimensec) = vap->va_access_time.tv_nsec;
        fsai->valid |=  FATTR_ATIME;
    }

    if (VATTR_IS_ACTIVE(vap, va_change_time)) {
        fsai->FUSEATTR(mtime) = vap->va_change_time.tv_sec;
        fsai->FUSEATTR(mtimensec) = vap->va_change_time.tv_nsec;
        fsai->valid |=  FATTR_MTIME;
    }

    if (VATTR_IS_ACTIVE(vap, va_modify_time)) {
        fsai->FUSEATTR(mtime) = vap->va_modify_time.tv_sec;
        fsai->FUSEATTR(mtimensec) = vap->va_modify_time.tv_nsec;
        fsai->valid |=  FATTR_MTIME;
    }

    VATTR_SET_SUPPORTED(vap, va_access_time);
    VATTR_SET_SUPPORTED(vap, va_change_time);
    VATTR_SET_SUPPORTED(vap, va_modify_time);

    /* Don't support va_{backup, change, create}_time */

    /* XXX: What about VA_UTIMES_NULL? */

    if (VATTR_IS_ACTIVE(vap, va_mode)) {
        //if (vap->va_mode & S_IFMT) {
            fsai->FUSEATTR(mode) = vap->va_mode & ALLPERMS;
            fsai->valid |= FATTR_MODE;
        //}
    }
    VATTR_SET_SUPPORTED(vap, va_mode);

#undef FUSEATTR

    if (!fsai->valid) {
        goto out;
    }

    vtyp = vnode_vtype(vp);

    if (fsai->valid & FATTR_SIZE && vtyp == VDIR) {
        err = EISDIR;
        goto out;
    }

    if (vfs_flags(vnode_mount(vp)) & MNT_RDONLY &&
        (fsai->valid & ~FATTR_SIZE || vtyp == VREG)) {
        err = EROFS;
        goto out;
    }

    if (fsai->valid & ~FATTR_SIZE) {
        // err = fuse_internal_access(vp, VADMIN, context, &facp); // XXX
        err = 0;
    }

    facp.facc_flags &= ~FACCESS_XQUERIES;

    action = KAUTH_VNODE_WRITE_ATTRIBUTES;
    if (VATTR_IS_ACTIVE(vap, va_acl)    ||
        VATTR_IS_ACTIVE(vap, va_uuuid)  ||
        VATTR_IS_ACTIVE(vap, va_guuid)) {
        action |= KAUTH_VNODE_WRITE_SECURITY;
    }

    if (err && !(fsai->valid & ~(FATTR_ATIME | FATTR_MTIME)) &&
        vap->va_vaflags & VA_UTIMES_NULL) {
        err = fuse_internal_access(vp, action, context, &facp);
    }

    if (err) {
        fuse_invalidate_attr(vp);
        goto out;
    }

    if ((err = fdisp_wait_answ(&fdi))) {
        fuse_invalidate_attr(vp);
        return (err);
    }

    vtyp = IFTOVT(((struct fuse_attr_out *)fdi.answ)->attr.mode);

    if (vnode_vtype(vp) != vtyp) {
        if (vnode_vtype(vp) == VNON && vtyp != VNON) {
            debug_printf("MacFUSE: vnode_vtype is VNON and vtype isn't!\n");
        } else {

            /*
             * STALE vnode, ditch
             *
             * The vnode has changed its type "behind our back". There's
             * nothing really we can do, so let us just force an internal
             * revocation and tell the caller to try again, if interested.
             */

            fuse_internal_vnode_disappear(vp, context);

            err = EAGAIN;
        }
    }

    if (!err) {
        if (sizechanged) {
            fuse_invalidate_attr(vp);
        } else {
            cache_attrs(vp, (struct fuse_attr_out *)fdi.answ);
        }
    }

out:
    fuse_ticket_drop(fdi.tick);
    if (!err && sizechanged) {
        VTOFUD(vp)->filesize = newsize;
        ubc_setsize(vp, (off_t)newsize);
    }

    if (err == 0) {
        FUSE_KNOTE(vp, NOTE_ATTRIB);
    }

    return (err);
}

#if M_MACFUSE_ENABLE_XATTR
/*
    struct vnop_setxattr_args {
        struct vnodeop_desc *a_desc;
        vnode_t              a_vp;
        char                *a_name;
        uio_t                a_uio;
        int                  a_options;
        vfs_context_t        a_context;
    };
*/
static int
fuse_vnop_setxattr(struct vnop_setxattr_args *ap)
{
    int err = 0;
    int iov_err = 0;
    int i, iov_cnt, namelen;
    size_t attrsize;
    user_addr_t a_baseaddr[FUSE_UIO_BACKUP_MAX];
    user_size_t a_length[FUSE_UIO_BACKUP_MAX];
    struct fuse_dispatcher   fdi;
    struct fuse_setxattr_in *fsxi;
    struct fuse_data        *data;

    vnode_t vp = ap->a_vp;
    uio_t uio = ap->a_uio;
    vfs_context_t context = ap->a_context;

    fuse_trace_printf_vnop();

    if (fuse_isdeadfs_nop(vp)) {
        return EBADF;
    }

    CHECK_BLANKET_DENIAL(vp, context, ENOENT);

    data = fuse_get_mpdata(vnode_mount(vp));
    if (data->noimplflags & FSESS_NOIMPL(SETXATTR)) {
        return ENOTSUP;
    }

    if (ap->a_name == NULL || ap->a_name[0] == '\0') {
        return EINVAL;
    }

    if (fuse_is_shortcircuit_xattr(ap->a_name)) {
        return ENOTSUP;
    }

    attrsize = uio_resid(uio);

    iov_cnt = uio_iovcnt(uio);
    if (iov_cnt > FUSE_UIO_BACKUP_MAX) {
        /* no need to make it more complicated */
        iov_cnt = FUSE_UIO_BACKUP_MAX;
    }

    for (i = 0; i < iov_cnt; i++) {
        iov_err = uio_getiov(uio, i, &(a_baseaddr[i]), &(a_length[i]));
    }

    /*
     * Check attrsize for some sane maximum: otherwise, we can fail malloc()
     * in fdisp_make_vp().
     */
    if (attrsize > FUSE_MAXATTRIBUTESIZE) {
        return E2BIG;
    }

    namelen = strlen(ap->a_name);

    fdisp_init(&fdi, sizeof(*fsxi) + namelen + 1 + attrsize);
    fdisp_make_vp(&fdi, FUSE_SETXATTR, vp, ap->a_context);
    fsxi = fdi.indata;

    fsxi->size = attrsize;
    fsxi->flags = ap->a_options;

    memcpy((char *)fdi.indata + sizeof(*fsxi), ap->a_name, namelen);
    ((char *)fdi.indata)[sizeof(*fsxi) + namelen] = '\0';

    err = uiomove((char *)fdi.indata + sizeof(*fsxi) + namelen + 1, attrsize,
                  uio);
    if (!err) {
        err = fdisp_wait_answ(&fdi);
    }

    if (!err) {
        fuse_ticket_drop(fdi.tick);
    } else {
        if (err == ENOSYS) {

            int a_spacetype = UIO_USERSPACE;

            data->noimplflags |= FSESS_NOIMPL(SETXATTR);

            if (iov_err) {
                return EAGAIN;
            }

            if (!uio_isuserspace(uio)) {
                a_spacetype = UIO_SYSSPACE;
            }

            uio_reset(uio, (off_t)0, a_spacetype, uio_rw(uio));
            for (i = 0; i < iov_cnt; i++) {
                uio_addiov(uio, CAST_USER_ADDR_T(a_baseaddr[i]), a_length[i]);
            }

            return ENOTSUP;
        }
    }

    return err;
}
#endif /* M_MACFUSE_ENABLE_XATTR */

/*
    struct vnop_strategy_args {
        struct vnodeop_desc *a_desc;
        struct buf          *a_bp;
    };
*/
static int
fuse_vnop_strategy(struct vnop_strategy_args *ap)
{
    buf_t   bp;
    vnode_t vp;
    struct fuse_vnode_data *fvdat;

    fuse_trace_printf_vnop();

    bp = ap->a_bp;
    vp = buf_vnode(bp);
    fvdat = VTOFUD(vp);

    if (!vp || (fuse_isdeadfs(vp))) {
        buf_seterror(bp, EIO);
        buf_biodone(bp);
        return EIO;
    }

    return fuse_internal_strategy_buf(ap);
}

/*
    struct vnop_symlink_args {
        struct vnodeop_desc  *a_desc;
        vnode_t               a_dvp;
        vnode_t              *a_vpp;
        struct componentname *a_cnp;
        struct vnode_attr    *a_vap;
        char                 *a_target;
        vfs_context_t         a_context;
    };
*/
static int  
fuse_vnop_symlink(struct vnop_symlink_args *ap)
{           
    vnode_t dvp = ap->a_dvp; 
    vnode_t *vpp = ap->a_vpp;
    struct componentname *cnp = ap->a_cnp;
    char *target = ap->a_target;
    vfs_context_t context = ap->a_context;
            
    int err;
    size_t len;
    struct fuse_dispatcher fdi;
        
    fuse_trace_printf_vnop();

    if (fuse_isdeadfs_nop(dvp)) {
        panic("MacFUSE: fuse_vnop_symlink(): called on a dead file system");
    }
            
    CHECK_BLANKET_DENIAL(dvp, context, EPERM);

    len = strlen(target) + 1;
    fdisp_init(&fdi, len + cnp->cn_namelen + 1);
    fdisp_make_vp(&fdi, FUSE_SYMLINK, dvp, context);

    memcpy(fdi.indata, cnp->cn_nameptr, cnp->cn_namelen);
    ((char *)fdi.indata)[cnp->cn_namelen] = '\0';
    memcpy((char *)fdi.indata + cnp->cn_namelen + 1, target, len);

    err = fuse_internal_newentry_core(dvp, vpp, cnp, VLNK, &fdi, context);
    fuse_invalidate_attr(dvp);

    if (err == 0) {
        FUSE_KNOTE(dvp, NOTE_WRITE);
    }

    return (err);
}       

/*
    struct vnop_write_args {
        struct vnodeop_desc *a_desc;
        vnode_t              a_vp;
        struct uio          *a_uio;
        int                  a_ioflag;
        vfs_context_t        a_context;
    };
*/
static int
fuse_vnop_write(struct vnop_write_args *ap)
{
    vnode_t       vp      = ap->a_vp;
    uio_t         uio     = ap->a_uio;
    int           ioflag  = ap->a_ioflag;
    vfs_context_t context = ap->a_context;

    int          error;
    int          lflag;
    off_t        offset;
    off_t        zero_off;
    off_t        filesize;
    off_t        original_offset;
    off_t        original_size;
    user_ssize_t original_resid;

    struct fuse_vnode_data *fvdat;

    /*
     * XXX: Locking
     *
     * lock_shared(truncatelock)
     * lock(nodelock)
     * if (file is being extended) {
     *     unlock(nodelock)
     *     unlock(truncatelock)
     *     lock_exclusive(truncatelock)
     *     lock(nodelock)
     *     current_size = the file's current size
     * }
     * if (file is being extended) { // check again
     *     // do whatever needs to be done to allocate storage
     * }
     * // We are always block-aligned
     * unlock(nodelock)
     * call the cluster layer
     * adjust ubc
     * lock(nodelock)
     * do cleanup 
     * unlock(nodelock)
     * unlock(truncatelock)
     */

    fuse_trace_printf_vnop();

    if (fuse_isdeadfs_nop(vp)) {
        return EIO;
    }

    fvdat = VTOFUD(vp);

    switch (vnode_vtype(vp)) {
    case VREG:
        break;

    case VDIR:
        return EISDIR;

    default:
        // Wanna panic here?
        return EPERM; // or EINVAL?
    }

    original_resid = uio_resid(uio);
    original_offset = uio_offset(uio);
    offset = original_offset;

    if (original_resid == 0) {
        return E_NONE;
    }

    if (original_offset < 0) {
        return EINVAL;
    }

    if (fuse_isdirectio(vp)) { /* direct_io */
        fufh_type_t             fufh_type = FUFH_WRONLY;
        struct fuse_dispatcher  fdi;
        struct fuse_filehandle *fufh = NULL;
        struct fuse_write_in   *fwi = NULL;
        struct fuse_write_out  *fwo = NULL;
        struct fuse_data       *data = fuse_get_mpdata(vnode_mount(vp));

        int chunksize;
        off_t diff;

        fufh = &(fvdat->fufh[fufh_type]);
        if (!(fufh->fufh_flags & FUFH_VALID)) {
            fufh_type = FUFH_RDWR;
            fufh = &(fvdat->fufh[fufh_type]);
            if (!(fufh->fufh_flags & FUFH_VALID)) {
                fufh = NULL;
            } else {
                debug_printf("write falling back to FUFH_RDWR ... OK\n");
            }
        }

        if (!fufh) {
            debug_printf("WRITE: no fufh: failing direct I/O\n");
            return EIO;
        } else {
           debug_printf("WRITE: using existing fufh of type %d\n", fufh_type);
        }

        fdisp_init(&fdi, 0);

        while (uio_resid(uio) > 0) {
            chunksize = min(uio_resid(uio), data->iosize);
            fdi.iosize = sizeof(*fwi) + chunksize;
            fdisp_make_vp(&fdi, FUSE_WRITE, vp, context);
            fwi = fdi.indata;
            fwi->fh = fufh->fh_id;
            fwi->offset = uio_offset(uio);
            fwi->size = chunksize;

            error = uiomove((char *)fdi.indata + sizeof(*fwi), chunksize, uio);
            if (error) {
                break;
            }

            error = fdisp_wait_answ(&fdi);
            if (error) {
                return error;
            }

            fwo = (struct fuse_write_out *)fdi.answ;

            diff = chunksize - fwo->size;
            if (diff < 0) {
                error = EINVAL;
                break;
            }

            uio_setresid(uio, (uio_resid(uio) + diff));
            uio_setoffset(uio, (uio_offset(uio) - diff));
        } /* while */

        fuse_ticket_drop(fdi.tick);

        return error;

    } /* direct_io */

    /* !direct_io */

    // Be wary of a size change here.

    original_size = fvdat->filesize;

    if (ioflag & IO_APPEND) {
        debug_printf("WRITE: arranging for append\n");
        uio_setoffset(uio, fvdat->filesize);
        offset = fvdat->filesize;
    }

    if (offset < 0) {
        return EFBIG;
    }

#if M_MACFUSE_EXPERIMENTAL_JUNK
    if (original_resid == 0) {
        return 0;
    }

    if (offset + original_resid > /* some maximum file size */) {
        return EFBIG;
    }
#endif

    if (offset + original_resid > original_size) {
        /* Need to extend the file. */
        debug_printf("WRITE: need to extend the file\n");
        filesize = offset + original_resid;
        fvdat->filesize = filesize;
    } else {
        debug_printf("WRITE: original size OK\n");
        filesize = original_size;
    }
        
    lflag = (ioflag & IO_SYNC);

    if (offset > original_size) {
        zero_off = original_size;
        lflag |= IO_HEADZEROFILL;
        debug_printf("WRITE: zero filling enabled\n");
    } else {
        zero_off = 0;
    }

    error = cluster_write(vp, uio, (off_t)original_size, (off_t)filesize,
                          (off_t)zero_off, (off_t)0, lflag);
        
    if (uio_offset(uio) > original_size) {
        debug_printf("WRITE: updating to new size\n");
        fuse_invalidate_attr(vp);
        fvdat->filesize = uio_offset(uio);
        ubc_setsize(vp, (off_t)fvdat->filesize);
        FUSE_KNOTE(vp, NOTE_WRITE | NOTE_EXTEND);
    } else {
        fvdat->filesize = original_size;
        FUSE_KNOTE(vp, NOTE_WRITE);
    }

#if M_MACFUSE_EXPERIMENTAL_JUNK
     if (original_resid > uio_resid(uio)) {
         dep->de_flag |= DE_UPDATE;
     }
#endif
        
    /*
     * If the write failed and they want us to, truncate the file back
     * to the size it was before the write was attempted.
     */

/* errexit: */
    if (error) {
        debug_printf("WRITE: we had a failed write (%d)\n", error);
        if (ioflag & IO_UNIT) {
#if M_MACFUSE_EXPERIMENTAL_JUNK
            detrunc(dep, original_size, ioflag & IO_SYNC, context);
#endif
            uio_setoffset(uio, original_offset);
            uio_setresid(uio, original_resid);
        } else {
#if M_MACFUSE_EXPERIMENTAL_JUNK
            detrunc(dep, dep->de_FileSize, ioflag & IO_SYNC, context);
#endif
            if (uio_resid(uio) != original_resid) {
                error = 0;
            }
        }
    } else if (ioflag & IO_SYNC) {
#if M_MACFUSE_EXPERIMENTAL_JUNK
        error = deupdat(dep, 1, context);
#endif
        ;
    }

#if M_MACFUSE_EXPERIMENTAL_JUNK
    if ((original_resid > uio_resid(uio)) &&
        !fuse_vfs_context_issuser(context)) {
        /* clear setuid/setgid here */
    }
#endif

    return error;
}

struct vnodeopv_entry_desc fuse_vnode_operation_entries[] = {
    { &vnop_access_desc,        (fuse_vnode_op_t) fuse_vnop_access        },
    { &vnop_advlock_desc,       (fuse_vnode_op_t) err_advlock             },
//  { &vnop_allocate_desc,      (fuse_vnode_op_t) fuse_vnop_allocate      },
    { &vnop_blktooff_desc,      (fuse_vnode_op_t) fuse_vnop_blktooff      },
    { &vnop_blockmap_desc,      (fuse_vnode_op_t) fuse_vnop_blockmap      },
//  { &vnop_bwrite_desc,        (fuse_vnode_op_t) fuse_vnop_bwrite        },
    { &vnop_close_desc,         (fuse_vnode_op_t) fuse_vnop_close         },
//  { &vnop_copyfile_desc,      (fuse_vnode_op_t) fuse_vnop_copyfile      },
    { &vnop_create_desc,        (fuse_vnode_op_t) fuse_vnop_create        },
    { &vnop_default_desc,       (fuse_vnode_op_t) vn_default_error        },
//  { &vnop_exchange_desc,      (fuse_vnode_op_t) fuse_vnop_exchange      },
    { &vnop_fsync_desc,         (fuse_vnode_op_t) fuse_vnop_fsync         },
    { &vnop_getattr_desc,       (fuse_vnode_op_t) fuse_vnop_getattr       },
//  { &vnop_getattrlist_desc,   (fuse_vnode_op_t) fuse_vnop_getattrlist   },
#if M_MACFUSE_ENABLE_XATTR
    { &vnop_getxattr_desc,      (fuse_vnode_op_t) fuse_vnop_getxattr      },
#endif /* M_MACFUSE_ENABLE_XATTR */
    { &vnop_inactive_desc,      (fuse_vnode_op_t) fuse_vnop_inactive      },
    { &vnop_ioctl_desc,         (fuse_vnode_op_t) fuse_vnop_ioctl         },
    { &vnop_link_desc,          (fuse_vnode_op_t) fuse_vnop_link          },
#if M_MACFUSE_ENABLE_XATTR
    { &vnop_listxattr_desc,     (fuse_vnode_op_t) fuse_vnop_listxattr     },
#endif /* M_MACFUSE_ENABLE_XATTR */
    { &vnop_lookup_desc,        (fuse_vnode_op_t) fuse_vnop_lookup        },
#if M_MACFUSE_ENABLE_UNSUPPORTED
    { &vnop_kqfilt_add_desc,    (fuse_vnode_op_t) fuse_vnop_kqfilt_add    },
    { &vnop_kqfilt_remove_desc, (fuse_vnode_op_t) fuse_vnop_kqfilt_remove },
#endif /* M_MACFUSE_ENABLE_UNSUPPORTED */
    { &vnop_mkdir_desc,         (fuse_vnode_op_t) fuse_vnop_mkdir         },
    { &vnop_mknod_desc,         (fuse_vnode_op_t) fuse_vnop_mknod         },
    { &vnop_mmap_desc,          (fuse_vnode_op_t) fuse_vnop_mmap          },
    { &vnop_mnomap_desc,        (fuse_vnode_op_t) fuse_vnop_mnomap        },
    { &vnop_offtoblk_desc,      (fuse_vnode_op_t) fuse_vnop_offtoblk      },
    { &vnop_open_desc,          (fuse_vnode_op_t) fuse_vnop_open          },
    { &vnop_pagein_desc,        (fuse_vnode_op_t) fuse_vnop_pagein        },
    { &vnop_pageout_desc,       (fuse_vnode_op_t) fuse_vnop_pageout       },
    { &vnop_pathconf_desc,      (fuse_vnode_op_t) fuse_vnop_pathconf      },
    { &vnop_read_desc,          (fuse_vnode_op_t) fuse_vnop_read          },
    { &vnop_readdir_desc,       (fuse_vnode_op_t) fuse_vnop_readdir       },
//  { &vnop_readdirattr_desc,   (fuse_vnode_op_t) fuse_vnop_readdirattr   },
    { &vnop_readlink_desc,      (fuse_vnode_op_t) fuse_vnop_readlink      },
    { &vnop_reclaim_desc,       (fuse_vnode_op_t) fuse_vnop_reclaim       },
    { &vnop_remove_desc,        (fuse_vnode_op_t) fuse_vnop_remove        },
#if M_MACFUSE_ENABLE_XATTR
    { &vnop_removexattr_desc,   (fuse_vnode_op_t) fuse_vnop_removexattr   },
#endif /* M_MACFUSE_ENABLE_XATTR */
    { &vnop_rename_desc,        (fuse_vnode_op_t) fuse_vnop_rename        },
    { &vnop_revoke_desc,        (fuse_vnode_op_t) fuse_vnop_revoke        },
    { &vnop_rmdir_desc,         (fuse_vnode_op_t) fuse_vnop_rmdir         },
//  { &vnop_searchfs_desc,      (fuse_vnode_op_t) fuse_vnop_searchfs      },
    { &vnop_select_desc,        (fuse_vnode_op_t) fuse_vnop_select        },
    { &vnop_setattr_desc,       (fuse_vnode_op_t) fuse_vnop_setattr       },
//  { &vnop_setattrlist_desc,   (fuse_vnode_op_t) fuse_vnop_setattrlist   }, 
#if M_MACFUSE_ENABLE_XATTR
    { &vnop_setxattr_desc,      (fuse_vnode_op_t) fuse_vnop_setxattr      },
#endif /* M_MACFUSE_ENABLE_XATTR */
    { &vnop_strategy_desc,      (fuse_vnode_op_t) fuse_vnop_strategy      },
    { &vnop_symlink_desc,       (fuse_vnode_op_t) fuse_vnop_symlink       },
//  { &vnop_whiteout_desc,      (fuse_vnode_op_t) fuse_vnop_whiteout      },
    { &vnop_write_desc,         (fuse_vnode_op_t) fuse_vnop_write         },
    { NULL, NULL }
};
