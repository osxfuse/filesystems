/*
 * Copyright (C) 2006 Google. All Rights Reserved.
 * Amit Singh <singh@>
 */

#include "fuse.h"
#include "fuse_device.h"
#include "fuse_internal.h"
#include "fuse_ipc.h"
#include "fuse_locking.h"
#include "fuse_node.h"
#include "fuse_vfsops.h"

#include <fuse_mount.h>

static const struct timespec kZeroTime = {0, 0};

vfstable_t fuse_vfs_table_ref = NULL;

errno_t (**fuse_vnode_operations)(void *);

static struct vnodeopv_desc fuse_vnode_operation_vector_desc = {
    &fuse_vnode_operations,      // opv_desc_vector_p
    fuse_vnode_operation_entries // opv_desc_ops
};

static struct vnodeopv_desc *fuse_vnode_operation_vector_desc_list[1] =
{
    &fuse_vnode_operation_vector_desc
};

static struct vfsops fuse_vfs_ops = {
    fuse_vfs_mount,   // vfs_mount
    NULL,             // vfs_start
    fuse_vfs_unmount, // vfs_unmount
    fuse_vfs_root,    // vfs_root
    NULL,             // vfs_quotactl
    fuse_vfs_getattr, // vfs_getattr
    fuse_vfs_sync,    // vfs_sync
    NULL,             // vfs_vget
    NULL,             // vfs_fhtovp
    NULL,             // vfs_vptofh
    NULL,             // vfs_init
    NULL,             // vfs_sysctl
    fuse_vfs_setattr, // vfs_setattr
    { NULL, NULL, NULL, NULL, NULL, NULL, NULL } // vfs_reserved[]
};

struct vfs_fsentry fuse_vfs_entry = {

    // VFS operations
    &fuse_vfs_ops,

    // Number of vnodeopv_desc being registered
    (sizeof(fuse_vnode_operation_vector_desc_list) /\
        sizeof(*fuse_vnode_operation_vector_desc_list)),

    // The vnodeopv_desc's
    fuse_vnode_operation_vector_desc_list,

    // File system type number
    0,

    // File system type name
    "fusefs",

    // Flags specifying file system capabilities
    // VFS_TBLTHREADSAFE | VFS_TBLFSNODELOCK | VFS_TBLNOTYPENUM,
    VFS_TBLNOTYPENUM,

    // Reserved for future use
    { NULL, NULL }
};

