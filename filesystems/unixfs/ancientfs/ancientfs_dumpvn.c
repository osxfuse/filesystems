/*
 * Ancient UNIX File Systems for MacFUSE
 * Amit Singh
 * http://osxbook.com
 */

#include "ancientfs_dumpvn.h"
#include "unixfs_common.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <assert.h>

#if UCB_NKB == 1
DECL_UNIXFS("UNIX dump1024/restor variable-length names", dumpvn1024);
#else
DECL_UNIXFS("UNIX dump/restor variable-length names", dumpvn);
#endif

static int ancientfs_dump_readheader(int fd, struct spcl* spcl);

static int
ancientfs_dump_readheader(int fd, struct spcl* spcl)
{
    ssize_t ret;

    if ((ret = read(fd, (char*)spcl, BSIZE)) != BSIZE) {
        if (ret == 0) /* EOF */
            return 1;
        return -1;
    }

    if (ancientfs_dump_cksum((uint16_t*)spcl, unixfs->s_endian,
                              unixfs->s_flags) != 0)
        return -1;

    spcl->c_magic    = fs16_to_host(unixfs->s_endian, spcl->c_magic);
    spcl->c_type     = fs16_to_host(unixfs->s_endian, spcl->c_type);
    spcl->c_date     = fs32_to_host(unixfs->s_endian, spcl->c_date);
    spcl->c_ddate    = fs32_to_host(unixfs->s_endian, spcl->c_ddate);
    spcl->c_volume   = fs16_to_host(unixfs->s_endian, spcl->c_volume);
    spcl->c_tapea    = fs32_to_host(unixfs->s_endian, spcl->c_tapea);
    spcl->c_inumber  = fs16_to_host(unixfs->s_endian, spcl->c_inumber);
    spcl->c_checksum = fs16_to_host(unixfs->s_endian, spcl->c_checksum);
    spcl->c_count    = fs16_to_host(unixfs->s_endian, spcl->c_count);

    struct dinode* di = &spcl->c_dinode;

    di->di_mode  = fs16_to_host(unixfs->s_endian, di->di_mode);
    di->di_nlink = fs16_to_host(unixfs->s_endian, di->di_nlink);
    di->di_uid   = fs16_to_host(unixfs->s_endian, di->di_uid);
    di->di_gid   = fs16_to_host(unixfs->s_endian, di->di_gid);
    di->di_size  = fs32_to_host(unixfs->s_endian, di->di_size);
    di->di_atime = fs32_to_host(unixfs->s_endian, di->di_atime);
    di->di_mtime = fs32_to_host(unixfs->s_endian, di->di_mtime);
    di->di_ctime = fs32_to_host(unixfs->s_endian, di->di_ctime);

    return 0;
}

