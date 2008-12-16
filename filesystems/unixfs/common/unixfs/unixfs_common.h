/*
 * UnixFS
 *
 * A general-purpose file system layer for writing/reimplementing/porting
 * Unix file systems through MacFUSE.

 * Copyright (c) 2008 Amit Singh. All Rights Reserved.
 * http://osxbook.com
 */

#ifndef _UNIXFS_COMMON_H_
#define _UNIXFS_COMMON_H_

/* Implementation of Ancient Unix file system internals. */

static void*         unixfs_internal_init(const char* dmg, uint32_t flags,
                                          char** fsname, char** volname);
static void          unixfs_internal_fini(void*);
static off_t         unixfs_internal_alloc(void);
static off_t         unixfs_internal_bmap(struct inode* ip, off_t lblkno,
                                         int* error);
static int           unixfs_internal_bread(off_t blkno, char *blkbuf);
static struct inode* unixfs_internal_iget(ino_t ino);
static void          unixfs_internal_iput(struct inode* ip);
static int           unixfs_internal_igetattr(ino_t ino, struct stat *stbuf);
static void          unixfs_internal_istat(struct inode* ip,
                                           struct stat* stbuf);
static int           unixfs_internal_namei(ino_t parentino, const char *name,
                                           struct stat* stbuf);
static int           unixfs_internal_nextdirentry(struct inode* ip,
                                                  struct unixfs_dirbuf* dirbuf,
                                                  off_t* offset,
                                                  struct unixfs_direntry* dent);
static ssize_t       unixfs_internal_pbread(struct inode* ip, char* buf,
                                            size_t nbyte, off_t offset,
                                            int* error);
static int           unixfs_internal_sanitycheck(void* filsys, off_t disksize);
static int           unixfs_internal_readlink(ino_t ino,
                                              char path[UNIXFS_MAXPATHLEN]);
static int           unixfs_internal_statvfs(struct statvfs* svb);

/* To be used in file-system-specific code. */

#define DECL_UNIXFS(fsname, sufx)                     \
    static struct unixfs_ops ops_##sufx = {           \
        .init         = unixfs_internal_init,         \
        .fini         = unixfs_internal_fini,         \
        .alloc        = unixfs_internal_alloc,        \
        .bmap         = unixfs_internal_bmap,         \
        .bread        = unixfs_internal_bread,        \
        .iget         = unixfs_internal_iget,         \
        .iput         = unixfs_internal_iput,         \
        .igetattr     = unixfs_internal_igetattr,     \
        .istat        = unixfs_internal_istat,        \
        .namei        = unixfs_internal_namei,        \
        .nextdirentry = unixfs_internal_nextdirentry, \
        .pbread       = unixfs_internal_pbread,       \
        .readlink     = unixfs_internal_readlink,     \
        .sanitycheck  = unixfs_internal_sanitycheck,  \
        .statvfs      = unixfs_internal_statvfs,      \
    };                                                \
    struct unixfs unixfs_##sufx = {                   \
        &ops_##sufx, NULL, -1, 0                      \
    };                                                \
    static struct super_block* unixfs = NULL;         \
    static const char* unixfs_fstype = fsname;

#endif /* _UNIXFS_COMMON_H_ */