static errno_t
fuse_vfs_mount(mount_t       mp,
               vnode_t       devvp,
               user_addr_t   udata,
               vfs_context_t context)
{
    size_t len;

    int err               = 0;
    int mntopts           = 0;
    int max_read_set      = 0;
    unsigned int max_read = ~0;

    struct fuse_softc *fdev;
    struct fuse_data  *data;
    fuse_mount_args    fusefs_args;

    fuse_trace_printf_vfsop();

    err = copyin(udata, &fusefs_args, sizeof(fusefs_args));
    if (err) {
        debug_printf("copyin failed\n");
        return EINVAL;
    }

    // Interesting flags that we can receive from mount or may want to forcibly
    // set include:
    //
    // MNT_RDONLY
    // MNT_SYNCHRONOUS
    // MNT_NOEXEC
    // MNT_NOSUID
    // MNT_NODEV
    // MNT_UNION
    // MNT_ASYNC
    // MNT_DONTBROWSE
    // MNT_IGNORE_OWNERSHIP
    // MNT_AUTOMOUNTED
    // MNT_JOURNALED
    // MNT_NOUSERXATTR
    // MNT_DEFWRITE

    err = ENOTSUP;

    if ((fusefs_args.daemon_timeout > FUSE_MAX_DAEMON_TIMEOUT) ||
        (fusefs_args.daemon_timeout < FUSE_MIN_DAEMON_TIMEOUT)) {
        return EINVAL;
    }

    if (fusefs_args.altflags & FUSE_MOPT_NO_BROWSE) {
        vfs_setflags(mp, MNT_DONTBROWSE);
    }

    if (fusefs_args.altflags & FUSE_MOPT_NO_SYNCWRITES) {
        if (fusefs_args.altflags &
            (FUSE_MOPT_NO_UBC | FUSE_MOPT_NO_READAHEAD)) {
            return EINVAL;
        }
        vfs_clearflags(mp, MNT_SYNCHRONOUS);
        vfs_setflags(mp, MNT_ASYNC);
        mntopts |= FSESS_NO_SYNCWRITES;
    } else {
        vfs_clearflags(mp, MNT_ASYNC);
        vfs_setflags(mp, MNT_SYNCHRONOUS);
    }

    if (!(fusefs_args.altflags & FUSE_MOPT_NO_AUTH_OPAQUE)) {
        // This sets MNTK_AUTH_OPAQUE in the mount point's mnt_kern_flag.
        vfs_setauthopaque(mp);
        err = 0;
    }

    if (!(fusefs_args.altflags & FUSE_MOPT_NO_AUTH_OPAQUE_ACCESS)) {
        // This sets MNTK_AUTH_OPAQUE_ACCESS in the mount point's mnt_kern_flag.
        vfs_setauthopaqueaccess(mp);
        err = 0;
    }

    if (vfs_isupdate(mp)) {
        return err;
    }

    err = 0;

    vfs_setfsprivate(mp, NULL);

    if ((fusefs_args.index < 0) || (fusefs_args.index >= FUSE_NDEVICES)) {
        return EINVAL;
    }
    fdev = fuse_softc_get(fusefs_args.rdev);

    FUSE_LOCK();
    {
        data = fuse_softc_get_data(fdev);
        if (data && (data->dataflag & FSESS_OPENED)) {
            data->mntco++;
            debug_printf("a.inc:mntco = %d\n", data->mntco);
        } else {
            FUSE_UNLOCK();
            return (ENXIO);
        }    
    }
    FUSE_UNLOCK();

    if (fusefs_args.altflags & FUSE_MOPT_JAIL_SYMLINKS) {
        mntopts |= FSESS_PUSH_SYMLINKS_IN;
    }

    if (fusefs_args.altflags & FUSE_MOPT_ALLOW_OTHER) {
        mntopts |= FSESS_DAEMON_CAN_SPY;
    }

    if (fusefs_args.altflags & FUSE_MOPT_DEFAULT_PERMISSIONS) {
        mntopts |= FSESS_DEFAULT_PERMISSIONS;
    }

    if (fusefs_args.altflags & FUSE_MOPT_NO_ATTRCACHE) {
        mntopts |= FSESS_NO_ATTRCACHE;
    }

    if (fusefs_args.altflags & FUSE_MOPT_NO_READAHEAD) {
        mntopts |= FSESS_NO_READAHEAD;
    }

    if (fusefs_args.altflags & FUSE_MOPT_NO_UBC) {
        mntopts |= FSESS_NO_UBC;
    }

    max_read_set = 0;

    if (fdata_kick_get(data)) {
        err = ENOTCONN;
    }

    if ((mntopts & FSESS_DAEMON_CAN_SPY) &&
        !fuse_vfs_context_issuser(context)) {
        debug_printf("only root can use \"allow_other\"\n");
        err = EPERM;
    }

    if (err) {
        goto out;
    }

    if (!data->daemoncred) {
        panic("MacFUSE: daemon found but identity unknown");
    }

    if (data->mpri != FM_NOMOUNTED) {
        debug_printf("already mounted\n");
        err = EALREADY;
        goto out;
    }

    if (fuse_vfs_context_issuser(context) &&
        vfs_context_ucred(context)->cr_uid != data->daemoncred->cr_uid) {
        debug_printf("not allowed to do the first mount\n");
        err = EPERM;
        goto out;
    }

    if ((fusefs_args.altflags & FUSE_MOPT_FSID) && (fusefs_args.fsid != 0)) {
        fsid_t   fsid;
        mount_t  other_mp;
        uint32_t target_dev;

        target_dev = FUSE_MAKEDEV(FUSE_CUSTOM_FSID_DEVICE_MAJOR,
                                  fusefs_args.fsid);

        fsid.val[0] = target_dev;
        fsid.val[1] = FUSE_CUSTOM_FSID_VAL1;

        other_mp = vfs_getvfs(&fsid);
        if (other_mp != NULL) {
            err = EPERM;
            goto out;
        }

        vfs_statfs(mp)->f_fsid.val[0] = target_dev;
        vfs_statfs(mp)->f_fsid.val[1] = FUSE_CUSTOM_FSID_VAL1;

    } else {
        vfs_getnewfsid(mp);    
    }

    data->blocksize = fusefs_args.blocksize;
    data->dataflag |= mntopts;
    data->daemon_timeout.tv_sec =  fusefs_args.daemon_timeout;
    data->daemon_timeout.tv_nsec = 0;
    if (data->daemon_timeout.tv_sec) {
        data->daemon_timeout_p = &(data->daemon_timeout);
    } else {
        data->daemon_timeout_p = (struct timespec *)0;
    }
    data->fdev = fdev;
    data->iosize = fusefs_args.iosize;
    data->max_read = max_read;
    data->mp = mp;
    data->mpri = FM_PRIMARY;
    data->subtype = fusefs_args.subtype;

    if (data->blocksize < FUSE_MIN_BLOCKSIZE) {
        data->blocksize = FUSE_MIN_BLOCKSIZE;
    }
    if (data->blocksize > FUSE_MAX_BLOCKSIZE) {
        data->blocksize = FUSE_MAX_BLOCKSIZE;
    }

    if (data->iosize < FUSE_MIN_IOSIZE) {
        data->iosize = FUSE_MIN_IOSIZE;
    }
    if (data->iosize > FUSE_MAX_IOSIZE) {
        data->iosize = FUSE_MAX_IOSIZE;
    }

    if (data->iosize < data->blocksize) {
        data->iosize = data->blocksize;
    }

    vfs_setfsprivate(mp, data);
    
    /*
     * In what looks like an oversight, Apple does not export this routine.
     * Hopefully they will fix it in a revision. We need to do this so that
     * advisory locking is handled by the VFS.
     */
    // vfs_setlocklocal(mp);

    copystr(fusefs_args.fsname, vfs_statfs(mp)->f_mntfromname,
            MNAMELEN - 1, &len);
    bzero(vfs_statfs(mp)->f_mntfromname + len, MNAMELEN - len);

    copystr(fusefs_args.volname, data->volname, MAXPATHLEN - 1, &len);
    bzero(data->volname + len, MAXPATHLEN - len);

    // Handshake with the daemon
    fuse_internal_send_init(data, context);

out:
    if (err) {
        data->mntco--;
        debug_printf("b.dec: mntco=%d\n", data->mntco);

        FUSE_LOCK();
        if ((data->mntco == 0) && !(data->dataflag & FSESS_OPENED)) {
            fuse_softc_set_data(fdev, NULL);
            fdata_destroy(data);
        }
        FUSE_UNLOCK();
    }

    return (err);
}