static void*
unixfs_internal_init(const char* dmg, uint32_t flags, fs_endian_t fse,
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
    struct filsys* fs = (struct filsys*)0;

    assert(sizeof(struct spcl) == BSIZE);

    if ((err = fstat(fd, &stbuf)) != 0) {
        perror("fstat");
        goto out;
    }

    if (!S_ISREG(stbuf.st_mode) && !(flags & UNIXFS_FORCE)) {
        err = EINVAL;
        fprintf(stderr, "%s is not a tape dump image file\n", dmg);
        goto out;
    }

    if ((stbuf.st_size % TAPE_BSIZE) && !(flags & UNIXFS_FORCE)) {
        err = EINVAL;
        fprintf(stderr, "%s is not a multiple of tape block size\n", dmg);
        goto out;
    }

    if (S_ISREG(stbuf.st_mode) && (stbuf.st_size < TAPE_BSIZE)) {
        err = EINVAL;
        fprintf(stderr, "*** fatal error: %s is smaller in size than a "
                "physical tape block\n", dmg);
        goto out;
    }

    sb = malloc(sizeof(struct super_block));
    if (!sb) {
        err = ENOMEM;
        goto out;
    }

    fs = calloc(1, sizeof(struct filsys));
    if (!fs) {
        free(sb);
        err = ENOMEM;
        goto out;
    }

    unixfs = sb;

    unixfs->s_flags = flags;
    unixfs->s_endian = (fse == UNIXFS_FS_INVALID) ? UNIXFS_FS_PDP : fse;
    unixfs->s_fs_info = (void*)fs;
    unixfs->s_bdev = fd;

    unixfs->s_statvfs.f_bsize = BSIZE;
    unixfs->s_statvfs.f_frsize = BSIZE;

    fs->s_fsize += stbuf.st_size / BSIZE;

    /* must initialize the inode layer before sanity checking */
    if ((err = unixfs_inodelayer_init(sizeof(struct tap_node_info))) != 0)
        goto out;

    struct spcl spcl;

    if (ancientfs_dump_readheader(fd, &spcl) != 0) {
        fprintf(stderr, "failed to read dump header\n");
        err = EINVAL;
        goto out;
    }

    if (spcl.c_type != TS_TAPE) {
       fprintf(stderr, "failed to recognize image as a tape dump\n");
       err = EINVAL;
       goto out;
    }

    fs->s_date = fs32_to_host(unixfs->s_endian, spcl.c_date);
    fs->s_ddate = fs32_to_host(unixfs->s_endian, spcl.c_ddate);

    int done = 0;

    while (!done) {
        err = ancientfs_dump_readheader(fd, &spcl);
        if (err) {
            if (err != 1) {
                fprintf(stderr, "*** warning: no tape header: retrying\n");
                continue;
            } else {
                fprintf(stderr, "failed to read next header (%d)\n", err);
                err = EINVAL;
                goto out;
            }
        }

next:
        switch (spcl.c_type) {

        case TS_TAPE:
             break;

        case TS_END:
             done = 1;
             break;

        case TS_BITS:
            if (!fs->s_initialized) {
                fs->s_initialized = 1;
                int count = spcl.c_count;
                char* bmp = (char*)fs->s_dumpmap;
                while (count--) {
                   if (read(fd, bmp, BSIZE) != BSIZE) {
                       fprintf(stderr,
                               "*** fatal error: failed to read bitmap\n");
                       err = EIO;
                       goto out;
                   }
                   /* fix up endian-ness */
                   int idx;
                   for (idx = 0; idx < BSIZE/sizeof(uint16_t); idx++)
                       bmp[idx] = fs16_to_host(unixfs->s_endian, bmp[idx]);
                   bmp += BSIZE / sizeof(uint16_t);
               }
           } else {
               fprintf(stderr, "*** warning: duplicate inode map\n");
               /* ignore the data */
               (void)lseek(fd, (off_t)(spcl.c_count * BSIZE), SEEK_CUR);
           }
           break;

        case TS_INODE: {
            struct dinode* dip = &spcl.c_dinode;
            a_ino_t candidate = spcl.c_inumber;

            if ((!BIT_ON(candidate, fs->s_dumpmap)) || (candidate == BADINO))
                continue;

            struct inode* ip = unixfs_inodelayer_iget((ino_t)candidate);
            if (!ip) {
                fprintf(stderr, "*** fatal error: no inode for %llu\n",
                        (ino64_t)candidate);
                abort();
            }

            struct tap_node_info* ti = (struct tap_node_info*)ip->I_private;
            ti->ti_daddr = NULL;

            assert(!ip->I_initialized);

            ip->I_number       = (ino_t)candidate;
            ip->I_mode         = dip->di_mode;
            ip->I_nlink        = dip->di_nlink;
            ip->I_uid          = dip->di_uid;
            ip->I_gid          = dip->di_gid;
            ip->I_size         = dip->di_size;
            ip->I_atime_sec = dip->di_atime;
            ip->I_mtime_sec = dip->di_mtime;
            ip->I_ctime_sec = dip->di_ctime;

            if (S_ISDIR(ip->I_mode))
                fs->s_directories++;
            else
                fs->s_files++;

            /* populate i_daddr */
            
            off_t nblocks = (off_t)((ip->I_size + (BSIZE - 1)) / BSIZE);

            ti->ti_daddr = (uint32_t*)calloc(nblocks, sizeof(uint32_t));
            if (!ti->ti_daddr) {
                fprintf(stderr, "*** fatal error: cannot allocate memory\n");
                abort();
            }

            int block_index = 0;

            for (i = 0; i < nblocks; i++) {
                if (block_index >= spcl.c_count) {
                    if (ancientfs_dump_readheader(fd, &spcl) == -1) {
                        fprintf(stderr,
                                "*** fatal error: cannot read header\n");
                        abort();
                    }
                    if (spcl.c_type != TS_ADDR) {
                        fprintf(stderr, "*** warning: expected TS_ADDR but "
                                        "got %hd\n", spcl.c_type);
                        int k = i;
                        for (; k < nblocks; k++)
                            ti->ti_daddr[k] = 0;
                        goto next;
                    }
                    block_index = 0;
                }

                if (spcl.c_addr[block_index]) {
                    off_t nextb = lseek(fd, (off_t)BSIZE, SEEK_CUR);
                    if (nextb == -1) {
                        fprintf(stderr, "*** fatal error: cannot read tape\n");
                        abort();
                    }
                    ti->ti_daddr[i] = spcl.c_tapea + block_index + 1;
                } else {
                    ti->ti_daddr[i] = 0; /* zero fill */
                }

                block_index++;
            }

            if (S_ISCHR(ip->I_mode) || S_ISBLK(ip->I_mode)) {
                char* p1 = (char*)(ip->I_daddr);
                char* p2 = (char*)(dip->di_addr);
                for (i = 0; i < 4; i++) {
                    *p1++ = *p2++;
                    *p1++ = 0;
                    *p1++ = *p2++;
                    *p1++ = *p2++;
                }
                ip->I_daddr[0] = fs32_to_host(unixfs->s_endian, ip->I_daddr[0]);
                uint32_t rdev = ip->I_daddr[0];
                ip->I_rdev = makedev((rdev >> 8) & 255, rdev & 255);
            }

            if (ip->I_ino > fs->s_lastino)
                fs->s_lastino = ip->I_ino;

            unixfs_inodelayer_isucceeded(ip);
            }
            break;
         }
    }

    unixfs->s_statvfs.f_ffree = 0;
    unixfs->s_statvfs.f_files = fs->s_files + fs->s_directories;  
    unixfs->s_statvfs.f_blocks = fs->s_fsize;
    unixfs->s_statvfs.f_bfree = 0;
    unixfs->s_statvfs.f_bavail = 0;
    unixfs->s_dentsize = DIRSIZ + 2;
    unixfs->s_statvfs.f_namemax = DIRSIZ;

    fs->s_rootip = unixfs_internal_iget(ROOTINO);
    if (!fs->s_rootip) {
        unixfs_internal_fini(fs);
        err = EINVAL;
        goto out;
    }
    fs->s_rootip->I_mtime_sec = fs->s_date;
    fs->s_rootip->I_ctime_sec = fs->s_ddate;
    unixfs_internal_iput(fs->s_rootip);

    snprintf(unixfs->s_fsname, UNIXFS_MNAMELEN, "UNIX dump/restor");

    char* dmg_basename = basename((char*)dmg);
    snprintf(unixfs->s_volname, UNIXFS_MAXNAMLEN, "%s (tape=%s)",
             unixfs_fstype, (dmg_basename) ? dmg_basename : "Tape Image");

    *fsname = unixfs->s_fsname;
    *volname = unixfs->s_volname;

out:
    if (err) {
        if (fs)
            unixfs_internal_fini(fs);
        else if (sb)
            free(sb);
        if (fd >= 0)
            close(fd);
        return NULL;
    }

    return sb;
}

