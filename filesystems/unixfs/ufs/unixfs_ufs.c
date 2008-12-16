/*
 * UFS for MacFUSE
 * Copyright (c) 2008 Amit Singh
 * http://osxbook.com
 */

#include "ufs.h"
#include "unixfs_common.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

DECL_UNIXFS("UFS", ufs);

static void*
unixfs_internal_init(const char* dmg, uint32_t flags,
                     char** fsname, char** volname)
{
    int fd = -1;

    if ((fd = open(dmg, O_RDONLY)) < 0) {
        perror("open");
        return NULL;
    }

    int err;
    struct stat stbuf;
    struct super_block* sb = (struct super_block*)0;

    if ((err = fstat(fd, &stbuf)) != 0) {
        perror("fstat");
        goto out;
    }

    if (!S_ISREG(stbuf.st_mode) && !(flags & UNIXFS_FORCE)) {
        err = EINVAL;
        fprintf(stderr, "%s is not a disk image file\n", dmg);
        goto out;
    }

    if ((err = unixfs_inodelayer_init(sizeof(struct ufs_inode_info))) != 0)
        goto out;

    char args[UNIXFS_MNAMELEN];
    snprintf(args, UNIXFS_MNAMELEN, "%s", *fsname);
    char* c = args;
    for (; *c; c++)
        *c = tolower(*c);

    sb = U_ufs_fill_super(fd, (void*)args, 0 /* silent */);
    if (!sb) {
        err = EINVAL;
        goto out;
    }

    unixfs = sb;

    err = U_ufs_statvfs(sb, &(unixfs->s_statvfs));
    if (err)
        goto out;

    unixfs->s_flags = flags;

    unixfs->s_dentsize = 0; /* variable */

    snprintf(unixfs->s_volname, UNIXFS_MAXNAMLEN, "%s", unixfs_fstype);
    snprintf(unixfs->s_fsname, UNIXFS_MNAMELEN, "%s", *fsname);

    *fsname = unixfs->s_fsname;
    *volname = unixfs->s_volname;

out:
    if (err) {
        if (fd > 0)
            close(fd);
        if (sb)
            free(sb);
    }

    return sb;
}

static void
unixfs_internal_fini(void* filsys)
{
    unixfs_inodelayer_fini();

    struct super_block* sb = (struct super_block*)filsys;
    if (sb)
        free(sb);
}

static off_t
unixfs_internal_alloc(void)
{
    return (off_t)0;
}

static off_t
unixfs_internal_bmap(struct inode* ip, off_t lblkno, int* error)
{
    off_t result;
    *error = U_ufs_get_block(ip, lblkno, &result);
    return result;
}

static int
unixfs_internal_bread(off_t blkno, char* blkbuf)
{
    struct super_block* sb = unixfs;

    if (pread(sb->s_bdev, blkbuf, sb->s_blocksize,
              blkno * (off_t)(sb->s_blocksize)) != sb->s_blocksize)
        return EIO;

    return 0;
}

struct inode*
unixfs_internal_iget(ino_t ino)
{
    if (ino == MACFUSE_ROOTINO)
        ino = UFS_ROOTINO;

    struct inode* inode = unixfs_inodelayer_iget(ino);
    if (!inode) {
        fprintf(stderr, "*** fatal error: no inode for %llu\n", ino);
        abort();
    }

    if (inode->I_initialized)
        return inode;

    struct super_block* sb = unixfs;
    inode->I_ino = ino;

    int error = U_ufs_iget(sb, inode);
    if (error) {
        fprintf(stderr, "major problem: failed to read inode %llu\n", ino);
        unixfs_inodelayer_ifailed(inode);
        goto bad_inode;
    }

    unixfs_inodelayer_isucceeded(inode);

    return inode;

bad_inode:
    return NULL;
}

static void
unixfs_internal_iput(struct inode* ip)
{
    unixfs_inodelayer_iput(ip);
}

static int
unixfs_internal_igetattr(ino_t ino, struct stat* stbuf)
{
    if (ino == MACFUSE_ROOTINO)
        ino = UFS_ROOTINO;

    struct inode* ip = unixfs_internal_iget(ino);
    if (!ip)
        return ENOENT;

    unixfs_internal_istat(ip, stbuf);

    unixfs_internal_iput(ip);

    return 0;
}

static void
unixfs_internal_istat(struct inode* ip, struct stat* stbuf)
{
    memcpy(stbuf, &ip->I_stat, sizeof(struct stat));
}

static int
unixfs_internal_namei(ino_t parentino, const char* name, struct stat* stbuf)
{
    if (parentino == MACFUSE_ROOTINO)
        parentino = UFS_ROOTINO;

    int ret = ENOENT;
    stbuf->st_ino = 0;

    size_t namelen = strlen(name);
    if (namelen > UFS_MAXNAMLEN)
        return ENAMETOOLONG;

    struct inode* dir = unixfs_internal_iget(parentino);
    if (!dir)
        return ENOENT;

    if (!S_ISDIR(dir->I_mode)) {
        unixfs_internal_iput(dir);
        return ENOTDIR;
    }

    ino_t target = U_ufs_inode_by_name(dir, name);
    if (target)
        ret = unixfs_internal_igetattr(target, stbuf);

    unixfs_internal_iput(dir);

    return ret;
}

int
unixfs_internal_nextdirentry(struct inode* dp, struct unixfs_dirbuf* dirbuf,
                             off_t* offset, struct unixfs_direntry* dent)
{
    return U_ufs_next_direntry(dp, dirbuf, offset, dent);

}

static ssize_t
unixfs_internal_pbread(struct inode* ip, char* buf, size_t nbyte, off_t offset,
                       int* error)
{
    char* p = buf;
    ssize_t done = 0;
    ssize_t tomove = 0;
    ssize_t remaining = nbyte;
    char page[PAGE_SIZE];
    sector_t beginpgno = offset >> PAGE_CACHE_SHIFT;

    while (remaining > 0) { /* page sized reads */
        *error = U_ufs_get_page(ip, beginpgno, page);
        if (*error)
            break;
        tomove = (remaining > PAGE_SIZE) ? PAGE_SIZE : remaining;
        memcpy(p, page, tomove);
        remaining -= tomove;
        done += tomove;
        p += tomove;
        beginpgno++;
    }

    if ((done == 0) && *error)
        return -1;
    
    return done;
}

static int
unixfs_internal_readlink(ino_t ino, char path[UNIXFS_MAXPATHLEN])
{
    struct inode* ip = unixfs_internal_iget(ino);
    if (!ip)
        return ENOENT;

    int error = 0;

    size_t linklen =
        (ip->I_size > UNIXFS_MAXPATHLEN - 1) ? UNIXFS_MAXPATHLEN - 1: ip->I_size;

    if (!ip->I_blocks) { /* fast */
        struct ufs_inode_info* p = (struct ufs_inode_info*)ip->I_private;
        memcpy(path, (char*)p->i_u1.i_symlink, sizeof(p->i_u1.i_symlink));
    } else {
        char page[PAGE_SIZE];
        error = U_ufs_get_page(ip, (off_t)0, page);
        if (error)
            goto out;
        memcpy(path, page, linklen);
    }

    path[linklen] = '\0';

out:
    unixfs_internal_iput(ip);

    return error;
}

int
unixfs_internal_sanitycheck(void* filsys, off_t disksize)
{
    /* handled elsewhere */
    return 0;
}

static int
unixfs_internal_statvfs(struct statvfs* svb)
{
    return U_ufs_statvfs(unixfs, svb);
}
