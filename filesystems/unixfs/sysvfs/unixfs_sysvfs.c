/*
 * SYSV File System Famiy for MacFUSE
 * Copyright (c) 2008 Amit Singh
 * http://osxbook.com
 */

#include "sysvfs.h"
#include "unixfs_common.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

DECL_UNIXFS("UNIX System V", sysv);

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

    if ((err = unixfs_inodelayer_init(sizeof(struct sysv_inode_info))) != 0)
        goto out;

    sb = sysv_fill_super(fd, (void*)0, 1 /* silent */);
    if (!sb) {
        err = EINVAL;
        goto out;
    }

    struct sysv_sb_info* sbi = SYSV_SB(sb);

    unixfs = sb;
    unixfs->s_flags = flags;

    unixfs->s_statvfs.f_bsize   = max(PAGE_SIZE, sb->s_blocksize);
    unixfs->s_statvfs.f_frsize  = sb->s_blocksize;
    unixfs->s_statvfs.f_blocks  = sbi->s_ndatazones;
    unixfs->s_statvfs.f_bavail  = sysv_count_free_blocks(sb);
    unixfs->s_statvfs.f_bfree   = unixfs->s_statvfs.f_bavail;
    unixfs->s_statvfs.f_files   = sbi->s_ninodes;
    unixfs->s_statvfs.f_ffree   = sysv_count_free_inodes(sb);
    unixfs->s_statvfs.f_namemax = SYSV_NAMELEN;
    unixfs->s_dentsize = 0;

    snprintf(unixfs->s_fsname, UNIXFS_MNAMELEN, "%s", sysv_flavor(sbi->s_type));
    snprintf(unixfs->s_volname, UNIXFS_MAXNAMLEN, "%s (%s)",
             unixfs_fstype, sysv_flavor(sbi->s_type));

    *fsname = unixfs->s_fsname;
    *volname = unixfs->s_volname;

out:
    if (err) {
        if (fd > 0)
            close(fd);
        if (sb) {
            struct sysv_sb_info* sbi = SYSV_SB(sb);
            if (sbi) {
                void* bh1 = sbi->s_bh1;
                void* bh2 = sbi->s_bh2;
                if (bh1) {
                    if (bh1 == bh2)
                        bh2 = NULL;
                    brelse(bh1);
                }
                if (bh2)
                    brelse(bh2);
                free(sbi);
            }
            free(sb);
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
        struct sysv_sb_info* sbi = SYSV_SB(sb);
        if (sbi) {
            void* bh1 = sbi->s_bh1;
            void* bh2 = sbi->s_bh2;
            if (bh1) {
                if (bh1 == bh2)
                    bh2 = NULL;
                brelse(bh1);
            }
            if (bh2)
                brelse(bh2);
            free(sbi);
        }
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
    *error = sysv_get_block(ip, lblkno, &result);
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
        ino = SYSV_ROOT_INO;

   struct super_block* sb = unixfs;
   struct sysv_sb_info* sbi = SYSV_SB(sb);

   if (!ino || ino > sbi->s_ninodes) {
       fprintf(stderr, "bad inode number: %llu\n", ino);
       return NULL;
    }

    struct inode* inode = unixfs_inodelayer_iget(ino);
    if (!inode) {
        fprintf(stderr, "*** fatal error: no inode for %llu\n", ino);
        abort();
    }

    if (inode->I_initialized)
        return inode;

    struct buffer_head  bh;
    struct sysv_dinode* raw_inode = sysv_raw_inode(sb, ino, &bh);
    if (!raw_inode) {
        fprintf(stderr, "major problem: failed to read inode %llu\n", ino);
        unixfs_inodelayer_ifailed(inode);
        goto bad_inode;
    }

    struct sysv_inode_info *si = SYSV_I(inode);

    inode->I_ino = ino;

    /* SystemV FS: kludge permissions if ino==SYSV_ROOT_INO ?? */

    inode->I_mode = fs16_to_host(unixfs->s_endian, raw_inode->di_mode);

    inode->I_uid   = (uid_t)fs16_to_host(unixfs->s_endian, raw_inode->di_uid);
    inode->I_gid   = (gid_t)fs16_to_host(unixfs->s_endian, raw_inode->di_gid);
    inode->I_nlink = fs16_to_host(unixfs->s_endian, raw_inode->di_nlink);
    inode->I_size  = fs32_to_host(unixfs->s_endian, raw_inode->di_size);

    inode->I_atime.tv_sec = fs32_to_host(unixfs->s_endian, raw_inode->di_atime);
    inode->I_mtime.tv_sec = fs32_to_host(unixfs->s_endian, raw_inode->di_mtime);
    inode->I_ctime.tv_sec = fs32_to_host(unixfs->s_endian, raw_inode->di_ctime);
    inode->I_ctime.tv_nsec = 0;
    inode->I_atime.tv_nsec = 0;
    inode->I_mtime.tv_nsec = 0;

    inode->I_sb = unixfs;
    inode->I_blkbits = sb->s_blocksize_bits;

    unsigned int block;
    for (block = 0; block < (10 + 1 + 1 + 1); block++)
        sysv_read3byte(sbi, &raw_inode->di_data[3 * block],
                       (u8*)&si->i_data[block]);

    if (S_ISCHR(inode->I_mode) || S_ISBLK(inode->I_mode)) {
        uint32_t rdev = fs32_to_host(unixfs->s_endian, si->i_data[0]);
        inode->I_rdev = makedev((rdev >> 8) & 255, rdev & 255);
    }

    si->i_dir_start_lookup = 0;

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
        ino = SYSV_ROOT_INO;

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
        parentino = SYSV_ROOT_INO;

    stbuf->st_ino = ENOENT;

    struct inode* dir = unixfs_internal_iget(parentino);
    if (!dir)
        return ENOENT;

    if (!S_ISDIR(dir->I_mode)) {
        unixfs_internal_iput(dir);
        return ENOTDIR;
    }

    int ret = ENOENT, found = 0;

    unsigned long namelen = strlen(name);
    unsigned long start, n;
    unsigned long npages = sysv_dir_pages(dir);
    struct sysv_dir_entry* de;
    char page[PAGE_SIZE];
    char* kaddr = NULL;

    start = SYSV_I(dir)->i_dir_start_lookup;
    if (start >= npages)
        start = 0;
    n = start;

    do {
        int error = sysv_get_page(dir, n, page);
        if (!error) {
            kaddr = (char*)page;
            de = (struct sysv_dir_entry*) kaddr;
            kaddr += PAGE_CACHE_SIZE - SYSV_DIRSIZE;
            for ( ; (char*) de <= kaddr ; de++) {
                if (!de->inode)
                    continue;
                if (sysv_namecompare(namelen, SYSV_NAMELEN, name, de->name)) {
                    found = 1;
                    goto found;
                }
            }
        }

        if (++n >= npages)
            n = 0;
    } while (n != start);

found:

    if (found)
        SYSV_I(dir)->i_dir_start_lookup = n;

    unixfs_internal_iput(dir);

    if (found)
        ret = unixfs_internal_igetattr((ino_t)fs16_to_host(unixfs->s_endian,
                                       de->inode), stbuf);

    return ret;
}

int
unixfs_internal_nextdirentry(struct inode* dp, struct unixfs_dirbuf* dirbuf,
                             off_t* offset, struct unixfs_direntry* dent)
{
    return sysv_next_direntry(dp, dirbuf, offset, dent);

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
        *error = sysv_get_page(ip, beginpgno, page);
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
    error = sysv_get_page(ip, (off_t)0, page);
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
