/*
 * Copyright (C) 2006 Google. All Rights Reserved.
 * Amit Singh <singh@>
 */

#ifndef _FUSE_INTERNAL_H_
#define _FUSE_INTERNAL_H_

#include <sys/types.h>
#include <sys/kauth.h>
#include <sys/kernel_types.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/ubc.h>
#include <sys/uio.h>
#include <sys/vnode.h>
#include <sys/xattr.h>

#include <UserNotification/KUNCUserNotifications.h>

#include <fuse_ioctl.h>
#include "fuse_ipc.h"
#include "fuse_kludges.h"
#include "fuse_node.h"

struct fuse_attr;
struct fuse_data;
struct fuse_dispatcher;
struct fuse_filehandle;
struct fuse_iov;
struct fuse_ticket;

/* access */

#define FVP_ACCESS_NOOP    0x01

#define FACCESS_VA_VALID   0x01
#define FACCESS_DO_ACCESS  0x02
#define FACCESS_STICKY     0x04
#define FACCESS_CHOWN      0x08
#define FACCESS_NOCHECKSPY 0x10
#define FACCESS_XQUERIES FACCESS_STICKY | FACCESS_CHOWN

struct fuse_access_param {
    uid_t xuid;
    gid_t xgid;
    unsigned facc_flags;
};

int
fuse_internal_access(vnode_t                   vp,
                     int                       action,
                     vfs_context_t             context,
                     struct fuse_access_param *facp);

static __inline__
int
fuse_match_cred(kauth_cred_t daemoncred, kauth_cred_t requestcred)
{
    if ((daemoncred->cr_uid == requestcred->cr_uid)             &&  
        (daemoncred->cr_uid == requestcred->cr_ruid)            &&  

        // THINK_ABOUT_THIS_LATER
        // (daemoncred->cr_uid == requestcred->cr_svuid)        &&  

        (daemoncred->cr_groups[0] == requestcred->cr_groups[0]) &&
        (daemoncred->cr_groups[0] == requestcred->cr_rgid)      &&  
        (daemoncred->cr_groups[0] == requestcred->cr_svgid)) {
        return 0;
    }   

    return EPERM;
}

static __inline__
int
fuse_vfs_context_issuser(vfs_context_t context)
{
    return (vfs_context_ucred(context)->cr_uid == 0);
}

/* attributes */

static __inline__
void
fuse_internal_attr_fat2vat(vnode_t            vp,
                           struct fuse_attr  *fat,
                           struct vnode_attr *vap)
{
    struct timespec t;
    mount_t mp = vnode_mount(vp);
    struct fuse_data *data = fuse_get_mpdata(mp);

    debug_printf("mp=%p, fat=%p, vap=%p\n", mp, fat, vap);

    VATTR_INIT(vap);

    VATTR_RETURN(vap, va_fsid, vfs_statfs(mp)->f_fsid.val[0]);
    VATTR_RETURN(vap, va_fileid, fat->ino);
    VATTR_RETURN(vap, va_linkid, fat->ino);

    /*
     * If we have asynchronous writes enabled, our local in-kernel size
     * takes precedence over what the daemon thinks.
     */
    /* ATTR_FUDGE_CASE */
    if (!vfs_issynchronous(mp)) {
        struct fuse_vnode_data *fvdat = VTOFUD(vp);
        fat->size = fvdat->filesize;    
    }
    VATTR_RETURN(vap, va_data_size, fat->size);

    /*
     * The kernel will compute the following for us if we leave them
     * untouched (and have sane values in statvfs):
     *
     * va_total_size
     * va_data_alloc
     * va_total_alloc
     */

    t.tv_sec = fat->atime; t.tv_nsec = fat->atimensec;
    VATTR_RETURN(vap, va_access_time, t);

    t.tv_sec = fat->ctime; t.tv_nsec = fat->ctimensec;
    VATTR_RETURN(vap, va_change_time, t);

