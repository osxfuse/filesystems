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
#include "fuse_ipc.h"
#include "fuse_node.h"
#include "fuse_nodehash.h"
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

    fuse_trace_printf_vnop();

    if (fuse_isdeadfs(vp)) {
        if (vnode_isvroot(vp)) {
            return 0;
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
        
    fuse_trace_printf_vnop();

    vp        = ap->a_vp;
    lblkno    = ap->a_lblkno;
    offsetPtr = ap->a_offset;

    if (fuse_isdeadfs_nop(vp)) {
        return EIO;
    }

    *offsetPtr = lblkno * FUSE_DEFAULT_BLOCKSIZE;

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
    uint32_t      contiguousPhysicalBytes;
    struct fuse_vnode_data *fvdat;

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

    *bpnPtr = foffset / FUSE_DEFAULT_BLOCKSIZE;

    if (fvdat->newfilesize > fvdat->filesize) {
       contiguousPhysicalBytes = \
           fvdat->newfilesize - (*bpnPtr * FUSE_DEFAULT_BLOCKSIZE);
    } else {
       contiguousPhysicalBytes = \
           fvdat->filesize - (*bpnPtr * FUSE_DEFAULT_BLOCKSIZE);
    }

    if (contiguousPhysicalBytes > size) {
        contiguousPhysicalBytes = size;
    }

    *runPtr = contiguousPhysicalBytes;

    if (poffPtr != NULL) {
        *poffPtr = 0;
    }

    debug_printf("offset=%lld, size=%ld, bpn=%lld, run=%d, filesize=%lld\n",
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
    fufh_type_t             fufh_type;
    struct fuse_vnode_data *fvdat = VTOFUD(vp);
    struct fuse_filehandle *fufh = NULL;

    fuse_trace_printf_vnop();

    if (fuse_isdeadfs_nop(vp)) {
        return 0;
    }

    if (vnode_vtype(vp) == VDIR) {
        fufh_type = FUFH_RDONLY;
    } else {
        fufh_type = fuse_filehandle_xlate_from_fflags(ap->a_fflag);
    }

    fufh = &(fvdat->fufh[fufh_type]);

    if (!(fufh->fufh_flags & FUFH_VALID)) {
        panic("fufh type %d found to be invalid in close\n", fufh_type);
    }

    fufh->open_count--;

    if ((fufh->open_count == 0) && !(fufh->fufh_flags & FUFH_MAPPED)) {
        (void)fuse_filehandle_put(vp, ap->a_context, fufh_type, 0);
    }

    return 0;
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
    struct componentname *cnp = ap->a_cnp;
    struct vnode_attr    *vap = ap->a_vap;
    vfs_context_t context = ap->a_context;

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
        panic("fuse_vnop_create(): called on a dead file system");
    }

    bzero(&fdi, sizeof(fdi));

    // XXX: Will we ever want devices?
    if ((vap->va_type != VREG) ||
        fusefs_get_data(mp)->dataflag & FSESS_NOCREATE) {
        goto good_old;
    }

    debug_printf("parent nid = %llu, mode = %x\n", parentnid, mode);

    fdisp_init(fdip, sizeof(*foi) + cnp->cn_namelen + 1);
    if (fusefs_get_data(vnode_mount(dvp))->dataflag & FSESS_NOCREATE) {
        debug_printf("eh, daemon doesn't implement create?\n");
        goto good_old;
    }

    fdisp_make(fdip, FUSE_CREATE, vnode_mount(dvp), parentnid, context);

    foi = fdip->indata;
    foi->mode = mode;
    foi->flags = O_RDWR; // XXX: We /always/ creat() like this.

    memcpy((char *)fdip->indata + sizeof(*foi), cnp->cn_nameptr,
           cnp->cn_namelen);
    ((char *)fdip->indata)[sizeof(*foi) + cnp->cn_namelen] = '\0';

    err = fdisp_wait_answ(fdip);

    if (err == ENOSYS) {
        debug_printf("create: got ENOSYS from daemon\n");
        fusefs_get_data(vnode_mount(dvp))->dataflag |= FSESS_NOCREATE;
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
                                         feo->nodeid,
                                         dvp,
                                         VREG, /*size*/0,
                                         vpp,
                                         (gone_good_old) ? 0 : FN_CREATING);
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

#if 0
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

    cache_enter(dvp, *vpp, cnp);

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
#if 0
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

#if 0
    buf_flushdirtyblks(vp, wait, 0, (char *)"fuse_fsync");
    microtime(&tv);
#endif

    // vnode and ubc are in lock-step.
    // can call vnode_isinuse().
    // can call ubc_sync_range().

    if (!(fusefs_get_data(vnode_mount(vp))->dataflag &
        vnode_vtype(vp) == VDIR ? FSESS_NOFSYNCDIR : FSESS_NOFSYNC)) {
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
    int dataflag;
    struct timespec uptsp;
    struct fuse_dispatcher fdi;

    vnode_t vp = ap->a_vp;
    struct vnode_attr *vap = ap->a_vap;
    vfs_context_t context = ap->a_context;

    fuse_trace_printf_vnop();

    dataflag = fusefs_get_data(vnode_mount(vp))->dataflag;

    /* Note that we are not bailing out on a dead file system just yet. */

    /* look for cached attributes */
    nanouptime(&uptsp);
    if (fusetimespeccmp(&uptsp, &VTOFUD(vp)->cached_attrs_valid, <=)) {
        if (vap != VTOVA(vp)) {
            memcpy(vap, VTOVA(vp), sizeof(*vap));
        }
        debug_printf("fuse_getattr a: returning 0\n");
        return (0);
    }

    if (!(dataflag & FSESS_INITED)) {
        if (!vnode_isvroot(vp)) {
            fdata_kick_set(fusefs_get_data(vnode_mount(vp)));
            err = ENOTCONN;
            debug_printf("fuse_getattr b: returning ENOTCONN\n");
            return (err);
        } else {
            goto fake;
        }
    }

    if ((err = fdisp_simple_putget_vp(&fdi, FUSE_GETATTR, vp, context))) {
        if (err == ENOTCONN && vnode_isvroot(vp)) {
            /* see comment at similar place in fuse_statfs() */
            goto fake;
        }
        debug_printf("fuse_getattr c: returning ENOTCONN\n");
        return (err);
    }

    cache_attrs(vp, (struct fuse_attr_out *)fdi.answ);
    if (vap != VTOVA(vp)) {
        memcpy(vap, VTOVA(vp), sizeof(*vap));
    }

    fuse_ticket_drop(fdi.tick);

    if (vnode_vtype(vp) != vap->va_type) {
        if (vnode_vtype(vp) == VNON && vap->va_type != VNON) {
            // vp->v_type = vap->va_type;
        } else {
            /* stale vnode */
            // XXX: vnode should be ditched.
            debug_printf("fuse_getattr d: returning ENOTCONN\n");
            return (ENOTCONN);
        }
    }

    debug_printf("fuse_getattr e: returning 0\n");

    return (0);

fake:
    bzero(vap, sizeof(*vap));
    vap->va_type = vnode_vtype(vp);

    return (0);
}

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

    struct fuse_dispatcher    fdi;
    struct fuse_getxattr_in  *fgxi; 
    struct fuse_getxattr_out *fgxo;

    int err = 0;
    int namelen;      
    
    fuse_trace_printf_vnop();

    if (fuse_isdeadfs_nop(vp)) {
        return EBADF;
    }

    if (ap->a_name == NULL || ap->a_name[0] == '\0') {
        return EINVAL;
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
    int type;

    fuse_trace_printf_vnop();

    for (type = 0; type < FUFH_MAXTYPE; type++) {
        fufh = &(fvdat->fufh[type]);
        if (fufh->fufh_flags & FUFH_VALID) {
            fufh->fufh_flags &= ~FUFH_MAPPED;
            fufh->open_count = 0;
            (void)fuse_filehandle_put(vp, ap->a_context, type, 0);
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
/*
 * We'll use ubc_sync_range(vp, 0, filesize, UBC_INVALIDATE) to invalidate a
 * given file's buffer cache. Additionally, we can use one of the UBC_PUSHALL
 * or UBC_PUSHDIRTY bits. The former pushes both dirty and precious pages to
 * the backing store. The latter only cleans any dirty pages.
 *
 * ret = ubc_sync_range(vp, 0, filesize, UBC_INVALIDATE);
 *
 */
    fuse_trace_printf_vnop();

    if (fuse_isdeadfs_nop(ap->a_vp)) {
        return EBADF;
    }

    return EPERM;
}

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

    int err = 0;
    struct fuse_dispatcher fdi;
    struct fuse_entry_out *feo;
    struct fuse_link_in    fli;

    fuse_trace_printf_vnop();

    if (fuse_isdeadfs_nop(vp)) {
        panic("fuse_vnop_link(): called on a dead file system");
    }

    if (vnode_mount(tdvp) != vnode_mount(vp)) {
        return (EXDEV);
    }

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

    return (err);
}

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
    vnode_t vp = ap->a_vp;
    uio_t uio = ap->a_uio;
    struct fuse_dispatcher fdi;
    struct fuse_getxattr_in *fgxi;
    struct fuse_getxattr_out *fgxo;
    int err = 0;

    fuse_trace_printf_vnop();

    if (fuse_isdeadfs_nop(vp)) {
        return EBADF;
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
    uint64_t size = 0;

    if (fuse_isdeadfs(dvp)) {
        *vpp = NULL;
        return ENXIO;
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
        pdp = VTOFUD(dvp)->parent;
        nid = VTOI(pdp);
        parent_nid = VTOFUD(dvp)->parent_nid;
        fdisp_init(&fdi, 0);
        op = FUSE_GETATTR;
        goto calldaemon;
    } else if (cnp->cn_namelen == 1 && *(cnp->cn_nameptr) == '.') {
        nid = VTOI(dvp);
        parent_nid = VTOFUD(dvp)->parent_nid;
        fdisp_init(&fdi, 0);
        op = FUSE_GETATTR;
        goto calldaemon;
    } else {
        err = cache_lookup(dvp, vpp, cnp);
        switch (err) {

        case -1: /* positive match */
            fuse_lookup_cache_hits++;
            return 0;

        case 0: /* no match in cache */
            fuse_lookup_cache_misses++;
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
    fdisp_make(&fdi, op, vnode_mount(dvp), nid, context);

    if (op == FUSE_LOOKUP) {
        memcpy(fdi.indata, cnp->cn_nameptr, cnp->cn_namelen);
        ((char *)fdi.indata)[cnp->cn_namelen] = '\0';
    }

    lookup_err = fdisp_wait_answ(&fdi);

    if (op == FUSE_LOOKUP && !lookup_err) {
        nid = ((struct fuse_entry_out *)fdi.answ)->nodeid;
        size = ((struct fuse_entry_out *)fdi.answ)->attr.size;
        if (!nid) {
            lookup_err = ENOENT;
        } else if (nid == FUSE_ROOT_ID) {
            lookup_err = EINVAL;
        }
    }

    if (lookup_err &&
        ((!fdi.answ_stat) || lookup_err != ENOENT || op != FUSE_LOOKUP)) {
        return (lookup_err);
    }

    if (lookup_err) {

        if ((nameiop == CREATE || nameiop == RENAME) &&
            islastcn /* && directory dvp has not been removed */) {

            if (vfs_isrdonly(mp)) {
                err = EROFS;
                goto out;
            }

#if 0 // THINK_ABOUT_THIS
            if ((err = fuse_internal_access(dvp, VWRITE, context, &facp))) {
                goto out;
            }
#endif

            /*
             * Possibly record the position of a slot in the
             * directory large enough for the new component name.
             * This can be recorded in the vnode private data for
             * dvp. Set the SAVENAME flag to hold onto the
             * pathname for use later in VOP_CREATE or VOP_RENAME.
             */
#define SAVENAME 0x0000800
            cnp->cn_flags |= SAVENAME;
            
            err = EJUSTRETURN;
            goto out;
        }

        /* Consider inserting name into cache. */

        /*
         * No we can't use negative caching, as the fs
         * changes are out of our control.
         * False positives' falseness turns out just as things
         * go by, but false negatives' falseness doesn't.
         * (and aiding the caching mechanism with extra control
         * mechanisms comes quite close to beating the whole purpose
         * caching...)
         */
#if 0
        if ((cnp->cn_flags & MAKEENTRY) && nameiop != CREATE) {
            DEBUG("inserting NULL into cache\n");
            cache_enter(dvp, NULL, cnp);
        }
#endif
        err = ENOENT;
        goto out;

    } else {

        if (op == FUSE_GETATTR) {
            fattr = &((struct fuse_attr_out *)fdi.answ)->attr;
        } else {
            fattr = &((struct fuse_entry_out *)fdi.answ)->attr;
        }

        /*
         * If deleting, and at end of pathname, return parameters
         * which can be used to remove file. If the wantparent flag
         * isn't set, we return only the directory, otherwise we go on
         * and lock the inode, being careful with ".".
         */
        if (nameiop == DELETE && islastcn) {
            /*
             * Check for write access on directory.
             */
            facp.xuid = fattr->uid;
            facp.facc_flags |= FACCESS_STICKY;
            err = fuse_internal_access(dvp, KAUTH_VNODE_DELETE_CHILD,
                                       context, &facp);
            facp.facc_flags &= ~FACCESS_XQUERIES;

            if (err) {
                goto out;
            }

            if (nid == VTOI(dvp)) {
                vnode_get(dvp);
                // VREF(dvp);
                *vpp = dvp;
                goto out;
            }

            /*
             * XXX We discard generation, also brought to us by
             * LOOKUP. It's purpose is unclear... it seems to be
             * simply just yet another 32/64 bit extension for the
             * inode number, but I'm not sure (hmm, it seems to
             * have something to do with NFS exportability, too).
             *
             * (Linux has such a 32 bit value in its inode struct,
             * and it really might just be a backward compatible
             * extension for the inode space [but might as well
             * be more into it, dunno]. The FUSE protocol uses
             * a 64 bit generation value; FUSE/Linux uses it for
             * filling the inode's i_generation field.)
             */   
            //if ((err = fuse_vget_i(vnode_mount(dvp), nid, context,
                //IFTOVT(fattr->mode), &vp, VG_NORMAL)))
            if ((err  = fuse_vget_i(vnode_mount(dvp),
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
        }

        /*
         * If rewriting (RENAME), return the inode and the
         * information required to rewrite the present directory
         * Must get inode of directory entry to verify it's a
         * regular file, or empty directory.
         */
        if (nameiop == RENAME && wantparent && islastcn) {

#if 0 // THINK_ABOUT_THIS
            if ((err = fuse_internal_access(dvp, VWRITE, context, &facp))) {
                goto out;
            }
#endif

            /*
             * Check for "."
             */
            if (nid == VTOI(dvp)) {
                err = EISDIR;
                goto out;
            }

            if ((err  = fuse_vget_i(vnode_mount(dvp),
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
            /*
             * Save the name for use in VOP_RENAME later.
             */
            cnp->cn_flags |= SAVENAME;

            goto out;
        }

        if (flags & ISDOTDOT) {
            err = vnode_get(pdp);
            if (err == 0)
                *vpp = pdp;
        } else if (nid == VTOI(dvp)) {
            // VREF(dvp); /* We want ourself, ie "." */
            err = vnode_get(dvp);
            if (err == 0)
                *vpp = dvp;
        } else {
            if ((err  = fuse_vget_i(vnode_mount(dvp),
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
                VTOFUD(vp)->parent = dvp;
                //SETPARENT(vp, dvp);
            }
            *vpp = vp;
        }

        if (op == FUSE_GETATTR)
            cache_attrs(*vpp, (struct fuse_attr_out *)fdi.answ);
        else
            cache_attrs(*vpp, (struct fuse_entry_out *)fdi.answ);

        /* Insert name into cache if appropriate. */

        /*
         * Nooo, caching is evil. With caching, we can't avoid stale
         * information taking over the playground (cached info is not
         * just positive/negative, it does have qualitative aspects,
         * too). And a (VOP/FUSE)_GETATTR is always thrown anyway, when
         * walking down along cached path components, and that's not
         * any cheaper than FUSE_LOOKUP. This might change with
         * implementing kernel side attr caching, but... In Linux,
         * lookup results are not cached, and the daemon is bombarded
         * with FUSE_LOOKUPS on and on. This shows that by design, the
         * daemon is expected to handle frequent lookup queries
         * efficiently, do its caching in userspace, and so on.
         *
         * So just leave the name cache alone.
         */

        /*
         * Well, now I know, Linux caches lookups, but with a
         * timeout... So it's the same thing as attribute caching:
         * we can deal with it when implement timeouts.
         */    
#if 0
        if (cnp->cn_flags & MAKEENTRY) {
            cache_enter(dvp, *vpp, cnp);
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
    vnode_t dvp = ap->a_dvp;
    vnode_t *vpp = ap->a_vpp;
    struct componentname *cnp = ap->a_cnp;
    struct vnode_attr *vap = ap->a_vap;
    vfs_context_t context = ap->a_context;

    struct fuse_mkdir_in fmdi;

    fuse_trace_printf_vnop();

    if (fuse_isdeadfs_nop(dvp)) {
        panic("fuse_vnop_mkdir(): called on a dead file system");
    }

    fmdi.mode = MAKEIMODE(vap->va_type, vap->va_mode);

    return fuse_internal_newentry(dvp, vpp, cnp, FUSE_MKDIR, &fmdi,
                                  sizeof(fmdi), VDIR, context);
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
    vnode_t dvp = ap->a_dvp;
    vnode_t *vpp = ap->a_vpp;
    struct componentname *cnp = ap->a_cnp;
    struct vnode_attr *vap = ap->a_vap;
    vfs_context_t context = ap->a_context;

    struct fuse_mknod_in fmni;

    fuse_trace_printf_vnop();

    if (fuse_isdeadfs_nop(dvp)) {
        panic("fuse_vnop_mknod(): called on a dead file system");
    }

    fmni.mode = MAKEIMODE(vap->va_type, vap->va_mode);
    fmni.rdev = vap->va_rdev;

    return fuse_internal_newentry(dvp, vpp, cnp, FUSE_MKNOD, &fmni,
                                  sizeof(fmni), vap->va_type, context);
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
    fufh_type_t fufh_type = fuse_filehandle_xlate_from_mmap(fflags);
    struct fuse_vnode_data *fvdat = VTOFUD(vp);
    struct fuse_filehandle *fufh = NULL;

    fuse_trace_printf_vnop();

    if (fuse_isdeadfs_nop(vp)) {
        panic("fuse_vnop_mmap(): called on a dead file system");
    }

    if (fufh_type == FUFH_INVALID) { // nothing to do
        return 0;
    }

    /* XXX: For PROT_WRITE, we should only care if file is mapped MAP_SHARED. */

    fufh = &(fvdat->fufh[fufh_type]);

    if (fufh->fufh_flags & FUFH_VALID) {
        goto out;
    }

    err = fuse_filehandle_get(vp, ap->a_context, fufh_type);
    if (err) {
        printf("Whoa! failed to get filehandle (err %d) in mmap\n", err);
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

    fuse_trace_printf_vnop();

    vp        = ap->a_vp;
    offset    = ap->a_offset;
    lblknoPtr = ap->a_lblkno;

    if (fuse_isdeadfs_nop(vp)) {
        return EIO;
    }

    *lblknoPtr = offset / FUSE_DEFAULT_BLOCKSIZE;

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

    int error, isdir = 0, mode, oflags;

    fuse_trace_printf_vnop();

    vp = ap->a_vp;

    if (fuse_isdeadfs(vp)) {
        return ENXIO;
    }

    mode    = ap->a_mode;
    context = ap->a_context;
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

    oflags = fuse_filehandle_xlate_to_oflags(fufh_type);

    fufh = &(fvdat->fufh[fufh_type]);

    if (!isdir && (fvdat->flag & FN_CREATING)) {

        lck_mtx_lock(fvdat->createlock);

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
                fufh->open_flags = oflags;
                fufh->type = fufh_type;

                fufh->fh_id = fufh_rw->fh_id;
                fufh->open_flags = fufh_rw->open_flags;
                debug_printf("creator picked up stashed handle, moved to %d\n",
                             fufh_type);
                
                fvdat->flag &= ~FN_CREATING;

                lck_mtx_unlock(fvdat->createlock);
                wakeup((caddr_t)fvdat->creator); // wake up all
                goto ok; /* return 0 */
            } else {
                printf("contender going to sleep\n");
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
            lck_mtx_unlock(fvdat->createlock);
            /* Can proceed from here. */
        }
    }

    if (fufh->fufh_flags & FUFH_VALID) {
        fufh->open_count++;
        goto ok; /* return 0 */
    }

    error = fuse_filehandle_get(vp, context, fufh_type);
    if (error) {
        return error;
    }

ok:
    {
        /*
         * Doing this here because when a vnode goes inactive, no-cache and
         * no-readahead are cleared by the kernel.
         */
        int dataflag = fusefs_get_data(vnode_mount(vp))->dataflag;
        if (dataflag & FSESS_NO_READAHEAD) {
            vnode_setnoreadahead(vp);
        }
        if (dataflag & FSESS_NO_UBC) {
            vnode_setnocache(vp);
        }
    }

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

    err = 0;
    switch (name) {
        case _PC_LINK_MAX:
            *retvalPtr = 1;     // no hard link support
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
            *retvalPtr = 1;     // it would be if we supported it (-:
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

        // The following are implemented by VFS:

        case _PC_EXTENDED_SECURITY_NP:
        case _PC_AUTH_OPAQUE_NP:
            assert(FALSE);

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
        vnode_t a_vp;
        struct uio *a_uio;
        int a_ioflag;
        vfs_context_t a_context;
    };
*/
static int
fuse_vnop_read(struct vnop_read_args *ap)
{
    int             err;
    vnode_t         vp;
    uio_t           uio;
    int             ioflag;
    vfs_context_t   context;
    off_t           orig_resid;
    off_t           orig_offset;

    struct fuse_vnode_data *fvdat;

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

    if (vnode_isreg(vp)) {
        err = cluster_read(vp, uio, fvdat->filesize, 0);
    } else if (vnode_isdir(vp)) {
        err = EISDIR;
    } else {
        err = EPERM;
    }

    /*
     * XXX
     *
     * Experiment to see if the following works as expected.
     * Make this an fcntl or fsctl. When the user daemon tells us to
     * do this for a file (which means the file got changed "somewhere else",
     * we should invalidate the buffer cache, redo getattr, see if the size
     * changed, set fvdat->filesize, call ubc_setsize(), etc.
     *
     * ubc_sync_range(vp, 0, fvdat->filesize, UBC_INVALIDATE);
     */

    return err;
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

    /* Sanity check the uio data. */
    if ((uio_iovcnt(uio) > 1) ||
        (uio_resid(uio) < (int)sizeof(struct dirent))) {
        return (EINVAL);
    }

#if 0
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
        err = fuse_filehandle_get(vp, context, FUFH_RDONLY);
        if (err) {
            return err;
        }
        freefufh = 1;
    }

#define DIRCOOKEDSIZE FUSE_DIRENT_ALIGN(FUSE_NAME_OFFSET + MAXNAMLEN + 1)
    fiov_init(&cookediov, DIRCOOKEDSIZE);

#if 0
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

    if (vnode_vtype(vp) != VLNK) {
        return EINVAL;
    }

    if ((err = fdisp_simple_putget_vp(&fdi, FUSE_READLINK, vp, context))) {
        return err;
    }

    if (((char *)fdi.answ)[0] == '/' &&
        fusefs_get_data(vnode_mount(vp))->dataflag & FSESS_PUSH_SYMLINKS_IN) {
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
        panic("FUSE: no vnode data during recycling");
    }

    for (type = 0; type < FUFH_MAXTYPE; type++) {
        fufh = &(fvdat->fufh[type]);
        if (fufh->fufh_flags & FUFH_VALID) {
            if (fufh->fufh_flags & FUFH_STRATEGY) {
                fufh->fufh_flags &= ~FUFH_MAPPED;
                fufh->open_count = 0;
                (void)fuse_filehandle_put(vp, ap->a_context, type, 0);
            } else {
                panic("vnode being reclaimed but fufh (type=%d) is valid",
                      type);
            }
        }
    }

    if ((!fuse_isdeadfs(vp)) && (fvdat->nlookup)) {
        struct fuse_dispatcher fdi;
        fdi.tick = NULL;
        fuse_internal_forget_send(vnode_mount(vp), ap->a_context, VTOI(vp),
                                  fvdat->nlookup, &fdi);
    }

    cache_purge(vp);

    hn = HNodeFromVNode(vp);
    if (HNodeDetachVNode(hn, vp)) {
        FSNodeScrub(fvdat);
        HNodeScrubDone(hn);
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
    vnode_t dvp = ap->a_dvp;
    vnode_t vp = ap->a_vp;
    struct componentname *cnp = ap->a_cnp;
    int flags = ap->a_flags;
    vfs_context_t context = ap->a_context;

    fuse_trace_printf_vnop();

    if (fuse_isdeadfs_nop(vp)) {
        panic("fuse_vnop_remove(): called on a dead file system");
    }

    if (vnode_isdir(vp)) {
        return EPERM;
    }

    if ((flags & VNODE_REMOVE_NODELETEBUSY) && vnode_isinuse(vp, 0)) {
        return EBUSY;
    }

    cache_purge(vp);

    return (fuse_internal_remove(dvp, vp, cnp, FUSE_UNLINK, context));
}

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
    vnode_t vp = ap->a_vp;

    fuse_trace_printf_vnop();

    if (fuse_isdeadfs_nop(vp)) {
        return EBADF;
    }

    if (ap->a_name == NULL || ap->a_name[0] == '\0') {
        return (EINVAL);  /* invalid name */
    }

    namelen = strlen(ap->a_name);

    fdisp_init(&fdi, namelen + 1);
    fdisp_make_vp(&fdi, FUSE_REMOVEXATTR, vp, ap->a_context);

    memcpy((char *)fdi.indata, ap->a_name, namelen);
    ((char *)fdi.indata)[namelen] = '\0';

    err = fdisp_wait_answ(&fdi);
    if (!err) {
        fuse_ticket_drop(fdi.tick);
    }

    return err;
}

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

    fuse_trace_printf_vnop();

    if (fuse_isdeadfs_nop(fdvp)) {
        panic("fuse_vnop_rename(): called on a dead file system");
    }

    cache_purge(fvp);

    err = fuse_internal_rename(fdvp, fcnp, tdvp, tcnp, ap->a_context);

    if (tvp != NULLVP) {
        if (tvp != fvp) {
            cache_purge(tvp);
        }
        if (err == 0) {
            vnode_recycle(tvp);
        }
    }

    return err;
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
    fuse_trace_printf_vnop();

    if (fuse_isdeadfs_nop(ap->a_vp)) {
        panic("fuse_vnop_rmdir(): called on a dead file system");
    }

    if (VTOFUD(ap->a_vp) == VTOFUD(ap->a_dvp)) {
        return EINVAL;
    }

    cache_purge(ap->a_vp);

    return (fuse_internal_remove(ap->a_dvp, ap->a_vp, ap->a_cnp,
                                 FUSE_RMDIR, ap->a_context));
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
fuse_vnop_select(struct vnop_select_args *ap)
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

#define VAP_ENSURE_EQUALITY(lhs, rhs, message) \
    if ((lhs) != (rhs)) { \
        debug_printf(message); \
        return (EINVAL); \
    }

#if 0
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

    if (!fsai->valid)
        goto out;

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

    if (err && !(fsai->valid & ~(FATTR_ATIME | FATTR_MTIME)) &&
        vap->va_vaflags & VA_UTIMES_NULL) {
        err = fuse_internal_access(vp, KAUTH_VNODE_WRITE_ATTRIBUTES,
                                   context, &facp);
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
            debug_printf("FUSE: Dang! vnode_vtype is VNON and vtype isn't.\n");
        } else {
            // XXX: should ditch vnode.
            err = ENOTCONN;
        }
    }

    cache_attrs(vp, (struct fuse_attr_out *)fdi.answ);

out:
    fuse_ticket_drop(fdi.tick);
    if (!err && sizechanged) {
        VTOFUD(vp)->filesize = newsize;
        VTOFUD(vp)->newfilesize = newsize;
        ubc_setsize(vp, (off_t)newsize);
    }

    return (err);
}

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
    vnode_t vp = ap->a_vp;
    uio_t uio = ap->a_uio;
    size_t attrsize;
    struct fuse_dispatcher   fdi;
    struct fuse_setxattr_in *fsxi;
    int err = 0;
    int namelen;

    fuse_trace_printf_vnop();

    if (fuse_isdeadfs_nop(vp)) {
        return EBADF;
    }

    if (ap->a_name == NULL || ap->a_name[0] == '\0') {
        return EINVAL;
    }

    attrsize = uio_resid(uio);

    /*
     * Check attrsize for some sane maximum: otherwise, we can fail malloc()
     * in fdisp_make_vp().
     */
    if (attrsize > 3082) { // Heh.
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
    }

    return err;
}

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
    vnode_t vn;
    struct fuse_vnode_data *fvdat;

    fuse_trace_printf_vnop();

    bp = ap->a_bp;
    vn = buf_vnode(bp);
    fvdat = VTOFUD(vn);

    if ((vn == NULL) || (fuse_isdeadfs(vn))) {
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
        panic("fuse_vnop_symlink(): called on a dead file system");
    }
            
    len = strlen(target) + 1;
    fdisp_init(&fdi, len + cnp->cn_namelen + 1);
    fdisp_make_vp(&fdi, FUSE_SYMLINK, dvp, context);

    memcpy(fdi.indata, cnp->cn_nameptr, cnp->cn_namelen);
    ((char *)fdi.indata)[cnp->cn_namelen] = '\0';
    memcpy((char *)fdi.indata + cnp->cn_namelen + 1, target, len);

    err = fuse_internal_newentry_core(dvp, vpp, cnp, VLNK, &fdi, context);
    fuse_invalidate_attr(dvp);

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
    vnode_t vp = ap->a_vp;
    uio_t uio = ap->a_uio;
    int ioflag = ap->a_ioflag;
    struct fuse_vnode_data *fvdat;

    int          error;
    int          lflag;
    off_t        offset;
    off_t        zero_off;
    u_int32_t    filesize;
    off_t        original_offset;
    u_int32_t    original_size;
    user_ssize_t original_resid;

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
    original_size = fvdat->filesize;
    original_offset = uio_offset(uio);
    offset = original_offset;

    if (original_resid == 0) {
        return E_NONE;
    }

    if (original_offset < 0) {
        return EINVAL;
    }

    // Be wary of a size change here.

    if (ioflag & IO_APPEND) {
        debug_printf("WRITE: arranging for append\n");
        uio_setoffset(uio, fvdat->filesize);
        offset = fvdat->filesize;
    }

    if (offset < 0)
        return EFBIG;

#if 0
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
        fvdat->newfilesize = filesize;
    } else {
        debug_printf("WRITE: original size OK\n");
        filesize = original_size;
        fvdat->newfilesize = filesize;
    }
        
    lflag = (ioflag & IO_SYNC);

    if (offset > original_size) {
        zero_off = original_size;
        lflag   |= IO_HEADZEROFILL;
        debug_printf("WRITE: zero filling enabled\n");
    } else
        zero_off = 0;

    error = cluster_write(vp, uio, (off_t)original_size, (off_t)filesize,
                          (off_t)zero_off, (off_t)0, lflag);
        
    if (uio_offset(uio) > fvdat->filesize) {
        debug_printf("WRITE: updating to new size\n");
        fvdat->filesize = uio_offset(uio);
        ubc_setsize(vp, (off_t)fvdat->filesize);
        fuse_invalidate_attr(vp);
    }
    fvdat->newfilesize = fvdat->filesize;

#if 0
     if (original_resid > uio_resid(uio)) {
         dep->de_flag |= DE_UPDATE;
     }
#endif
        
    /*
     * If the write failed and they want us to, truncate the file back
     * to the size it was before the write was attempted.
     */
errexit:
    if (error) {
        debug_printf("WRITE: we had a failed write (%d)\n", error);
        if (ioflag & IO_UNIT) {
#if 0
            detrunc(dep, original_size, ioflag & IO_SYNC, context);
#endif
            uio_setoffset(uio, original_offset);
            uio_setresid(uio, original_resid);
        } else {
#if 0
            detrunc(dep, dep->de_FileSize, ioflag & IO_SYNC, context);
#endif
            if (uio_resid(uio) != original_resid) {
                error = 0;
            }
        }
    } else if (ioflag & IO_SYNC) {
#if 0
        error = deupdat(dep, 1, context);
#endif
        ;
    }

    return error;
}

struct vnodeopv_entry_desc fuse_vnode_operation_entries[] = {
    { &vnop_access_desc,        (fuse_vnode_op_t) fuse_vnop_access      },
//  { &vnop_advlock_desc,       (fuse_vnode_op_t) fuse_vnop_advlock     },
//  { &vnop_allocate_desc,      (fuse_vnode_op_t) fuse_vnop_allocate    },
    { &vnop_blktooff_desc,      (fuse_vnode_op_t) fuse_vnop_blktooff    },
    { &vnop_blockmap_desc,      (fuse_vnode_op_t) fuse_vnop_blockmap    },
//  { &vnop_bwrite_desc,        (fuse_vnode_op_t) fuse_vnop_bwrite      },
    { &vnop_close_desc,         (fuse_vnode_op_t) fuse_vnop_close       },
//  { &vnop_copyfile_desc,      (fuse_vnode_op_t) fuse_vnop_copyfile    },
    { &vnop_create_desc,        (fuse_vnode_op_t) fuse_vnop_create      },
    { &vnop_default_desc,       (fuse_vnode_op_t) vn_default_error      },
//  { &vnop_exchange_desc,      (fuse_vnode_op_t) fuse_vnop_exchange    },
    { &vnop_fsync_desc,         (fuse_vnode_op_t) fuse_vnop_fsync       },
    { &vnop_getattr_desc,       (fuse_vnode_op_t) fuse_vnop_getattr     },
//  { &vnop_getattrlist_desc,   (fuse_vnode_op_t) fuse_vnop_getattrlist },
    { &vnop_getxattr_desc,      (fuse_vnode_op_t) fuse_vnop_getxattr    },
    { &vnop_inactive_desc,      (fuse_vnode_op_t) fuse_vnop_inactive    },
    { &vnop_ioctl_desc,         (fuse_vnode_op_t) fuse_vnop_ioctl       },
    { &vnop_link_desc,          (fuse_vnode_op_t) fuse_vnop_link        },
    { &vnop_listxattr_desc,     (fuse_vnode_op_t) fuse_vnop_listxattr   },
    { &vnop_lookup_desc,        (fuse_vnode_op_t) fuse_vnop_lookup      },
    { &vnop_mkdir_desc,         (fuse_vnode_op_t) fuse_vnop_mkdir       },
    { &vnop_mknod_desc,         (fuse_vnode_op_t) fuse_vnop_mknod       },
    { &vnop_mmap_desc,          (fuse_vnode_op_t) fuse_vnop_mmap        },
    { &vnop_mnomap_desc,        (fuse_vnode_op_t) fuse_vnop_mnomap      },
    { &vnop_offtoblk_desc,      (fuse_vnode_op_t) fuse_vnop_offtoblk    },
    { &vnop_open_desc,          (fuse_vnode_op_t) fuse_vnop_open        },
    { &vnop_pagein_desc,        (fuse_vnode_op_t) fuse_vnop_pagein      },
    { &vnop_pageout_desc,       (fuse_vnode_op_t) fuse_vnop_pageout     },
    { &vnop_pathconf_desc,      (fuse_vnode_op_t) fuse_vnop_pathconf    },
    { &vnop_read_desc,          (fuse_vnode_op_t) fuse_vnop_read        },
    { &vnop_readdir_desc,       (fuse_vnode_op_t) fuse_vnop_readdir     },
//  { &vnop_readdirattr_desc,   (fuse_vnode_op_t) fuse_vnop_readdirattr },
    { &vnop_readlink_desc,      (fuse_vnode_op_t) fuse_vnop_readlink    },
    { &vnop_reclaim_desc,       (fuse_vnode_op_t) fuse_vnop_reclaim     },
    { &vnop_remove_desc,        (fuse_vnode_op_t) fuse_vnop_remove      },
    { &vnop_removexattr_desc,   (fuse_vnode_op_t) fuse_vnop_removexattr },
    { &vnop_rename_desc,        (fuse_vnode_op_t) fuse_vnop_rename      },
    { &vnop_revoke_desc,        (fuse_vnode_op_t) nop_revoke            },
    { &vnop_rmdir_desc,         (fuse_vnode_op_t) fuse_vnop_rmdir       },
//  { &vnop_searchfs_desc,      (fuse_vnode_op_t) fuse_vnop_searchfs    },
    { &vnop_select_desc,        (fuse_vnode_op_t) fuse_vnop_select      },
    { &vnop_setattr_desc,       (fuse_vnode_op_t) fuse_vnop_setattr     },
//  { &vnop_setattrlist_desc,   (fuse_vnode_op_t) fuse_vnop_setattrlist }, 
    { &vnop_setxattr_desc,      (fuse_vnode_op_t) fuse_vnop_setxattr    },
    { &vnop_strategy_desc,      (fuse_vnode_op_t) fuse_vnop_strategy    },
    { &vnop_symlink_desc,       (fuse_vnode_op_t) fuse_vnop_symlink     },
//  { &vnop_whiteout_desc,      (fuse_vnode_op_t) fuse_vnop_whiteout    },
    { &vnop_write_desc,         (fuse_vnode_op_t) fuse_vnop_write       },
    { NULL, NULL }
};
