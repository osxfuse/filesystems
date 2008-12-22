/*
 * Ancient UNIX File Systems for MacFUSE
 * Amit Singh
 * http://osxbook.com
 */

#include "ancientfs_v1,2,3.h"
#include "unixfs_common.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

DECL_UNIXFS("UNIX V1/V2/V3", v123);

static void*
unixfs_internal_init(const char* dmg, uint32_t flags,
                     char** fsname, char** volname)
{
    int fd = -1;
    if ((fd = open(dmg, O_RDONLY)) < 0) {
        perror("open");
        return NULL;
    }

    int err, i, j;
    struct stat stbuf;
    struct super_block* sb = (struct super_block*)0;
    struct filsys* fs = (struct filsys*)0;

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

    fs = malloc(SBSIZE);
    if (!fs) {
        free(sb);
        err = ENOMEM;
        goto out;
    }

    if (pread(fd, fs, SBSIZE, SUPERB) != SBSIZE) {
        perror("pread");
        err = EIO;
        goto out;
    }

    unixfs = sb;

    unixfs->s_flags = flags; 
    unixfs->s_endian = UNIXFS_FS_PDP;
    unixfs->s_fs_info = (void*)fs;
    unixfs->s_bdev = fd;
   
    fs->s_bmapsz = fs16_to_host(unixfs->s_endian, fs->s_bmapsz);
    fs->s_bmap = (uint8_t*)((char*)fs + sizeof(a_int));
    fs->s_imapsz = fs16_to_host(unixfs->s_endian,
                       *(uint16_t*)((char*)fs + sizeof(a_int) + fs->s_bmapsz));
    fs->s_imap =
        (uint8_t*)((char*)fs + sizeof(a_int) + fs->s_imapsz + sizeof(a_int));

    unixfs->s_statvfs.f_bsize = BSIZE;
    unixfs->s_statvfs.f_frsize = BSIZE;
    unixfs->s_statvfs.f_blocks = fs->s_bmapsz * 8;

    unixfs->s_statvfs.f_bfree = 0;
    unixfs->s_statvfs.f_files = 0;
    unixfs->s_statvfs.f_ffree = 0;
    unixfs->s_statvfs.f_favail = 0;

    uint8_t* byte = fs->s_bmap;
    for (i = 0; i < fs->s_bmapsz; i++, byte++)
        for (j = 0; j < 8; j++)
            if ((*byte & (1 << j)))
                unixfs->s_statvfs.f_bfree++;
    unixfs->s_statvfs.f_bavail = unixfs->s_statvfs.f_bavail;

    byte = fs->s_imap;
    for (i = 0; i < fs->s_imapsz; i++, byte++)
        for (j = 0; j < 8; j++)
            if ((*byte & (1 << j)))
                unixfs->s_statvfs.f_files++;
            else
                unixfs->s_statvfs.f_ffree++;
    unixfs->s_statvfs.f_favail = unixfs->s_statvfs.f_favail;

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

    unixfs->s_dentsize = DIRSIZ + 2;
    unixfs->s_statvfs.f_namemax = DIRSIZ;

    (void)unixfs_fstype;

    snprintf(unixfs->s_fsname, UNIXFS_MNAMELEN, "UNIX %s",
             (flags & ANCIENTFS_UNIX_V1) ? "V1" :
             (flags & ANCIENTFS_UNIX_V2) ? "V2" :
             (flags & ANCIENTFS_UNIX_V3) ? "V3" : "V?");

    char* dmg_basename = basename((char*)dmg);
    snprintf(unixfs->s_volname, UNIXFS_MAXNAMLEN, "UNIX %s (disk=%s)",
             (flags & ANCIENTFS_UNIX_V1) ? "V1" :
             (flags & ANCIENTFS_UNIX_V2) ? "V2" :
             (flags & ANCIENTFS_UNIX_V3) ? "V3" : "V?",
             (dmg_basename) ? dmg_basename : "Disk Image");

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
    return (off_t)0;
}

static off_t
unixfs_internal_bmap(struct inode* ip, off_t lblkno, int* error)
{
    a_int bn = (a_int)lblkno;

    if (bn & ~077777) {
        *error = EFBIG;
        return 0;
    }

    *error = EROFS;

    int ret;
    a_int i, nb;
    a_int* bap;
    char ubuf[UNIXFS_IOSIZE(unixfs)];

    if (((a_int)ip->I_mode & ILARG) == 0) {
        /* small file algorithm */
        if ((bn & ~7) != 0) /* convert small to large */
            return 0; /* !writable */
        nb = (a_int)(ip->I_daddr[bn]);
        if (nb == 0)
            return 0; /* !writable */
        *error = 0;
        return (off_t)nb;
    }

/* large: */

    i = bn >> 8;
    if (i >= 8) {
        *error = EFBIG;
        return 0;
    }
    if ((nb = (a_int)(ip->I_daddr[i])) == 0)
        return 0; /* !writable */
    else {
        ret = unixfs_internal_bread((off_t)nb, ubuf);
        if (ret) {
            *error = ret;
            return 0;
        }
    }

    bap = (a_int*)ubuf;

    i = bn & 0377;
    if ((nb = fs16_to_host(unixfs->s_endian, bap[i])) == 0)
        return 0; /* !writable */

    *error = 0;

    return (off_t)nb;
}