static void
unixfs_internal_fini(void* filsys)
{
    struct super_block* sb = (struct super_block*)filsys;
    struct filsys* fs = (struct filsys*)sb->s_fs_info;
    if (fs) {
        ino_t i = fs->s_lastino;
        for (; i >= (ino_t)ROOTINO; i--) {
            struct inode* tmp = unixfs_internal_iget(i);
            if (tmp) {
                struct tap_node_info* ti =
                    (struct tap_node_info*)tmp->I_private;
                unixfs_internal_iput(tmp);
                unixfs_internal_iput(tmp);
                if (ti->ti_daddr)
                    free(ti->ti_daddr);
            }
        }
    }

    unixfs_inodelayer_fini();

    if (sb) {
        if (sb->s_bdev >= 0)
            close(sb->s_bdev);
        sb->s_bdev = -1;
        if (fs)
            free(fs);
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
    off_t nblocks = (off_t)((ip->I_size + (BSIZE - 1)) / BSIZE);

    if (lblkno >= nblocks) {
        *error = EFBIG;
        return (off_t)0; 
    }

    *error = 0;

    return (off_t)(((struct tap_node_info*)ip->I_private)->ti_daddr[lblkno]);
}

static int
unixfs_internal_bread(off_t blkno, char* blkbuf)
{
    if (blkno >= ((struct filsys*)unixfs->s_fs_info)->s_fsize) {
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

    unixfs_inodelayer_ifailed(ip);

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

#define roundup(x, y) ((((x)+((y)-1))/(y))*(y))

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
    struct inode* ip = unixfs_internal_iget(ino);
    if (!ip)
        return ENOENT;

    int error;

    off_t bn = unixfs_internal_bmap(ip, (off_t)0, &error);
    if (UNIXFS_BADBLOCK(bn, error))
        goto out;

    /* we know MAXPATHLEN (256) < BSIZE (512/1024) == UNIXFS_MAXPATHLEN */
    error = unixfs_internal_bread(bn, path);
    if (error)
        goto out;

    size_t linklen = (ip->I_size > BSIZE - 1) ? BSIZE - 1: ip->I_size;
    path[linklen] = '\0';
    error = 0;

out:
    unixfs_internal_iput(ip);

    return error;
}

static int
unixfs_internal_sanitycheck(void* filsys, off_t disksize)
{
    return 0;
}

static int
unixfs_internal_statvfs(struct statvfs* svb)
{
    memcpy(svb, &unixfs->s_statvfs, sizeof(struct statvfs));
    return 0;
}