    t.tv_sec = fat->mtime; t.tv_nsec = fat->mtimensec;
    VATTR_RETURN(vap, va_modify_time, t);

    VATTR_RETURN(vap, va_mode, fat->mode & ~S_IFMT);
    VATTR_RETURN(vap, va_nlink, fat->nlink);
    VATTR_RETURN(vap, va_uid, fat->uid);
    VATTR_RETURN(vap, va_gid, fat->gid);
    VATTR_RETURN(vap, va_rdev, fat->rdev);

    VATTR_RETURN(vap, va_type, IFTOVT(fat->mode));

    VATTR_RETURN(vap, va_iosize, data->iosize);

    VATTR_RETURN(vap, va_flags, 0);
}

static __inline__
void
fuse_internal_attr_loadvap(vnode_t vp, struct vnode_attr *out_vap)
{
    mount_t mp = vnode_mount(vp);
    struct vnode_attr *in_vap = VTOVA(vp);

    if (in_vap == out_vap) {
        return;
    }

    VATTR_RETURN(out_vap, va_fsid, in_vap->va_fsid);
    VATTR_RETURN(out_vap, va_fileid, in_vap->va_fileid);
    VATTR_RETURN(out_vap, va_linkid, in_vap->va_linkid);

    /*
     * If we have asynchronous writes enabled, our local in-kernel size
     * takes precedence over what the daemon thinks.
     */
    /* ATTR_FUDGE_CASE */
    if (!vfs_issynchronous(mp)) {
        VATTR_RETURN(out_vap, va_data_size, VTOFUD(vp)->filesize);
        VATTR_RETURN(in_vap,  va_data_size, VTOFUD(vp)->filesize);
    } else {
        VATTR_RETURN(out_vap, va_data_size, in_vap->va_data_size);
    }

    VATTR_RETURN(out_vap, va_access_time, in_vap->va_access_time);
    VATTR_RETURN(out_vap, va_change_time, in_vap->va_change_time);
    VATTR_RETURN(out_vap, va_modify_time, in_vap->va_modify_time);

    VATTR_RETURN(out_vap, va_mode, in_vap->va_mode);
    VATTR_RETURN(out_vap, va_nlink, in_vap->va_nlink);
    VATTR_RETURN(out_vap, va_uid, in_vap->va_uid);
    VATTR_RETURN(out_vap, va_gid, in_vap->va_gid);
    VATTR_RETURN(out_vap, va_rdev, in_vap->va_rdev);

    VATTR_RETURN(out_vap, va_type, in_vap->va_type);

    VATTR_RETURN(out_vap, va_iosize, in_vap->va_iosize);

    VATTR_RETURN(out_vap, va_flags, in_vap->va_flags);
}

#define timespecadd(vvp, uvp)                  \
    do {                                       \
           (vvp)->tv_sec += (uvp)->tv_sec;     \
           (vvp)->tv_nsec += (uvp)->tv_nsec;   \
           if ((vvp)->tv_nsec >= 1000000000) { \
               (vvp)->tv_sec++;                \
               (vvp)->tv_nsec -= 1000000000;   \
           }                                   \
    } while (0)

