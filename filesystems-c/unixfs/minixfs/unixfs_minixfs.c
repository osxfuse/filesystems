/*
 * Minix File System Famiy for MacFUSE
 * Copyright (c) 2008 Amit Singh
 * http://osxbook.com
 */

#include "minixfs.h"
#include "unixfs_common.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

DECL_UNIXFS("Minix", minix);

static void*
unixfs_internal_init(const char* dmg, uint32_t flags, __unused fs_endian_t fse,
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

    if ((err = unixfs_inodelayer_init(sizeof(struct minix_inode_info))) != 0)
        goto out;

    sb = minixfs_fill_super(fd, (void*)0, 1 /* silent */);
    if (!sb) {
        err = EINVAL;
        goto out;
    }

    struct minix_sb_info* sbi = minix_sb(sb);

    unixfs = sb;
    unixfs->s_flags = flags;

    (void)minixfs_statvfs(sb, &(unixfs->s_statvfs));

    unixfs->s_dentsize = 0;

    snprintf(unixfs->s_fsname, UNIXFS_MNAMELEN, "%s %s",
             unixfs_fstype, (sbi->s_version == MINIX_V3) ? "V3" :
             (sbi->s_version == MINIX_V2) ? "V2" :
             (sbi->s_version == MINIX_V1) ? "V1" : "??");
    snprintf(unixfs->s_volname, UNIXFS_MAXNAMLEN, "%s (%s)",
             unixfs_fstype, (sbi->s_version == MINIX_V3) ? "V3" :
             (sbi->s_version == MINIX_V2) ? "V2" :
             (sbi->s_version == MINIX_V1) ? "V1" : "??");

    *fsname = unixfs->s_fsname;
    *volname = unixfs->s_volname;

out:
    if (err) {
        if (fd > 0)
            close(fd);
        if (sb) {
            free(sb);
            sb = NULL;
        }
    }

    return sb;
}

static void
unixfs_internal_fini(void* filsys)
{
    unixfs_inodelayer_fini();

    struct super_block* sb = (struct super_block*)filsys;
    if (sb) {
        struct minix_sb_info* sbi = minix_sb(sb);
        if (sbi)
            free(sbi);
        free(sb);
    }
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
    *error = minixfs_get_block(ip, lblkno, &result);
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
        ino = MINIX_ROOT_INO;

   struct super_block* sb = unixfs;

    struct inode* inode = unixfs_inodelayer_iget(ino);
    if (!inode) {
        fprintf(stderr, "*** fatal error: no inode for %llu\n", ino);
        abort();
    }

    if (inode->I_initialized)
        return inode;

    inode->I_sb = unixfs;
    inode->I_blkbits = sb->s_blocksize_bits;

    if (minixfs_iget(sb, inode) != 0) {
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
        ino = MINIX_ROOT_INO;

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
        parentino = MINIX_ROOT_INO;

    stbuf->st_ino = ENOENT;

    struct inode* dir = unixfs_internal_iget(parentino);
    if (!dir)
        return ENOENT;

    if (!S_ISDIR(dir->I_mode)) {
        unixfs_internal_iput(dir);
        return ENOTDIR;
    }

    int ret = ENOENT;

    unsigned long namelen = strlen(name);
    unsigned long start, n;
    unsigned long npages = minix_dir_pages(dir);
    minix3_dirent* de3;
    minix_dirent* de;
    char page[PAGE_SIZE];
    char* kaddr = NULL;


    struct super_block* sb = dir->I_sb;
    struct minix_sb_info* sbi = minix_sb(sb);
    unsigned chunk_size = sbi->s_dirsize;
    struct minix_inode_info* minix_inode = minix_i(dir);

    start = minix_inode->i_dir_start_lookup;
    if (start >= npages)
        start = 0;
    n = start;

    ino_t found_ino = 0;

    do {
        int error = minixfs_get_page(dir, n, page);
        if (!error) {
            kaddr = (char*)page;
            if (INODE_VERSION(dir) == MINIX_V3) {
                de3 = (minix3_dirent*)kaddr;
                kaddr += PAGE_CACHE_SIZE - chunk_size;
                for (; (char*)de3 <= kaddr; de3++) {
                    if (!de3->inode)
                        continue;
                    if (minix_namecompare(namelen, chunk_size,
                                          name, de3->name)) {
                        found_ino = de3->inode;
                        goto found;
                    }
                }
            } else {
                de = (minix_dirent*)kaddr;
                kaddr += PAGE_CACHE_SIZE - chunk_size;
                for (; (char*)de <= kaddr; de++) {
                    if (!de->inode)
                        continue;
                    if (minix_namecompare(namelen, chunk_size,
                                          name, de->name)) {
                        found_ino = de->inode;
                        goto found;
                    }
                }
            }
        }

        if (++n >= npages)
            n = 0;
    } while (n != start);

found:

    if (found_ino)
        minix_inode->i_dir_start_lookup = n;

    unixfs_internal_iput(dir);

    if (found_ino)
        ret = unixfs_internal_igetattr(found_ino, stbuf);

    return ret;
}

int
unixfs_internal_nextdirentry(struct inode* dp, struct unixfs_dirbuf* dirbuf,
                             off_t* offset, struct unixfs_direntry* dent)
{
    return minixfs_next_direntry(dp, dirbuf, offset, dent);

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
        *error = minixfs_get_page(ip, beginpgno, page);
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

    char page[PAGE_SIZE];
    error = minixfs_get_page(ip, (off_t)0, page);
    if (error)
        goto out;
    memcpy(path, page, linklen);

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
    memcpy(svb, &unixfs->s_statvfs, sizeof(struct statvfs));
    return 0;
}
