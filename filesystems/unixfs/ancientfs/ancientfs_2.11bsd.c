/*
 * Ancient UNIX File Systems for MacFUSE
 * Amit Singh
 * http://osxbook.com
 */

#include "ancientfs_2.11bsd.h"
#include "unixfs_common.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

DECL_UNIXFS("2.11BSD", 211bsd);

static void*
unixfs_internal_init(const char* dmg, uint32_t flags,
                     char** fsname, char** volname)
{
    int fd = -1;
    if ((fd = open(dmg, O_RDONLY)) < 0) {
        perror("open");
        return NULL;
    }

    int err, i;
    struct stat stbuf;
    struct super_block* sb = (struct super_block*)0;
    struct fs* fs = (struct fs*)0;

    if ((err = fstat(fd, &stbuf)) != 0) {
        perror("fstat");
        goto out;
    }

    if (!S_ISREG(stbuf.st_mode) && !(flags & UNIXFS_FORCE)) {
        err = EINVAL;
        fprintf(stderr, "%s is not a disk image file\n", dmg);
        goto out;
    }

    sb = malloc(sizeof(struct super_block));
    if (!sb) {
        err = ENOMEM;
        goto out;
    }

    assert(sizeof(struct fs) <= DEV_BSIZE);

    fs = malloc(DEV_BSIZE);
    if (!fs) {
        free(sb);
        err = ENOMEM;
        goto out;
    }

    if (pread(fd, fs, SBSIZE, (off_t)(DEV_BSIZE * SUPERB)) != SBSIZE) {
        perror("pread");
        err = EIO;
        goto out;
    }

    unixfs = sb;

    unixfs->s_flags = flags;
    unixfs->s_endian = UNIXFS_FS_PDP;
    unixfs->s_fs_info = (void*)fs;
    unixfs->s_bdev = fd;

    fs->s_isize = fs16_to_host(unixfs->s_endian, fs->s_isize);
    fs->s_fsize = fs32_to_host(unixfs->s_endian, fs->s_fsize);
    fs->s_nfree = fs16_to_host(unixfs->s_endian, fs->s_nfree);

    for (i = 0; i < NICFREE; i++)
        fs->s_free[i] = fs32_to_host(unixfs->s_endian, fs->s_free[i]);
    fs->s_ninode = fs16_to_host(unixfs->s_endian, fs->s_ninode);
    for (i = 0; i < NICINOD; i++)
        fs->s_inode[i] = fs16_to_host(unixfs->s_endian, fs->s_inode[i]);
    fs->s_time = fs32_to_host(unixfs->s_endian, fs->s_time);

    unixfs->s_statvfs.f_bsize = DEV_BSIZE;
    unixfs->s_statvfs.f_frsize = DEV_BSIZE;

    /* must initialize the inode layer before sanity checking */
    if ((err = unixfs_inodelayer_init(0)) != 0)
        goto out;

    if (unixfs_internal_sanitycheck(fs, stbuf.st_size) != 0) {
        if (!(flags & UNIXFS_FORCE)) {
            fprintf(stderr,
              "retry with the --force option to see if it will work anyway\n");
            err = EINVAL;
            goto out;
        }
    }

    int iblock;
    unixfs->s_statvfs.f_files = 0;
    unixfs->s_statvfs.f_ffree = 0;

    char* ubuf = malloc(UNIXFS_IOSIZE(unixfs));
    if (!ubuf) {
        err = ENOMEM;
        goto out;
    }

    for (iblock = 2; iblock < fs->s_isize; iblock++) {
        if (unixfs_internal_bread((off_t)iblock, ubuf) != 0)
            continue;
        struct dinode* dip = (struct dinode*)ubuf;
        for (i = 0; i < INOPB; i++, dip++) {
            if (fs16_to_host(unixfs->s_endian, dip->di_nlink) == 0)
                unixfs->s_statvfs.f_ffree++;
            else
                unixfs->s_statvfs.f_files++;
        }
    }

    free(ubuf);

    unixfs->s_statvfs.f_blocks = fs->s_fsize;
    unixfs->s_statvfs.f_bfree = 0;

    while (unixfs_internal_alloc())
        unixfs->s_statvfs.f_bfree++;

    unixfs->s_statvfs.f_bavail = unixfs->s_statvfs.f_bfree;
    unixfs->s_dentsize = 0; /* no fixed size */
    unixfs->s_statvfs.f_namemax = MAXNAMLEN;

    snprintf(unixfs->s_fsname, UNIXFS_MNAMELEN, "%s", unixfs_fstype);

    char* dmg_basename = basename((char*)dmg);
    snprintf(unixfs->s_volname, UNIXFS_MAXNAMLEN, "%s (disk=%s)",
             unixfs_fstype, (dmg_basename) ? dmg_basename : "Disk Image");

    *fsname = unixfs->s_fsname;
    *volname = unixfs->s_volname;

out:
    if (err) {
        if (fd >= 0)
            close(fd);
        if (fs)
            free(fs);
        if (sb)
            free(sb);
        return NULL;
    }

    return sb;
}

