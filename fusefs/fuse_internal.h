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
#include <sys/uio.h>
#include <sys/vnode.h>

#include "fuse_ipc.h"
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
fuse_internal_attr_fat2vat(mount_t            mp,
                           struct fuse_attr  *fat,
                           struct vnode_attr *vap)
{
    struct timespec t;
    struct fuse_data *data;

    debug_printf("mp=%p, fat=%p, vap=%p\n", mp, fat, vap);

    VATTR_INIT(vap);

    VATTR_RETURN(vap, va_fsid, vfs_statfs(mp)->f_fsid.val[0]);

    VATTR_RETURN(vap, va_fileid, fat->ino);
    VATTR_RETURN(vap, va_linkid, fat->ino);
    VATTR_RETURN(vap, va_data_size, fat->size);
    VATTR_RETURN(vap, va_total_alloc, fat->blocks * S_BLKSIZE);

    t.tv_sec = fat->atime; t.tv_nsec = fat->atimensec;
    VATTR_RETURN(vap, va_access_time, t);

    t.tv_sec = fat->ctime; t.tv_nsec = fat->ctimensec;
    VATTR_RETURN(vap, va_change_time, t);

    t.tv_sec = fat->mtime; t.tv_nsec = fat->mtimensec;
    VATTR_RETURN(vap, va_modify_time, t);

    VATTR_RETURN(vap, va_mode, fat->mode & ~S_IFMT);
    VATTR_RETURN(vap, va_nlink, fat->nlink);
    VATTR_RETURN(vap, va_uid, fat->uid); // or 99 for experiments
    VATTR_RETURN(vap, va_gid, fat->gid); // or 99 for experiments
    VATTR_RETURN(vap, va_rdev, fat->rdev);

    VATTR_RETURN(vap, va_type, IFTOVT(fat->mode));

    data = fusefs_get_data(mp);
    VATTR_RETURN(vap, va_iosize, data->iosize);

    VATTR_RETURN(vap, va_flags, 0);
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
    fuse_internal_attr_fat2vat(vnode_mount(vp), &(fuse_out)->attr, VTOVA(vp)); \
} while (0)

/* fsync */

int
fuse_internal_fsync(vnode_t                 vp,
                    vfs_context_t           context,
                    struct fuse_filehandle *fufh,
                    void                   *param);

int
fuse_internal_fsync_callback(struct fuse_ticket *tick, uio_t uio);

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
fuse_internal_readdir_processdata(uio_t            uio,
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
                     struct componentname *fcnp,
                     vnode_t               tdvp,
                     struct componentname *tcnp,
                     vfs_context_t         context);

/* strategy */

int
fuse_internal_strategy(vnode_t vp, buf_t bp);

errno_t
fuse_internal_strategy_buf(struct vnop_strategy_args *ap);


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
fuse_internal_forget_callback(struct fuse_ticket *tick, uio_t uio);        

void
fuse_internal_forget_send(mount_t                 mp,
                          vfs_context_t           context,
                          uint64_t                nodeid,
                          uint64_t                nlookup,
                          struct fuse_dispatcher *fdip);

/* fuse start/stop */

int fuse_internal_init_callback(struct fuse_ticket *tick, uio_t uio);
void fuse_internal_send_init(struct fuse_data *data, vfs_context_t context);

/* miscellaneous */

#define fuse_isdeadfs_nop(vp) 0

static __inline__
int
fuse_isdeadfs(vnode_t vp)
{
    mount_t mp = vnode_mount(vp);
    struct fuse_data *data = fusefs_get_data(mp);

    return (data->dataflag & FSESS_KICK);
}

static __inline__
int
fuse_isdeadfs_mp(mount_t mp)
{
    struct fuse_data *data = fusefs_get_data(mp);

    return (data->dataflag & FSESS_KICK);
}

#endif /* _FUSE_INTERNAL_H_ */