static errno_t
fuse_vfs_unmount(mount_t mp, int mntflags, vfs_context_t context)
{
    int err   = 0;
    int flags = 0;

    struct fuse_data      *data;
    struct fuse_softc     *fdev;
    struct fuse_dispatcher fdi;

    fuse_trace_printf_vfsop();

    if (mntflags & MNT_FORCE) {
        flags |= FORCECLOSE;
    }

    data = fusefs_get_data(mp);
    if (!data) {
        panic("MacFUSE: no private data for mount point?");
    }

    if (fdata_kick_get(data)) {
        flags |= FORCECLOSE;
    } else if (!(data->dataflag & FSESS_INITED)) {
        flags |= FORCECLOSE;
        fdata_kick_set(data);
    }

    err = vflush(mp, NULLVP, flags);
    if (err) {
        debug_printf("vflush failed");
        return (err);
    }

    if (fdata_kick_get(data)) {
        goto alreadydead;
    }

    fdisp_init(&fdi, 0 /* no data to send along */);
    fdisp_make(&fdi, FUSE_DESTROY, mp, 0, context);
    err = fdisp_wait_answ(&fdi);
    if (!err) {
        fuse_ticket_drop(fdi.tick);
    }

#if 0
    vfs_event_signal(&vfs_statfs(data->mp)->f_fsid, VQ_UNMOUNT, 0);
#endif

    fdata_kick_set(data);

alreadydead:

    data->mpri = FM_NOMOUNTED;
    data->mntco--;
    FUSE_LOCK();
    fdev = data->fdev;
    if (data->mntco == 0 && !(data->dataflag & FSESS_OPENED)) {
        fuse_softc_set_data(fdev, NULL);
        fdata_destroy(data);
    }
    FUSE_UNLOCK();

    vfs_setfsprivate(mp, NULL);

    return (0);
}        