static void
unixfs_internal_fini(void* filsys)
{
    unixfs_inodelayer_fini();
    struct super_block* sb = (struct super_block*)filsys;
    if (sb) {
        if (sb->s_bdev >= 0)
            close(sb->s_bdev);
        sb->s_bdev = -1;
        if (sb->s_fs_info)
            free(sb->s_fs_info);
    }
}

static off_t
unixfs_internal_alloc(void)
{
    struct fs* fs = (struct fs*)unixfs->s_fs_info;

    a_int i = --fs->s_nfree;
    if (i < 0)
        goto nospace;

    if (i > NICFREE) /* bad free count */
        return (off_t)0;

    a_daddr_t bno = fs->s_free[i];
    if (bno == 0)
        return (off_t)0;

    if (bno < fs->s_isize || bno >= fs->s_fsize)
        return (off_t)0; /* bad free block <bno> */

    if (fs->s_nfree <= 0) {
        char ubuf[UNIXFS_IOSIZE(unixfs)];
        int ret = unixfs_internal_bread((off_t)bno, ubuf);
        if (ret == 0) {
            struct fblk* fblk = (struct fblk*)ubuf;
            fs->s_nfree = fs16_to_host(unixfs->s_endian, fblk->df_nfree);
            for (i = 0; i < NICFREE; i++)
                fs->s_free[i] = fs32_to_host(unixfs->s_endian, fblk->df_free[i]);
        } else
            return (off_t)0;
    }

    return (off_t)bno;

nospace:
    fs->s_nfree = 0;
    return (off_t)0; /* ENOSPC */
}

static off_t
unixfs_internal_bmap(struct inode* ip, off_t lblkno, int* error)
{
    a_daddr_t bn = (a_daddr_t)lblkno;

    if (bn < 0) {
        *error = EFBIG;
        return (off_t)0;
    }

    *error = EROFS;

    /*
     * blocks 0..NADDR-4 are direct blocks
     */

    int i;
    a_daddr_t nb;

    if (bn < (NADDR - 3)) {
        i = bn;
        nb = ip->I_daddr[i];
        if (nb == 0)
            return (off_t)0; /* !writable; should be -1 rather */
        *error = 0;
        return (off_t)nb;
    }

    /*
     * addresses NADDR - 3, NADDR - 2, and NADDR - 1 have single, double,
     * and triple indirect blocks. the first step is to determine how many
     * levels of indirection.
     */

    int j, sh = 0;
    nb = 1;
    bn -= NADDR - 3;
    for (j = 3; j > 0; j--) { 
        sh += NSHIFT;
        nb <<= NSHIFT;
        if (bn < nb)
            break;
        bn -= nb;
    }
    if (j == 0) {
        *error = EFBIG;
        return (off_t)0;
    }

    /*
     * fetch the first indirect block
     */

    nb = ip->I_daddr[NADDR - j];
    if (nb == 0)
        return (off_t)0; /* !writable; should be -1 rather */

    /*
     * fetch through the indirect blocks
     */

    for (; j <= 3; j++) {
        char ubuf[UNIXFS_IOSIZE(unixfs)];
        int ret = unixfs_internal_bread((off_t)nb, ubuf);
        if (ret) {
            *error = ret;
            return (off_t)0;
        }
        a_daddr_t* bap = (a_daddr_t*)ubuf;
        sh -= NSHIFT;
        i = (bn >> sh) & NMASK;
        nb = fs32_to_host(unixfs->s_endian, bap[i]);
        if (nb == 0)
            return (off_t)0; /* !writable; should be -1 rather */
    }

    /* calculate read-ahead here. */

    *error = 0;

    return (off_t)nb;
}