#define cache_attrs(vp, fuse_out) do {                                         \
    struct timespec uptsp_ ## __func__;                                        \
                                                                               \
    VTOFUD(vp)->cached_attrs_valid.tv_sec = (fuse_out)->attr_valid;            \
    VTOFUD(vp)->cached_attrs_valid.tv_nsec = (fuse_out)->attr_valid_nsec;      \
    nanouptime(&uptsp_ ## __func__);                                           \
                                                                               \
    timespecadd(&VTOFUD(vp)->cached_attrs_valid, &uptsp_ ## __func__);         \
                                                                               \
    fuse_internal_attr_fat2vat(vp, &(fuse_out)->attr, VTOVA(vp));              \
} while (0)

/* fsync */

int
fuse_internal_fsync(vnode_t                 vp,
                    vfs_context_t           context,
                    struct fuse_filehandle *fufh,
                    void                   *param);

int
fuse_internal_fsync_callback(struct fuse_ticket *ftick, uio_t uio);

/* ioctl */

int
fuse_internal_ioctl_avfi(vnode_t                 vp,
                         vfs_context_t           context,
                         struct fuse_avfi_ioctl *avfi);

/* readdir */

struct pseudo_dirent {
    uint32_t d_namlen;
};

int
fuse_internal_readdir(vnode_t                 vp,
                      uio_t                   uio,
                      vfs_context_t           context,
                      struct fuse_filehandle *fufh,
                      struct fuse_iov        *cookediov);

int
fuse_internal_readdir_processdata(vnode_t          vp,
                                  uio_t            uio,
                                  size_t           reqsize,
                                  void            *buf,
                                  size_t           bufsize,
                                  struct fuse_iov *cookediov);

/* remove */

int
fuse_internal_remove(vnode_t               dvp,
                     vnode_t               vp,
                     struct componentname *cnp,
                     enum fuse_opcode      op,
                     vfs_context_t         context);

/* rename */

int
fuse_internal_rename(vnode_t               fdvp,
                     vnode_t               fvp,
                     struct componentname *fcnp,
                     vnode_t               tdvp,
                     vnode_t               tvp,
                     struct componentname *tcnp,
                     vfs_context_t         context);

/* revoke */

int
fuse_internal_revoke(vnode_t vp, int flags, vfs_context_t context);

/* strategy */

int
fuse_internal_strategy(vnode_t vp, buf_t bp);

errno_t
fuse_internal_strategy_buf(struct vnop_strategy_args *ap);


/* xattr */

/*
 * By default, we compile to have these attributes /not/ handled by the
 * user-space file system. Feel free to change this and recompile.
 */
static __inline__
int
fuse_is_shortcircuit_xattr(const char *name)
{
    if (bcmp(name, XATTR_FINDERINFO_NAME, sizeof(XATTR_FINDERINFO_NAME)) == 0) {
        return 1;
    }

    if (bcmp(name, XATTR_RESOURCEFORK_NAME,
             sizeof(XATTR_RESOURCEFORK_NAME)) == 0) {
        return 1;
    }

    if (bcmp(name, KAUTH_FILESEC_XATTR, sizeof(KAUTH_FILESEC_XATTR)) == 0) {
        return 1;
    }

    return 0;
}


/* entity creation */

static __inline__
int
fuse_internal_checkentry(struct fuse_entry_out *feo, enum vtype vtype)
{
    debug_printf("feo=%p, vtype=%d\n", feo, vtype);

    if (vtype != IFTOVT(feo->attr.mode)) {
        debug_printf("EINVAL -- %x != %x\n", vtype, IFTOVT(feo->attr.mode));
        return (EINVAL);
    }

    if (feo->nodeid == FUSE_NULL_ID) {
        debug_printf("EINVAL -- feo->nodeid is NULL\n");
        return (EINVAL);
    }

    if (feo->nodeid == FUSE_ROOT_ID) {
        debug_printf("EINVAL -- feo->nodeid is FUSE_ROOT_ID\n");
        return (EINVAL);
    }

    return (0);
}

int
fuse_internal_newentry(vnode_t               dvp,
                       vnode_t              *vpp,
                       struct componentname *cnp,
                       enum fuse_opcode      op,
                       void                 *buf,
                       size_t                bufsize,
                       enum vtype            vtype,
                       vfs_context_t         context);

void
fuse_internal_newentry_makerequest(mount_t                 mp,
                                   uint64_t                dnid,
                                   struct componentname   *cnp,
                                   enum fuse_opcode        op,
                                   void                   *buf,
                                   size_t                  bufsize,
                                   struct fuse_dispatcher *fdip,
                                   vfs_context_t           context);

int
fuse_internal_newentry_core(vnode_t                 dvp,
                            vnode_t                *vpp,
                            struct componentname   *cnp,
                            enum vtype              vtyp,
                            struct fuse_dispatcher *fdip,
                            vfs_context_t           context);

/* entity destruction */

int
fuse_internal_forget_callback(struct fuse_ticket *ftick, uio_t uio);        

void
fuse_internal_forget_send(mount_t                 mp,
                          vfs_context_t           context,
                          uint64_t                nodeid,
                          uint64_t                nlookup,
                          struct fuse_dispatcher *fdip);

void
fuse_internal_interrupt_send(struct fuse_ticket *ftick);

void
fuse_internal_vnode_disappear(vnode_t vp, vfs_context_t context, int dorevoke);

/* fuse start/stop */

int fuse_internal_init_callback(struct fuse_ticket *ftick, uio_t uio);
void fuse_internal_send_init(struct fuse_data *data, vfs_context_t context);
void fuse_internal_thread_call_expiry_handler(void *param0, void *param1);

/* miscellaneous */

#define fuse_isdeadfs_nop(vp) 0

static __inline__
int
fuse_isdeadfs_mp(mount_t mp)
{
    return (fuse_get_mpdata(mp)->dataflags & FSESS_KICK);
}

static __inline__
int
fuse_isdeadfs(vnode_t vp)
{
    return (fuse_isdeadfs_mp(vnode_mount(vp)));
}

static __inline__
int
fuse_isdirectio(vnode_t vp)
{
    /* Try global first. */
    if (fuse_get_mpdata(vnode_mount(vp))->dataflags & FSESS_DIRECT_IO) {
        return 1;
    }

    return (VTOFUD(vp)->flag & FN_DIRECT_IO);
}

static __inline__
int
fuse_isdirectio_mp(mount_t mp)
{
    return (fuse_get_mpdata(mp)->dataflags & FSESS_DIRECT_IO);
}

#if M_MACFUSE_EXPERIMENTAL_JUNK
static __inline__
int
fuse_isnoattrcache(vnode_t vp)
{
    /* Try global first. */
    if (fuse_get_mpdata(vnode_mount(vp))->dataflags & FSESS_NO_ATTRCACHE) {
        return 1;
    }

    return 0;
}

static __inline__
int
fuse_isnoattrcache_mp(mount_t mp)
{
    return (fuse_get_mpdata(mp)->dataflags & FSESS_NO_ATTRCACHE);
}
#endif

static __inline__
int
fuse_isnoreadahead(vnode_t vp)
{
    /* Try global first. */
    if (fuse_get_mpdata(vnode_mount(vp))->dataflags & FSESS_NO_READAHEAD) {
        return 1;
    }
    
    /* In our model, direct_io implies no readahead. */
    return fuse_isdirectio(vp);
}

static __inline__
int
fuse_isnosynconclose(vnode_t vp)
{
    if (fuse_isdirectio(vp)) {
        return 0;
    }

    return (fuse_get_mpdata(vnode_mount(vp))->dataflags & FSESS_NO_SYNCONCLOSE);
}

static __inline__
int
fuse_isnosyncwrites_mp(mount_t mp)
{
    /* direct_io implies we won't have nosyncwrites. */
    if (fuse_isdirectio_mp(mp)) {
        return 0;
    }

    return (fuse_get_mpdata(mp)->dataflags & FSESS_NO_SYNCWRITES);
}

static __inline__
void
fuse_setnosyncwrites_mp(mount_t mp)
{
    vfs_clearflags(mp, MNT_SYNCHRONOUS);
    vfs_setflags(mp, MNT_ASYNC);
    fuse_get_mpdata(mp)->dataflags |= FSESS_NO_SYNCWRITES;
}

static __inline__
void
fuse_clearnosyncwrites_mp(mount_t mp)
{
    if (!vfs_issynchronous(mp)) {
        vfs_clearflags(mp, MNT_ASYNC);
        vfs_setflags(mp, MNT_SYNCHRONOUS);
        fuse_get_mpdata(mp)->dataflags &= ~FSESS_NO_SYNCWRITES;
    }
}

static __inline__
int
fuse_isnoubc(vnode_t vp)
{
    /* Try global first. */
    if (fuse_get_mpdata(vnode_mount(vp))->dataflags & FSESS_NO_UBC) {
        return 1;
    }
    
    /* In our model, direct_io implies no UBC. */
    return fuse_isdirectio(vp);
}

static __inline__
int
fuse_isnoubc_mp(mount_t mp)
{
    return (fuse_get_mpdata(mp)->dataflags & FSESS_NO_UBC);
}

static __inline__
int
fuse_isnovncache(vnode_t vp)
{
    /* Try global first. */
    if (fuse_get_mpdata(vnode_mount(vp))->dataflags & FSESS_NO_VNCACHE) {
        return 1;
    }
    
    /* In our model, direct_io implies no vncache for this vnode. */
    return fuse_isdirectio(vp);
}

static __inline__
int
fuse_isnovncache_mp(mount_t mp)
{
    return (fuse_get_mpdata(mp)->dataflags & FSESS_NO_VNCACHE);
}

static __inline__
int
fuse_isextendedsecurity(vnode_t vp)
{
    return (fuse_get_mpdata(vnode_mount(vp))->dataflags & \
            FSESS_EXTENDED_SECURITY);
}

static __inline__
int
fuse_isextendedsecurity_mp(mount_t mp)
{
    return (fuse_get_mpdata(mp)->dataflags & FSESS_EXTENDED_SECURITY);
}

static __inline__
uint32_t
fuse_round_powerof2(uint32_t size)
{
    uint32_t result = 512;

    while (result < size) {
        result <<= 1;
    }

    return result;
}

static __inline__
uint32_t
fuse_round_size(uint32_t size, uint32_t b_min, uint32_t b_max)
{
    uint32_t candidate = fuse_round_powerof2(size);

    /* We assume that b_min and b_max will already be powers of 2. */

    if (candidate < b_min) {
        candidate = b_min;
    }

    if (candidate > b_max) {
        candidate = b_max;
    }

    return candidate;
}

static __inline__
int
fuse_skip_apple_special_mp(mount_t mp, char *nameptr, long namelen)
{
#define DS_STORE ".DS_Store"
    int ismpoption = fuse_get_mpdata(mp)->dataflags & FSESS_NO_APPLESPECIAL;

    if (ismpoption && nameptr) {
        /* This _will_ allow just "._", that is, a namelen of 2. */
        if (namelen > 2) {
            if (namelen == ((sizeof(DS_STORE)/sizeof(char)) - 1)) {
                if (bcmp(nameptr, DS_STORE, sizeof(DS_STORE)) == 0) {
                    return 1;
                }
            } else if (nameptr[0] == '.' && nameptr[1] == '_') {
                return 1;
            }
        }
    }

    return 0;
}

static __inline__
int
fuse_blanket_deny(vnode_t vp, vfs_context_t context)
{
    mount_t mp = vnode_mount(vp);
    struct fuse_data *data = fuse_get_mpdata(mp);
    int issuser = fuse_vfs_context_issuser(context);
    int isvroot = vnode_isvroot(vp);

    /* if allow_other is set */
    if (data->dataflags & FSESS_ALLOW_OTHER) {
        return 0;
    }

    /* if allow_root is set */
    if (issuser && (data->dataflags & FSESS_ALLOW_ROOT)) {
        return 0;
    }

    /* if this is the user who mounted the fs */
    if (fuse_match_cred(data->daemoncred, vfs_context_ucred(context)) == 0) {
        return 0;
    }

    if (!(data->dataflags & FSESS_INITED) && isvroot && issuser) {
        return 0;
    }

    if (fuse_isdeadfs(vp) && isvroot) {
        return 0;
    }

    return 1;
}

#define CHECK_BLANKET_DENIAL(vp, context, err) \
    { \
        if (fuse_blanket_deny(vp, context)) { \
            return err; \
        } \
    }

#endif /* _FUSE_INTERNAL_H_ */