static errno_t
fuse_vfs_root(mount_t mp, struct vnode **vpp, vfs_context_t context)
{
    int err = 0;
    vnode_t vp = NULL;

    fuse_trace_printf_vfsop();

    err = FSNodeGetOrCreateFileVNodeByID(mp,             // mount
                                         FUSE_ROOT_ID,   // node id
                                         NULLVP,         // parent
                                         VDIR,           // type
                                         FUSE_ROOT_SIZE, // size
                                         &vp,            // ptr
                                         0);             // flags

    *vpp = vp;

    return (err);
}

static void
handle_capabilities_and_attributes(struct vfs_attr *attr)
{
    attr->f_capabilities.capabilities[VOL_CAPABILITIES_FORMAT] = 0
//      | VOL_CAP_FMT_PERSISTENTOBJECTIDS
        | VOL_CAP_FMT_SYMBOLICLINKS
        | VOL_CAP_FMT_HARDLINKS
//      | VOL_CAP_FMT_JOURNAL
//      | VOL_CAP_FMT_JOURNAL_ACTIVE
        | VOL_CAP_FMT_NO_ROOT_TIMES
//      | VOL_CAP_FMT_SPARSE_FILES
//      | VOL_CAP_FMT_ZERO_RUNS
        | VOL_CAP_FMT_CASE_SENSITIVE
//      | VOL_CAP_FMT_CASE_PRESERVING
        | VOL_CAP_FMT_FAST_STATFS
//      | VOL_CAP_FMT_2TB_FILESIZE
        ;
    attr->f_capabilities.valid[VOL_CAPABILITIES_FORMAT] = 0
        | VOL_CAP_FMT_PERSISTENTOBJECTIDS
        | VOL_CAP_FMT_SYMBOLICLINKS
        | VOL_CAP_FMT_HARDLINKS
        | VOL_CAP_FMT_JOURNAL
        | VOL_CAP_FMT_JOURNAL_ACTIVE
        | VOL_CAP_FMT_NO_ROOT_TIMES
        | VOL_CAP_FMT_SPARSE_FILES
        | VOL_CAP_FMT_ZERO_RUNS
        | VOL_CAP_FMT_CASE_SENSITIVE
        | VOL_CAP_FMT_CASE_PRESERVING
        | VOL_CAP_FMT_FAST_STATFS
        | VOL_CAP_FMT_2TB_FILESIZE
        ;
    attr->f_capabilities.capabilities[VOL_CAPABILITIES_INTERFACES] = 0
//      | VOL_CAP_INT_SEARCHFS
//      | VOL_CAP_INT_ATTRLIST
//      | VOL_CAP_INT_NFSEXPORT
//      | VOL_CAP_INT_READDIRATTR
//      | VOL_CAP_INT_EXCHANGEDATA
//      | VOL_CAP_INT_COPYFILE
//      | VOL_CAP_INT_ALLOCATE
        | VOL_CAP_INT_VOL_RENAME
//      | VOL_CAP_INT_ADVLOCK
//      | VOL_CAP_INT_FLOCK
//      | VOL_CAP_INT_EXTENDED_SECURITY
//      | VOL_CAP_INT_USERACCESS
        ;
    attr->f_capabilities.valid[VOL_CAPABILITIES_INTERFACES] = 0
        | VOL_CAP_INT_SEARCHFS
        | VOL_CAP_INT_ATTRLIST
        | VOL_CAP_INT_NFSEXPORT
        | VOL_CAP_INT_READDIRATTR
        | VOL_CAP_INT_EXCHANGEDATA
        | VOL_CAP_INT_COPYFILE
        | VOL_CAP_INT_ALLOCATE
        | VOL_CAP_INT_VOL_RENAME
        | VOL_CAP_INT_ADVLOCK
        | VOL_CAP_INT_FLOCK
        | VOL_CAP_INT_EXTENDED_SECURITY
        | VOL_CAP_INT_USERACCESS
        ;

    attr->f_capabilities.capabilities[VOL_CAPABILITIES_RESERVED1] = 0;
    attr->f_capabilities.valid[VOL_CAPABILITIES_RESERVED1] = 0;
    attr->f_capabilities.capabilities[VOL_CAPABILITIES_RESERVED2] = 0;
    attr->f_capabilities.valid[VOL_CAPABILITIES_RESERVED2] = 0;
    VFSATTR_SET_SUPPORTED(attr, f_capabilities);
    
    attr->f_attributes.validattr.commonattr = 0
        | ATTR_CMN_NAME
        | ATTR_CMN_DEVID
        | ATTR_CMN_FSID
        | ATTR_CMN_OBJTYPE
//      | ATTR_CMN_OBJTAG
        | ATTR_CMN_OBJID
//      | ATTR_CMN_OBJPERMANENTID
        | ATTR_CMN_PAROBJID
//      | ATTR_CMN_SCRIPT
//      | ATTR_CMN_CRTIME
//      | ATTR_CMN_MODTIME
//      | ATTR_CMN_CHGTIME
//      | ATTR_CMN_ACCTIME
//      | ATTR_CMN_BKUPTIME
//      | ATTR_CMN_FNDRINFO
        | ATTR_CMN_OWNERID
        | ATTR_CMN_GRPID
        | ATTR_CMN_ACCESSMASK
//      | ATTR_CMN_FLAGS
//      | ATTR_CMN_USERACCESS
//      | ATTR_CMN_EXTENDED_SECURITY
//      | ATTR_CMN_UUID
//      | ATTR_CMN_GRPUUID
        ;
    attr->f_attributes.validattr.volattr = 0
        | ATTR_VOL_FSTYPE
        | ATTR_VOL_SIGNATURE
        | ATTR_VOL_SIZE
        | ATTR_VOL_SPACEFREE
        | ATTR_VOL_SPACEAVAIL
//      | ATTR_VOL_MINALLOCATION
//      | ATTR_VOL_ALLOCATIONCLUMP
        | ATTR_VOL_IOBLOCKSIZE
//      | ATTR_VOL_OBJCOUNT
        | ATTR_VOL_FILECOUNT
//      | ATTR_VOL_DIRCOUNT
//      | ATTR_VOL_MAXOBJCOUNT
        | ATTR_VOL_MOUNTPOINT
        | ATTR_VOL_NAME
        | ATTR_VOL_MOUNTFLAGS
        | ATTR_VOL_MOUNTEDDEVICE
//      | ATTR_VOL_ENCODINGSUSED
        | ATTR_VOL_CAPABILITIES
        | ATTR_VOL_ATTRIBUTES
        ;
    attr->f_attributes.validattr.dirattr = 0
        | ATTR_DIR_LINKCOUNT
//      | ATTR_DIR_ENTRYCOUNT
//      | ATTR_DIR_MOUNTSTATUS
        ;
    attr->f_attributes.validattr.fileattr = 0
        | ATTR_FILE_LINKCOUNT
        | ATTR_FILE_TOTALSIZE
        | ATTR_FILE_ALLOCSIZE
        | ATTR_FILE_IOBLOCKSIZE
        | ATTR_FILE_DEVTYPE
//      | ATTR_FILE_FORKCOUNT
//      | ATTR_FILE_FORKLIST
        | ATTR_FILE_DATALENGTH
        | ATTR_FILE_DATAALLOCSIZE
//      | ATTR_FILE_RSRCLENGTH
//      | ATTR_FILE_RSRCALLOCSIZE
        ;
    attr->f_attributes.validattr.forkattr = 0;
    
    // All attributes that we do support, we support natively.
    
    attr->f_attributes.nativeattr.commonattr = \
        attr->f_attributes.validattr.commonattr;
    attr->f_attributes.nativeattr.volattr    = \
        attr->f_attributes.validattr.volattr;
    attr->f_attributes.nativeattr.dirattr    = \
        attr->f_attributes.validattr.dirattr;
    attr->f_attributes.nativeattr.fileattr   = \
        attr->f_attributes.validattr.fileattr;
    attr->f_attributes.nativeattr.forkattr   = \
        attr->f_attributes.validattr.forkattr;

    VFSATTR_SET_SUPPORTED(attr, f_attributes);
}