static int
unixfs_internal_bread(off_t blkno, char* blkbuf)
{
    if (blkno >= ((struct fs*)unixfs->s_fs_info)->s_fsize) {
        fprintf(stderr,
                "***fatal error: bread failed for block %llu\n", blkno);
        abort();
        /* NOTREACHED */
    }

    if (blkno == 0) { /* zero fill */
        memset(blkbuf, 0, UNIXFS_IOSIZE(unixfs));
        return 0;
    }

    if (pread(unixfs->s_bdev, blkbuf, UNIXFS_IOSIZE(unixfs),
              blkno * (off_t)DEV_BSIZE) != UNIXFS_IOSIZE(unixfs))
        return EIO;

    return 0;
}

static struct inode*
unixfs_internal_iget(ino_t ino)
{
    if (ino == MACFUSE_ROOTINO)
        ino = ROOTINO;

    struct inode* ip = unixfs_inodelayer_iget(ino);
    if (!ip) {
        fprintf(stderr, "*** fatal error: no inode for %llu\n", ino);
        abort();
    }

    if (ip->I_initialized)
        return ip;

    char ubuf[UNIXFS_IOSIZE(unixfs)];

    if (unixfs_internal_bread((off_t)itod((a_ino_t)ino), ubuf) != 0) {
        unixfs_inodelayer_ifailed(ip);
        return NULL;
    }

    struct dinode* dip = (struct dinode*)ubuf;
    dip += itoo((a_ino_t)ino);

    ip->I_number = ino;

    /* ip->I_ic1 = dip->di_ic1 */

    ip->I_mode  = fs16_to_host(unixfs->s_endian, dip->di_mode);
    ip->I_nlink = fs16_to_host(unixfs->s_endian, dip->di_nlink);
    ip->I_uid   = fs16_to_host(unixfs->s_endian, dip->di_uid);
    ip->I_gid   = fs16_to_host(unixfs->s_endian, dip->di_gid);
    ip->I_size  = fs32_to_host(unixfs->s_endian, dip->di_size);

#ifndef EXTERNALTIMES

    /* ip->I_ic2 = dip->di_ic2 */

    ip->I_atime.tv_sec = fs32_to_host(unixfs->s_endian, dip->di_atime);
    ip->I_mtime.tv_sec = fs32_to_host(unixfs->s_endian, dip->di_mtime);
    ip->I_ctime.tv_sec = fs32_to_host(unixfs->s_endian, dip->di_ctime);

#endif

    int i;

    for (i = 0; i < NADDR; i++)
        ip->I_daddr[i] = fs32_to_host(unixfs->s_endian, dip->di_addr[i]);

    if (S_ISCHR(ip->I_mode) || S_ISBLK(ip->I_mode)) {
        uint32_t rdev = ip->I_daddr[0];
        ip->I_rdev = makedev((rdev >> 8) & 255, rdev & 255);
    }

    unixfs_inodelayer_isucceeded(ip);

    return ip;
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
        ino = ROOTINO;

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

#define	roundup(x, y) ((((x)+((y)-1))/(y))*(y))

static int
__unixfs_internal_blkatoff(struct inode* ip, off_t offset, char* ubuf)
{
    int error;

    off_t lbn = lblkno(offset);
    off_t bn = unixfs_internal_bmap(ip, lbn, &error);
    if (bn == 0)
        return error;

    return unixfs_internal_bread(bn, ubuf);
}

static int
__unixfs_internal_dirbadentry(struct direct* ep, int entryoffsetinblock)
{
    int i;

    if ((ep->d_reclen & 0x3) != 0 ||
         ep->d_reclen > ANCIENTFS_211BSD_DIRBLKSIZ - \
             (entryoffsetinblock & (ANCIENTFS_211BSD_DIRBLKSIZ - 1)) ||
         ep->d_reclen < ANCIENTFS_211BSD_DIRSIZ(ep) ||
         ep->d_namlen > UNIXFS_MAXNAMLEN)
        return 1;

    for (i = 0; i < ep->d_namlen; i++)
        if (ep->d_name[i] == '\0')
            return 1;

    return ep->d_name[i];
}

static int
unixfs_internal_namei(ino_t parentino, const char* name, struct stat* stbuf)
{
    if (parentino == MACFUSE_ROOTINO)
        parentino = ROOTINO;

    stbuf->st_ino = 0;

    struct inode* dp = unixfs_internal_iget(parentino);
    if (!dp)
        return ENOENT;

    if (!S_ISDIR(dp->I_mode)) {
        unixfs_internal_iput(dp);
        return ENOTDIR;
    }

    int ret = ENOENT, i, found = 0;
    off_t ni_offset = 0, entryoffsetinblock = 0;
    int endsearch = roundup(dp->I_size, ANCIENTFS_211BSD_DIRBLKSIZ);

    struct direct* ep;
    char ubuf[UNIXFS_IOSIZE(unixfs)];

    size_t namlen = strlen(name);

    while (ni_offset < endsearch) {

        if (blkoff(ni_offset) == 0) {
            ret = __unixfs_internal_blkatoff(dp, ni_offset, ubuf);
            if (ret) {
                ret = ENOENT;
                goto out;
            }
            entryoffsetinblock = 0;
        }
        ep = (struct direct*)((char*)ubuf + entryoffsetinblock);
        ep->d_ino = fs16_to_host(unixfs->s_endian, ep->d_ino);
        ep->d_reclen = fs16_to_host(unixfs->s_endian, ep->d_reclen);
        ep->d_namlen = fs16_to_host(unixfs->s_endian, ep->d_namlen);
        if (ep->d_reclen == 0 ||
            __unixfs_internal_dirbadentry(ep, entryoffsetinblock)) {
            i = ANCIENTFS_211BSD_DIRBLKSIZ -
                    (entryoffsetinblock & (ANCIENTFS_211BSD_DIRBLKSIZ - 1));
            ni_offset += i;
            entryoffsetinblock += i;
            continue;
        }
        if (ep->d_ino) {
            if ((namlen == ep->d_namlen) &&
                bcmp(name, ep->d_name, ep->d_namlen) == 0) {
                found = 1;
                break;
            }
        }
        ni_offset += ep->d_reclen;
        entryoffsetinblock += ep->d_reclen;

    } /* while */

out:
    unixfs_internal_iput(dp);

    if (found) /* matched */
        ret = unixfs_internal_igetattr((ino_t)(ep->d_ino), stbuf);

    return ret;
}

static int
unixfs_internal_nextdirentry(struct inode* dp, struct unixfs_dirbuf* dirbuf,
                             off_t* offset, struct unixfs_direntry* dent)
{
    struct direct* ep;
    off_t ni_offset = *offset;
    off_t entryoffsetinblock = blkoff(ni_offset);
    int endsearch = roundup(dp->I_size, ANCIENTFS_211BSD_DIRBLKSIZ);

    if (ni_offset >= endsearch)
        return -1;

    if (!dirbuf->flags.initialized || (blkoff(ni_offset) == 0)) {
        int ret = __unixfs_internal_blkatoff(dp, ni_offset, dirbuf->data);
        if (ret)
            return ret;
        entryoffsetinblock = 0;
        dirbuf->flags.initialized = 1;
    }
    ep = (struct direct*)((char*)dirbuf->data + entryoffsetinblock);
    ep->d_ino = fs16_to_host(unixfs->s_endian, ep->d_ino);
    ep->d_reclen = fs16_to_host(unixfs->s_endian, ep->d_reclen);
    ep->d_namlen = fs16_to_host(unixfs->s_endian, ep->d_namlen);
    if (ep->d_reclen == 0 ||
        __unixfs_internal_dirbadentry(ep, entryoffsetinblock)) {
        int i =
            ANCIENTFS_211BSD_DIRBLKSIZ - (entryoffsetinblock &
                (ANCIENTFS_211BSD_DIRBLKSIZ - 1));
        ni_offset += i;
        entryoffsetinblock += i;
        dent->ino = 0;
    } else {
        dent->ino = (ino_t)ep->d_ino;
        memcpy(dent->name, ep->d_name, ep->d_namlen);
        dent->name[ep->d_namlen] = '\0';
        ni_offset += ep->d_reclen;
        entryoffsetinblock += ep->d_reclen;
    }

    *offset = ni_offset;

    return 0;
}

static ssize_t
unixfs_internal_pbread(struct inode* ip, char* buf, size_t nbyte, off_t offset,
                       int* error)
{
    ssize_t done = 0;
    size_t tomove = 0;
    ssize_t remaining = nbyte;
    ssize_t iosize = UNIXFS_IOSIZE(unixfs);
    char blkbuf[iosize];
    char* p = buf;

    while (remaining > 0) {
        off_t lbn = offset / DEV_BSIZE;
        off_t bn = unixfs_internal_bmap(ip, lbn, error);
        if (UNIXFS_BADBLOCK(bn, *error))
            break;
        *error = unixfs_internal_bread(bn, blkbuf);
        if (*error != 0)
            break;
        tomove = (remaining > iosize) ? iosize : remaining;
        memcpy(p, blkbuf, tomove);
        remaining -= tomove;
        done += tomove;
        offset += tomove;
        p += tomove;
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

    int error;

    off_t bn = unixfs_internal_bmap(ip, (off_t)0, &error);
    if (UNIXFS_BADBLOCK(bn, error))
        goto out;

    /* we know MAXPATHLEN (256) < DEV_BSIZE (1024) == UNIXFS_MAXPATHLEN */
    error = unixfs_internal_bread(bn, path);
    if (error)
        goto out;

    size_t linklen = (ip->I_size > DEV_BSIZE - 1) ? DEV_BSIZE - 1: ip->I_size;
    path[linklen] = '\0';
    error = 0;

out:
    unixfs_internal_iput(ip);

    return error;
}

static int
unixfs_internal_sanitycheck(void* filsys, off_t disksize)
{
    struct fs* fs = (struct fs*)filsys;

    if ((off_t)(fs->s_fsize * DEV_BSIZE) > disksize) {
        fprintf(stderr, "*** disk image seems smaller than the volume\n");
        return -1;
    }

    if (fs->s_nfree > NICFREE) {
        fprintf(stderr, "*** warning: implausible s_nfree %hu\n", fs->s_nfree);
        return -1;
    }

    if (fs->s_ninode > NICINOD) {
        fprintf(stderr,
                "*** warning: implausible s_ninode %hu\n", fs->s_ninode);
        return -1;
    }

    if (fs->s_time == 0) {
        fprintf(stderr, "*** warning: implausible timestamp of 0\n");
        return -1;
    }

    struct stat stbuf;
    int ret = unixfs_internal_igetattr((ino_t)ROOTINO, &stbuf);
    if (ret) {
        fprintf(stderr, "*** warning: failed to get root inode\n");
        return -1;
    }

    if (!S_ISDIR(stbuf.st_mode)) {
        fprintf(stderr, "*** warning: root inode is not a directory\n");
        return -1;
    }

    if (stbuf.st_size == 0) {
        fprintf(stderr, "*** warning: root inode has zero size\n");
        return -1;
    }

    return 0;
}

static int
unixfs_internal_statvfs(struct statvfs* svb)
{
    memcpy(svb, &unixfs->s_statvfs, sizeof(struct statvfs));
    return 0;
}
