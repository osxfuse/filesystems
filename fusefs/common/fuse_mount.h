/*
 * Copyright (C) 2006 Google. All Rights Reserved.
 * Amit Singh <singh@>
 */

#ifndef _FUSE_MOUNT_H_
#define _FUSE_MOUNT_H_

#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifndef KERNEL
#include <unistd.h>
#endif

#include <fuse_param.h>
#include <fuse_version.h>

/*
 * Shared between the kernel and user spaces.
 */
struct fuse_mount_args {
    char     mntpath[MAXPATHLEN]; // path to the mount point

    uint64_t altflags;            // see mount-time flags below
    uint32_t blocksize;           // fictitious block size of our "storage"
    uint32_t daemon_timeout;      // timeout in seconds for upcalls to daemon
    uint32_t fsid;                // optional custom value for part of fsid[0]
    char     fsname[MAXPATHLEN];  // file system description string (arbitrary)
    uint32_t index;               // the N in /dev/fuseN
    uint32_t init_timeout;        // timeout in seconds for daemon handshake
    uint32_t iosize;              // maximum size for reading or writing
    dev_t    rdev;                // dev_t for the /dev/fuseN in question
    uint32_t subtype;             // file system sub type (type is FUSE)
    char     volname[MAXPATHLEN]; // volume name
};
typedef struct fuse_mount_args fuse_mount_args;

/* file system subtypes */
enum {
    FUSE_FSSUBTYPE_UNKNOWN = 0,
    FUSE_FSSUBTYPE_XMPFS,
    FUSE_FSSUBTYPE_SSHFS,
    FUSE_FSSUBTYPE_FTPFS,
    FUSE_FSSUBTYPE_WEBDAVFS,
    FUSE_FSSUBTYPE_SPOTLIGHTFS,
    FUSE_FSSUBTYPE_PICASAFS,
    FUSE_FSSUBTYPE_PROCFS,
    FUSE_FSSUBTYPE_NTFS,
    FUSE_FSSUBTYPE_BEAGLEFS,
    FUSE_FSSUBTYPE_CRYPTOFS,
    FUSE_FSSUBTYPE_MAX,
};

/* mount-time flags */
#define FUSE_MOPT_IGNORE                 0x0000000000000000ULL
#define FUSE_MOPT_ALLOW_OTHER            0x0000000000000001ULL
#define FUSE_MOPT_ALLOW_RECURSION        0x0000000000000002ULL
#define FUSE_MOPT_ALLOW_ROOT             0x0000000000000004ULL
#define FUSE_MOPT_BLOCKSIZE              0x0000000000000008ULL
#define FUSE_MOPT_DAEMON_TIMEOUT         0x0000000000000010ULL
#define FUSE_MOPT_DEBUG                  0x0000000000000020ULL
#define FUSE_MOPT_DEFAULT_PERMISSIONS    0x0000000000000040ULL
#define FUSE_MOPT_DEFER_AUTH             0x0000000000000080ULL
#define FUSE_MOPT_DIRECT_IO              0x0000000000000100ULL
#define FUSE_MOPT_EXTENDED_SECURITY      0x0000000000000200ULL
#define FUSE_MOPT_FD                     0x0000000000000400ULL
#define FUSE_MOPT_FSID                   0x0000000000000800ULL
#define FUSE_MOPT_FSNAME                 0x0000000000001000ULL
#define FUSE_MOPT_GID                    0x0000000000002000ULL
#define FUSE_MOPT_HARD_REMOVE            0x0000000000004000ULL
#define FUSE_MOPT_INIT_TIMEOUT           0x0000000000008000ULL
#define FUSE_MOPT_IOSIZE                 0x0000000000010000ULL
#define FUSE_MOPT_JAIL_SYMLINKS          0x0000000000020000ULL
#define FUSE_MOPT_KERNEL_CACHE           0x0000000000040000ULL
#define FUSE_MOPT_KILL_ON_UNMOUNT        0x0000000000080000ULL
#define FUSE_MOPT_NO_ALERTS              0x0000000000100000ULL
#define FUSE_MOPT_NO_APPLESPECIAL        0x0000000000200000ULL
#define FUSE_MOPT_NO_ATTRCACHE           0x0000000000400000ULL
#define FUSE_MOPT_NO_AUTH_OPAQUE         0x0000000000800000ULL
#define FUSE_MOPT_NO_AUTH_OPAQUE_ACCESS  0x0000000001000000ULL
#define FUSE_MOPT_NO_BROWSE              0x0000000002000000ULL
#define FUSE_MOPT_NO_LOCALCACHES         0x0000000004000000ULL
#define FUSE_MOPT_NO_READAHEAD           0x0000000008000000ULL
#define FUSE_MOPT_NO_SYNCONCLOSE         0x0000000010000000ULL
#define FUSE_MOPT_NO_SYNCWRITES          0x0000000020000000ULL
#define FUSE_MOPT_NO_UBC                 0x0000000040000000ULL
#define FUSE_MOPT_NO_VNCACHE             0x0000000080000000ULL

/* Next 32 bits */
#define FUSE_MOPT_PING_DISKARB           0x0000000100000000ULL
#define FUSE_MOPT_READDIR_INO            0x0000000200000000ULL
#define FUSE_MOPT_ROOTMODE               0x0000000400000000ULL
#define FUSE_MOPT_SUBTYPE                0x0000000800000000ULL
#define FUSE_MOPT_UID                    0x0000001000000000ULL
#define FUSE_MOPT_UMASK                  0x0000002000000000ULL
#define FUSE_MOPT_USE_INO                0x0000004000000000ULL
#define FUSE_MOPT_VOLICON                0x0000008000000000ULL
#define FUSE_MOPT_VOLNAME                0x0000010000000000ULL
#define FUSE_MOPT_VOL_RENAME             0x0000020000000000ULL

#define FUSE_MAKEDEV(x, y)              ((dev_t)(((x) << 24) | (y)))
#define FUSE_MINOR_MASK                 0xFFFFFF
#define FUSE_CUSTOM_FSID_DEVICE_MAJOR   255
#define FUSE_CUSTOM_FSID_VAL1           0x55464553

/* Notifications... */

/* The object. */

#define FUSE_UNOTIFICATIONS_OBJECT MACFUSE_BUNDLE_IDENTIFIER ".unotifications"

/* The notifications themselves. */

#define FUSE_UNOTIFICATIONS_NOTIFY_INITED          \
    FUSE_UNOTIFICATIONS_OBJECT ".inited"

#define FUSE_UNOTIFICATIONS_NOTIFY_INITTIMEDOUT    \
    FUSE_UNOTIFICATIONS_OBJECT ".inittimedout"

#define FUSE_UNOTIFICATIONS_NOTIFY_MOUNTED         \
    FUSE_UNOTIFICATIONS_OBJECT ".mounted"

#define FUSE_UNOTIFICATIONS_NOTIFY_VERSIONMISMATCH \
    FUSE_UNOTIFICATIONS_OBJECT ".versionmismatch"

/* User data keys. */

#define kFUSEDevicePathKey "kFUSEDevicePath"
#define kFUSEMountPathKey  "kFUSEMountPath"

#endif /* _FUSE_MOUNT_H_ */
