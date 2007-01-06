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

// shared between the kernel and user spaces
struct fuse_mount_args {
    char     mntpath[MAXPATHLEN]; // path to the mount point

    uint32_t altflags;            // see mount-time flags below
    uint32_t blocksize;           // fictitious block size of our "storage"
    uint32_t fsid;                // optional custom value for part of fsid[0]
    char     fsname[MAXPATHLEN];  // file system description string (arbitrary)
    uint32_t iosize;              // maximum size for reading or writing
    uint32_t index;               // the N in /dev/fuseN
    uint32_t subtype;             // the file system's sub type (type is FUSE)
    dev_t    rdev;                // dev_t for the /dev/fuseN in question
    char     volname[MAXPATHLEN]; // volume name
};
typedef struct fuse_mount_args fuse_mount_args;

// file system subtypes
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

// mount-time flags
#define FUSE_MOPT_IGNORE                0x00000000
#define FUSE_MOPT_ALLOW_OTHER           0x00000001
#define FUSE_MOPT_ALLOW_ROOT            0x00000002
#define FUSE_MOPT_BLOCKSIZE             0x00000004
#define FUSE_MOPT_DEBUG                 0x00000008
#define FUSE_MOPT_DEFAULT_PERMISSIONS   0x00000010
#define FUSE_MOPT_DIRECT_IO             0x00000020
#define FUSE_MOPT_FD                    0x00000040
#define FUSE_MOPT_FSID                  0x00000080
#define FUSE_MOPT_FSNAME                0x00000100
#define FUSE_MOPT_GID                   0x00000200
#define FUSE_MOPT_HARD_REMOVE           0x00000400
#define FUSE_MOPT_IOSIZE                0x00000800
#define FUSE_MOPT_JAIL_SYMLINKS         0x00001000
#define FUSE_MOPT_KERNEL_CACHE          0x00002000
#define FUSE_MOPT_LARGE_READ            0x00004000
#define FUSE_MOPT_MAX_READ              0x00008000
#define FUSE_MOPT_NO_ATTRCACHE          0x00010000
#define FUSE_MOPT_NO_AUTH_OPAQUE        0x00020000
#define FUSE_MOPT_NO_AUTH_OPAQUE_ACCESS 0x00040000
#define FUSE_MOPT_NO_READAHEAD          0x00080000
#define FUSE_MOPT_NO_SYNCWRITES         0x00100000
#define FUSE_MOPT_NO_UBC                0x00200000
#define FUSE_MOPT_PING_DISKARB          0x00400000
#define FUSE_MOPT_READDIR_INO           0x00800000
#define FUSE_MOPT_ROOTMODE              0x01000000
#define FUSE_MOPT_SUBTYPE               0x02000000
#define FUSE_MOPT_UID                   0x04000000
#define FUSE_MOPT_UMASK                 0x08000000
#define FUSE_MOPT_USE_INO               0x10000000
#define FUSE_MOPT_VOLNAME               0x20000000

#define FUSE_MAKEDEV(x, y)              ((dev_t)(((x) << 24) | (y)))
#define FUSE_MINOR_MASK                 0xFFFFFF
#define FUSE_CUSTOM_FSID_DEVICE_MAJOR   255
#define FUSE_CUSTOM_FSID_VAL1           0x55464553

#endif /* _FUSE_MOUNT_H_ */
