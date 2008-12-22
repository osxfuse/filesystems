/*
 * UnixFS
 *
 * A general-purpose file system layer for writing/reimplementing/porting
 * Unix file systems through MacFUSE.

 * Copyright (c) 2008 Amit Singh. All Rights Reserved.
 * http://osxbook.com
 */

#ifndef _UNIXFS_H_
#define _UNIXFS_H_

#include <assert.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/statvfs.h>

#define UNIXFS_MAXPATHLEN 1024
#define UNIXFS_MAXNAMLEN  255
#define UNIXFS_MNAMELEN   90
#define UNIXFS_ARGLEN     1024

typedef enum {
    UNIXFS_FS_PDP,
    UNIXFS_FS_LITTLE,
    UNIXFS_FS_BIG,
    UNIXFS_FS_INVALID,
} fs_endian_t;

struct unixfs_ops;

/* Our encapsulation of an Ancient Unix file system instance. */

struct unixfs {
    struct      unixfs_ops *ops;       /* file system operations */
    void*       filsys;                /* in-core super block */
    uint32_t    flags;                 /* directives/miscellaneous flags */
    fs_endian_t fsendian;
    char*       fsname;
    char*       volname;
};

/* flags */

#define UNIXFS_FORCE           0x00000001 /* mount even if things look fishy */

/* Our encapsulation of an Ancient Unix directory entry. */

struct unixfs_direntry {
    ino_t ino __attribute__((aligned(8)));
    char  name[UNIXFS_MAXNAMLEN + 1];
};

#define UNIXFS_DIRBUFSIZ 8192

struct unixfs_dirbuf {
    struct flags {
       uint32_t initialized;
    } flags;
    char data[UNIXFS_DIRBUFSIZ];
};

/* Interface to Ancient Unix file system internals. */

struct inode;
struct stat;

struct unixfs_ops {
    void*         (*init)(const char* dmg, uint32_t flags, fs_endian_t fse,
                          char** fsname, char** volname);
    void          (*fini)(void*);
    off_t         (*alloc)(void);
    off_t         (*bmap)(struct inode* ip, off_t lblkno, int* error);
    int           (*bread)(off_t blkno, char* blkbuf);
    struct inode* (*iget)(ino_t ino);
    void          (*iput)(struct inode* ip);
    int           (*igetattr)(ino_t ino, struct stat* stbuf);
    void          (*istat)(struct inode* ip, struct stat* stbuf);
    int           (*namei)(ino_t parentino, const char* name,
                           struct stat* stbuf);
    int           (*nextdirentry)(struct inode* ip,
                                  struct unixfs_dirbuf* dirbuf,
                                  off_t* offset,
                                  struct unixfs_direntry* dent);
    ssize_t       (*pbread)(struct inode*ip, char* buf, size_t nbyte,
                            off_t offset, int* error);
    int           (*readlink)(ino_t, char path[UNIXFS_MAXPATHLEN]);
    int           (*sanitycheck)(void* filsys, off_t disksize);
    int           (*statvfs)(struct statvfs* svb);
};

#define min(x, y) ((x) < (y) ? (x) : (y))
#define max(x, y) ((x) > (y) ? (x) : (y))

extern void           unixfs_usage(void);
extern struct unixfs* unixfs_preflight(char*, char**, struct unixfs**);
extern void           unixfs_postflight(char*, char*, char*);

#endif /* _UNIXFS_H_ */