static errno_t
fuse_vfs_getattr(mount_t mp, struct vfs_attr *attr, vfs_context_t context)
{
    int err    = 0;
    int faking = 0;

    struct fuse_dispatcher  fdi;
    struct fuse_statfs_out *fsfo;
    struct fuse_statfs_out  faked;
    struct fuse_data       *data;

    fuse_trace_printf_vfsop();

    data = fusefs_get_data(mp);
    if (!data) {
        panic("MacFUSE: no private data for mount point?");
    }

    if (!(data->dataflag & FSESS_INITED)) {
        faking = 1;
        goto dostatfs;
    }

    if ((err = fdisp_simple_vfs_getattr(&fdi, mp, context))) {

         // If we cannot communicate with the daemon (most likely because
         // it's dead), we still want to portray that we are a bonafide
         // file system so that we can be gracefully unmounted.

        if (err == ENOTCONN) {
            faking = 1;
            goto dostatfs;
        }

        return err;
    }

dostatfs:
    if (faking == 1) {
        bzero(&faked, sizeof(faked));
        fsfo = &faked;

         /*
          * This is a kludge so that the Finder doesn't get unhappy
          * upon seeing a block size of 0, which is possible if the Finder
          * causes a vfs_getattr() before the daemon handshake is complete.
          */
         faked.st.frsize = FUSE_DEFAULT_BLOCKSIZE;

    } else {
        fsfo = fdi.answ;
    }

    /*
     * FUSE user daemon will (might) give us this:
     *
     * __u64   blocks;  // total data blocks in the file system
     * __u64   bfree;   // free blocks in the file system
     * __u64   bavail;  // free blocks available to non-superuser
     * __u64   files;   // total file nodes in the file system
     * __u64   ffree;   // free file nodes in the file system
     * __u32   bsize;   // optimal transfer block size
     * __u32   namelen; // maximum length of filenames
     * __u32   frsize;  // fundamental file system block size
     *
     * On Mac OS X, we will map this data to struct vfs_attr as follows:
     *
     *  Mac OS X                     FUSE
     *  --------                     ----
     *  uint64_t f_supported   <-    // handled here
     *  uint64_t f_active      <-    // handled here
     *  uint64_t f_objcount    <-    -
     *  uint64_t f_filecount   <-    files
     *  uint64_t f_dircount    <-    -
     *  uint32_t f_bsize       <-    frsize
     *  size_t   f_iosize      <-    bsize
     *  uint64_t f_blocks      <-    blocks
     *  uint64_t f_bfree       <-    bfree
     *  uint64_t f_bavail      <-    bavail
     *  uint64_t f_bused       <-    blocks - bfree
     *  uint64_t f_files       <-    files
     *  uint64_t f_ffree       <-    ffree
     *  fsid_t   f_fsid        <-    // handled elsewhere
     *  uid_t    f_owner       <-    // handled elsewhere
     *  ... capabilities       <-    // handled here
     *  ... attributes         <-    // handled here
     *  f_create_time          <-    -
     *  f_modify_time          <-    -
     *  f_access_time          <-    -
     *  f_backup_time          <-    -
     *  uint32_t f_fssubtype   <-    // daemon provides
     *  char *f_vol_name       <-    // handled here
     *  uint16_t f_signature   <-    // handled here
     *  uint16_t f_carbon_fsid <-    // handled here
     */

    VFSATTR_RETURN(attr, f_filecount, fsfo->st.files);
    VFSATTR_RETURN(attr, f_bsize, fsfo->st.frsize);
    VFSATTR_RETURN(attr, f_iosize, fsfo->st.bsize);
    VFSATTR_RETURN(attr, f_blocks, fsfo->st.blocks);
    VFSATTR_RETURN(attr, f_bfree, fsfo->st.bfree);
    VFSATTR_RETURN(attr, f_bavail, fsfo->st.bavail);
    VFSATTR_RETURN(attr, f_bused, (fsfo->st.blocks - fsfo->st.bfree));
    VFSATTR_RETURN(attr, f_files, fsfo->st.files);
    VFSATTR_RETURN(attr, f_ffree, fsfo->st.ffree);

    /* f_fsid and f_owner handled elsewhere. */

    /* Handle capabilities and attributes. */
    handle_capabilities_and_attributes(attr);

    VFSATTR_RETURN(attr, f_create_time, kZeroTime);
    VFSATTR_RETURN(attr, f_modify_time, kZeroTime);
    VFSATTR_RETURN(attr, f_access_time, kZeroTime);
    VFSATTR_RETURN(attr, f_backup_time, kZeroTime);

    VFSATTR_RETURN(attr, f_fssubtype, data->subtype);

    /* Daemon needs to pass this. */
    if (VFSATTR_IS_ACTIVE(attr, f_vol_name)) {
        if (data->volname[0] != 0) {
            strncpy(attr->f_vol_name, data->volname, MAXPATHLEN);
            attr->f_vol_name[MAXPATHLEN - 1] = 0;
            VFSATTR_SET_SUPPORTED(attr, f_vol_name);
        }
    }

    VFSATTR_RETURN(attr, f_signature, OSSwapBigToHostInt16(FUSEFS_SIGNATURE));
    VFSATTR_RETURN(attr, f_carbon_fsid, 0);

    if (faking == 0) {
        fuse_ticket_drop(fdi.tick);
    }

    return (0);
}