static int
unixfs_internal_bread(off_t blkno, char* blkbuf)
{
    if (blkno >= (((struct filsys*)unixfs->s_fs_info)->s_bmapsz * 8)) {
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
              blkno * (off_t)BSIZE) != UNIXFS_IOSIZE(unixfs))
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
        fprintf(stderr, "*** fatal error: no inode for %llu\n", (ino64_t)ino);
        abort();
    }

    if (ip->I_initialized)
        return ip;

    char ubuf[UNIXFS_IOSIZE(unixfs)];
    off_t blkno = (off_t)(((a_ino_t)ino + 31) / 16);

    if (unixfs_internal_bread(blkno, ubuf) != 0) {
        unixfs_inodelayer_ifailed(ip);
        return NULL;
    }

    struct dinode* dip =
        (struct dinode*)(ubuf + (32 * (((a_ino_t)ino + 31) % 16)));

    ip->I_number = ino;

    ip->I_mode  = fs16_to_host(unixfs->s_endian, dip->di_flags);
    ip->I_nlink = dip->di_nlink;
    ip->I_uid   = dip->di_uid;
    ip->I_size  = fs16_to_host(unixfs->s_endian, dip->di_size);

    uint16_t t0 = fs16_to_host(unixfs->s_endian, dip->di_mtime[0]);
    uint16_t t1 = fs16_to_host(unixfs->s_endian, dip->di_mtime[1]);
    uint32_t t = t0 << 16 | t1;
    ip->I_atime_sec = ip->I_mtime_sec = ip->I_ctime_sec =
        ancientfs_v123_time(t, unixfs->s_flags);

#ifndef __linux__
    t0 = fs16_to_host(unixfs->s_endian, dip->di_crtime[0]);
    t1 = fs16_to_host(unixfs->s_endian, dip->di_crtime[1]);
    t = t0 << 16 | t1;
    ip->I_crtime_sec = ancientfs_v123_time(t, unixfs->s_flags);
#endif

    int i;

    for (i = 0; i < 8; i++)
        ip->I_daddr[i] = fs16_to_host(unixfs->s_endian, dip->di_addr[i]);

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
    stbuf->st_mode = ancientfs_v123_mode(ip->I_mode, unixfs->s_flags);
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

    if (!S_ISDIR(ancientfs_v123_mode(dp->I_mode, unixfs->s_flags))) {
        unixfs_internal_iput(dp);
        return ENOTDIR;
    }

    int ret = ENOENT, eo = 0, count = dp->I_size / unixfs->s_dentsize;
    a_int offset = 0;
    char ubuf[UNIXFS_IOSIZE(unixfs)];
    struct dent udent;

eloop:
    if (count == 0) {
        ret = ENOENT;
        goto out;
    }

    if ((offset & 0777) == 0) {
        off_t blkno = unixfs_internal_bmap(dp, (off_t)(offset/BSIZE), &ret);
        if (UNIXFS_BADBLOCK(blkno, ret))
            goto out;
        if (unixfs_internal_bread(blkno, ubuf) != 0)
            goto out;
    }

    memset(&udent, 0, sizeof(udent));
    memcpy(&udent, ubuf + (offset & 0777), unixfs->s_dentsize);

    udent.u_ino = fs16_to_host(unixfs->s_endian, udent.u_ino);

    offset += unixfs->s_dentsize;
    count--;

    if (udent.u_ino == 0) {
        if (eo == 0)
            eo = offset;
        goto eloop;
    }

    if (strncmp(name, udent.u_name, DIRSIZ) != 0)
        goto eloop;

    /* matched */

    ret = unixfs_internal_igetattr((ino_t)(udent.u_ino), stbuf);

out:
    unixfs_internal_iput(dp);

    return ret;
}

static int
unixfs_internal_nextdirentry(struct inode* dp, struct unixfs_dirbuf* dirbuf,
                             off_t* offset, struct unixfs_direntry* dent)
{
    if ((*offset + unixfs->s_dentsize) > dp->I_size)
        return -1;

    if (!dirbuf->flags.initialized || ((*offset & 0777) == 0)) {
        int ret;
        off_t blkno = unixfs_internal_bmap(dp, (off_t)(*offset / BSIZE), &ret);
        if (UNIXFS_BADBLOCK(blkno, ret))
            return ret;
        ret = unixfs_internal_bread(blkno, dirbuf->data);
        if (ret != 0)
            return ret;
        dirbuf->flags.initialized = 1;
    }

    struct dent udent;
    size_t dirnamelen = min(DIRSIZ, UNIXFS_MAXNAMLEN);

    memset(&udent, 0, sizeof(udent));
    memcpy(&udent, dirbuf->data + (*offset & 0777), unixfs->s_dentsize);
    udent.u_ino = fs16_to_host(unixfs->s_endian, udent.u_ino);
    dent->ino = udent.u_ino;
    memcpy(dent->name, udent.u_name, dirnamelen);
    dent->name[dirnamelen] = '\0';

    *offset += unixfs->s_dentsize;

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
        off_t lbn = offset / BSIZE;
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
    return ENOSYS;
}

static int
unixfs_internal_sanitycheck(void* filsys, off_t disksize)
{
    struct filsys* fs = (struct filsys*)filsys;

    if (fs->s_bmapsz % 2) {
        fprintf(stderr, "*** storage free map byte count is not even\n");
        return -1;
    }

    if (fs->s_imapsz % 2) {
        fprintf(stderr, "*** i-node free map byte count is not even\n");
        return -1;
    }

    if ((off_t)(fs->s_bmapsz * 8 * BSIZE) > disksize) {
        fprintf(stderr, "*** disk image seems smaller than the volume\n");
        return -1;
    }

    if (((fs->s_imapsz / INOPB) + 2) > (fs->s_bmapsz * 8)) {
        fprintf(stderr, "*** inconsistent number of disk blocks\n");
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
