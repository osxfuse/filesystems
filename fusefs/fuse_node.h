/*
 * Copyright (C) 2006 Google. All Rights Reserved.
 * Amit Singh <singh@>
 */

#ifndef _FUSE_NODE_H_
#define _FUSE_NODE_H_

#include "fuse_file.h"
#include "fuse_knote.h"
#include "fuse_node.h"
#include "fuse_nodehash.h"
#include <fuse_param.h>

extern errno_t (**fuse_vnode_operations)(void *);

enum {
    kFSNodeMagic    = 'FUSE',
    kFSNodeBadMagic = 'FU**',
    kHNodeMagic     = 'HNOD',
};

#define FN_CREATING  0x00000001
#define FN_REVOKING  0x00000002
#define FN_DIRECT_IO 0x00000004
#define FN_NULL_ACL  0x00000008
#define FN_HAS_ACL   0x00000010

struct fuse_vnode_data {
    uint32_t   fMagic;
    boolean_t  fInitialised;
    uint64_t   nid;
    uint64_t   nlookup;
    enum vtype vtype;
    uint64_t   parent_nid;

    lck_mtx_t *createlock;
    void      *creator;
    /* XXX: Clean up this multi-flag nonsense. Really. */
    int        flag;
    int        flags;
    uint32_t   c_flag;

#if M_MACFUSE_ENABLE_UNSUPPORTED
    struct klist c_knotes;
#endif /* M_MACFUSE_ENABLE_UNSUPPORTED */

    /*
     * The nodelock must be held when data in the FUSE node is accessed or
     * modified. Typically, we would take this lock at the beginning of a
     * vnop and drop it at the end of the vnop.
     */
    lck_rw_t  *nodelock;
    void      *nodelockowner;

    /*
     * The truncatelock guards against the EOF changing on us (that is, a
     * file resize) unexpectedly.
     */
    lck_rw_t  *truncatelock;

    vnode_t    c_vp;
    vnode_t    parent;
    off_t      filesize; 

    struct     fuse_filehandle fufh[FUFH_MAXTYPE];

    struct vnode_attr cached_attrs;
    struct timespec   cached_attrs_valid;
};
typedef struct fuse_vnode_data * fusenode_t;

#define VTOFUD(vp) \
    ((struct fuse_vnode_data *)FSNodeGenericFromHNode(vnode_fsnode(vp)))
#define VTOI(vp)    (VTOFUD(vp)->nid)
#define VTOVA(vp)   (&(VTOFUD(vp)->cached_attrs))
#define VTOILLU(vp) ((unsigned long long)(VTOFUD(vp) ? VTOI(vp) : 0))

#define FUSE_NULL_ID 0

static __inline__
void
fuse_invalidate_attr(vnode_t vp)
{
    if (VTOFUD(vp)) {
        bzero(&VTOFUD(vp)->cached_attrs_valid, sizeof(struct timespec));
    }
}

void fuse_vnode_init(vnode_t vp, struct fuse_vnode_data *fvdat,
                     uint64_t nodeid, enum vtype vtyp, uint64_t parentid);
void fuse_vnode_ditch(vnode_t vp, vfs_context_t context);
void fuse_vnode_teardown(vnode_t vp, vfs_context_t context, enum vtype vtyp);

enum vget_mode { VG_NORMAL, VG_WANTNEW, VG_FORCENEW };

struct get_filehandle_param {
    enum fuse_opcode opcode;
    uint8_t          do_gc:1;
    uint8_t          do_new:1;
    int explicitidentity;
    pid_t pid;
    uid_t uid;
    gid_t gid;
};

#define C_NEED_RVNODE_PUT    0x00001
#define C_NEED_DVNODE_PUT    0x00002
#define C_ZFWANTSYNC         0x00004
#define C_FROMSYNC           0x00008
#define C_MODIFIED           0x00010
#define C_NOEXISTS           0x00020
#define C_DELETED            0x00040
#define C_HARDLINK           0x00080
#define C_FORCEUPDATE        0x00100
#define C_HASXATTRS          0x00200
#define C_NEED_DATA_SETSIZE  0x01000
#define C_NEED_RSRC_SETSIZE  0x02000

#define C_CREATING           0x04000
#define C_ACCESS_NOOP        0x08000

errno_t
FSNodeGetOrCreateFileVNodeByID(mount_t       mp,
                               vfs_context_t context,
                               uint64_t      nodeid,
                               vnode_t       dvp,
                               enum vtype    vtyp,
                               uint64_t      insize,
                               vnode_t      *vnPtr,
                               int           flags,
                               int          *oflags);

void FSNodeScrub(struct fuse_vnode_data *fvdat);

int
fuse_vget_i(mount_t               mp,
            uint64_t              nodeid,
            vfs_context_t         context,
            vnode_t               dvp,
            vnode_t              *vpp,
            struct componentname *cnp,
            enum vtype            vtyp,
            uint64_t              size,
            enum vget_mode        mode,
            uint64_t              parentid);

/* Name cache wrappers */

static __inline__
void
fuse_vncache_enter(vnode_t dvp, vnode_t vp, struct componentname *cnp)
{
#if FUSE_TRACE_VNCACHE
    IOLog("cache enter: dvp=%p, vp=%p, %s\n", dvp, vp, cnp->cn_nameptr);
#endif
    return cache_enter(dvp, vp, cnp);
}

static __inline__
void
fuse_vncache_purge(vnode_t vp)
{
#if FUSE_TRACE_VNCACHE
    IOLog("cache purge: vp=%p\n", vp);
#endif
    return cache_purge(vp);
}

static __inline__
int
fuse_vncache_lookup(vnode_t dvp, vnode_t *vpp, struct componentname *cnp)
{
    int ret = cache_lookup(dvp, vpp, cnp);
#if FUSE_TRACE_VNCACHE
    IOLog("cache lookup: ret=%d, dvp=%p, *vpp=%p, %s\n",
          ret, dvp, *vpp, cnp->cn_nameptr);
#endif
    return ret;
}

#endif /* _FUSE_NODE_H_ */