struct fuse_sync_cargs {
    vfs_context_t context;
    int waitfor;
    int error;
};

static int
fuse_sync_callback(vnode_t vp, void *cargs)
{
    int type;
    struct fuse_sync_cargs *args;
    struct fuse_vnode_data *fvdat;
    struct fuse_dispatcher  fdi;
    struct fuse_filehandle *fufh;

    if (!vnode_hasdirtyblks(vp)) {
        return VNODE_RETURNED;
    }

    if (fuse_isdeadfs_nop(vp)) {
        return VNODE_RETURNED_DONE;
    }

    if (fdata_kick_get(fusefs_get_data(vnode_mount(vp)))) {
        return VNODE_RETURNED_DONE;
    }

    if (!(fusefs_get_data(vnode_mount(vp))->dataflag &
        vnode_vtype(vp) == VDIR ? FSESS_NOFSYNCDIR : FSESS_NOFSYNC)) {
        return VNODE_RETURNED;
    }

    args = (struct fuse_sync_cargs *)cargs;
    fvdat = VTOFUD(vp);

    cluster_push(vp, 0);

    fdisp_init(&fdi, 0);
    for (type = 0; type < FUFH_MAXTYPE; type++) {
        fufh = &(fvdat->fufh[type]);
        if (fufh->fufh_flags & FUFH_VALID) {
            fuse_internal_fsync(vp, args->context, fufh, &fdi);
        }
    }

    /*
     * In general:
     *
     * - can use vnode_isinuse() if the need be
     * - vnode and UBC are in lock-step
     * - note that umount will call ubc_sync_range()
     */

    return VNODE_RETURNED;
}

static errno_t
fuse_vfs_sync(mount_t mp, int waitfor, vfs_context_t context)
{
    uint64_t mntflags;
    struct fuse_sync_cargs args;
    int allerror = 0;

    fuse_trace_printf_vfsop();

    mntflags = vfs_flags(mp);

    if (fuse_isdeadfs_mp(mp)) {
        return 0;
    }

    if (vfs_isupdate(mp)) {
        return 0;
    } 

    if (vfs_isrdonly(mp)) {
        return EROFS; // should panic!?
    }

    /*
     * Write back each (modified) fuse node.
     */
    args.context = context;
    args.waitfor = waitfor;
    args.error = 0;

    vnode_iterate(mp, 0, fuse_sync_callback, (void *)&args);

    if (args.error) {
        allerror = args.error;
    }

    /*
     * For other types of stale file system information, such as:
     *
     * - fs control info
     * - quota information
     * - modified superblock
     */

    return allerror;
}

static errno_t
fuse_vfs_setattr(mount_t mp, struct vfs_attr *fsap, vfs_context_t context)
{
    int error = 0;
    struct fuse_data *data;
    kauth_cred_t cred = vfs_context_ucred(context);

    fuse_trace_printf_vfsop();

    if (!fuse_vfs_context_issuser(context) &&
        (kauth_cred_getuid(cred) != vfs_statfs(mp)->f_owner)) {
        return EACCES;
    }

    data = fusefs_get_data(mp);

    if (VFSATTR_IS_ACTIVE(fsap, f_vol_name)) {

        size_t vlen;

        if (fsap->f_vol_name[0] == 0) {
            goto out;
        }

        /*
         * If the FUSE API supported volume name change, we would be sending
         * a message to the FUSE daemon at this point.
         */

        copystr(fsap->f_vol_name, data->volname, MAXPATHLEN - 1, &vlen);
        bzero(data->volname + vlen, MAXPATHLEN - vlen);
        VFSATTR_SET_SUPPORTED(fsap, f_vol_name);
    }

out:
    return error;
}
