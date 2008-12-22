/*
 * Ancient UNIX File Systems for MacFUSE
 * Amit Singh
 * http://osxbook.com
 */

#include "ancientfs_itp.h"
#include "unixfs_common.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

DECL_UNIXFS("UNIX itp", itp);

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
    uint32_t tapedir_begin_block = 0, tapedir_end_block = 0, last_block = 0;

    if ((err = fstat(fd, &stbuf)) != 0) {
        perror("fstat");
        goto out;
    }

    if (!S_ISREG(stbuf.st_mode) && !(flags & UNIXFS_FORCE)) {
        err = EINVAL;
        fprintf(stderr, "%s is not a tape image file\n", dmg);
        goto out;
    }

    if (flags & ANCIENTFS_DECTAPE) {
        tapedir_begin_block = 0;
        tapedir_end_block = TAPEDIR_END_BLOCK_DEC;
    } else if (flags & ANCIENTFS_MAGTAPE) {
        tapedir_begin_block = 0;
        tapedir_end_block = TAPEDIR_END_BLOCK_MAG;
    } else if (flags & ANCIENTFS_GENTAPE) {
        tapedir_begin_block = 0;
        tapedir_end_block = TAPEDIR_END_BLOCK_GENERIC;
    } else {
        err = EINVAL;
        fprintf(stderr, "unrecognized tape type\n");
        goto out;
    }

    if (S_ISREG(stbuf.st_mode))
        last_block = stbuf.st_size / BSIZE;
    else
        last_block = tapedir_end_block;

    sb = malloc(sizeof(struct super_block));
    if (!sb) {
        err = ENOMEM;
        goto out;
    }

    assert(sizeof(struct filsys) <= BSIZE);

    fs = calloc(1, BSIZE);
    if (!fs) {
        free(sb);
        err = ENOMEM;
        goto out;
    }

    unixfs = sb;

    unixfs->s_flags = flags;
    unixfs->s_endian = UNIXFS_FS_PDP;
    unixfs->s_fs_info = (void*)fs;
    unixfs->s_bdev = fd;

    /* must initialize the inode layer before sanity checking */
    if ((err = unixfs_inodelayer_init(sizeof(struct tap_node_info))) != 0)
        goto out;

    struct inode* rootip = unixfs_inodelayer_iget((ino_t)ROOTINO);
    if (!rootip) {
        fprintf(stderr, "*** fatal error: no root inode\n");
        abort();
    }

    rootip->I_mode = S_IFDIR | 0755;
    rootip->I_uid  = getuid();
    rootip->I_gid  = getgid();
    rootip->I_size = 2;
    rootip->I_atime_sec = rootip->I_mtime_sec = rootip->I_ctime_sec =        time(0);

    struct tap_node_info* rootti = (struct tap_node_info*)rootip->I_private;
    rootti->ti_self = rootip;
    rootti->ti_name[0] = '\0';
    rootti->ti_parent = NULL;
    rootti->ti_children = NULL;
    rootti->ti_next_sibling = NULL;

    unixfs_inodelayer_isucceeded(rootip);

    fs->s_fsize = stbuf.st_size / BSIZE;
    fs->s_files = 0;
    fs->s_directories = 1 + 1 + 1;
    fs->s_rootip = rootip;
    fs->s_lastino = ROOTINO;

    char tapeblock[BSIZE];

    for (i = tapedir_begin_block; i < tapedir_end_block; i++) {
        if (pread(fd, tapeblock, BSIZE, (off_t)(i * BSIZE)) != BSIZE) {
            fprintf(stderr, "*** fatal error: cannot read tape block %llu\n",
                    (off_t)i);
            err = EIO;
            goto out;
        }

        struct dinode_itp* di = (struct dinode_itp*)tapeblock;
        
        for (j = 0; j < INOPB; j++, di++) {

            if (ancientfs_itp_cksum((uint8_t*)di,
                                 unixfs->s_flags, unixfs->s_endian) != 0)
                continue;

            if (!di->di_path[0]) {
                if (flags & ANCIENTFS_GENTAPE)
                    i = tapedir_end_block;
                continue;
            }

            ino_t parent_ino = ROOTINO;

            char* path = (char*)di->di_path;

            size_t pathlen = strlen(path);
            if ((*path == '.') && ((pathlen == 1) ||
                                  ((pathlen == 2) && (*(path + 1) == '/')))) {
                /* root */
                rootip->I_mode = fs16_to_host(unixfs->s_endian, di->di_mode);
                rootip->I_atime_sec = \
                    rootip->I_mtime_sec = \
                        rootip->I_ctime_sec = \
                            fs32_to_host(unixfs->s_endian, di->di_mtime);
                continue;
            }

            /* we don't deal with many fancy paths here: just '/' and './' */
            if (*path == '/')
                path++;
            else if (*path == '.' && *(path + 1) == '/')
                path += 2;

            char *cnp, *term;

            for (cnp = strtok_r(path, "/", &term); cnp;
                cnp = strtok_r(NULL, "/", &term)) {
                /* we have { parent_ino, cnp } */
                struct stat stbuf;
                int missing = unixfs_internal_namei(parent_ino, cnp, &stbuf);
                if (!missing) {
                    parent_ino = stbuf.st_ino;
                    continue;
                }
                struct inode* ip =
                    unixfs_inodelayer_iget((ino_t)(fs->s_lastino + 1));
                if (!ip) {
                    fprintf(stderr, "*** fatal error: no inode for %llu\n",
                            (ino_t)(fs->s_lastino + 1));
                    abort();
                }
                ip->I_mode = fs16_to_host(unixfs->s_endian, di->di_mode);
                ip->I_uid  = di->di_uid;
                ip->I_gid  = di->di_gid;
                ip->I_size = di->di_size0 << 16 |
                               fs16_to_host(unixfs->s_endian, di->di_size1);
                ip->I_daddr[0] = (uint32_t)fs16_to_host(unixfs->s_endian,
                                                        di->di_addr);
                ip->I_nlink = 1;
                ip->I_atime_sec = ip->I_mtime_sec = ip->I_ctime_sec =
                    fs32_to_host(unixfs->s_endian, di->di_mtime);
                struct tap_node_info* ti = (struct tap_node_info*)ip->I_private;
                memcpy(ti->ti_name, cnp, strlen(cnp));
                ti->ti_self = ip;
                ti->ti_children = NULL;
                /* this should work out as long as we have no corruption */
                struct inode* parent_ip = unixfs_internal_iget(parent_ino);
                parent_ip->I_size += 1;
                ti->ti_parent = (struct tap_node_info*)(parent_ip->I_private);
                ti->ti_next_sibling = ti->ti_parent->ti_children;
                ti->ti_parent->ti_children = ti;
                if (S_ISDIR(ancientfs_itp_mode(ip->I_mode, flags))) {
                    fs->s_directories++;
                    parent_ino = fs->s_lastino + 1;
                    ip->I_size = 2;
                    ip->I_daddr[0] = 0;
                } else
                    fs->s_files++;
                fs->s_lastino++;
                unixfs_internal_iput(parent_ip);
                unixfs_inodelayer_isucceeded(ip);
                /* no put */
            }
        }
    }

    unixfs->s_statvfs.f_bsize = BSIZE;
    unixfs->s_statvfs.f_frsize = BSIZE;
    unixfs->s_statvfs.f_ffree = 0;
    unixfs->s_statvfs.f_files = fs->s_files + fs->s_directories;
    unixfs->s_statvfs.f_blocks = fs->s_fsize;
    unixfs->s_statvfs.f_bfree = 0;
    unixfs->s_statvfs.f_bavail = 0;
    unixfs->s_dentsize = 1;
    unixfs->s_statvfs.f_namemax = DIRSIZ;

    snprintf(unixfs->s_fsname, UNIXFS_MNAMELEN, "UNIX itp");

    char* dmg_basename = basename((char*)dmg);
    snprintf(unixfs->s_volname, UNIXFS_MAXNAMLEN, "%s (tape=%s)",
             unixfs_fstype, (dmg_basename) ? dmg_basename : "Tape Image");

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
    struct super_block* sb = (struct super_block*)filsys;
    struct filsys* fs = (struct filsys*)sb->s_fs_info;
    ino_t i = fs->s_lastino;
    for (; i >= ROOTINO; i--) {
        struct inode* tmp = unixfs_internal_iget(i);
        if (tmp) {
            unixfs_internal_iput(tmp);
            unixfs_internal_iput(tmp);
        }
    }

    unixfs_inodelayer_fini();

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
    off_t nblocks = (off_t)((ip->I_size + (BSIZE - 1)) / BSIZE);

    if (lblkno >= nblocks) {
        *error = EFBIG;
        return (off_t)0; 
    }

    return (off_t)(ip->I_daddr[0] + lblkno);
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

    if (pread(unixfs->s_bdev, blkbuf, UNIXFS_IOSIZE(unixfs),
              blkno * (off_t)BSIZE) != UNIXFS_IOSIZE(unixfs))
        return EIO;

    return 0;
}

static struct inode*
unixfs_internal_iget(ino_t ino)
{
    struct inode* ip = unixfs_inodelayer_iget(ino);
    if (!ip) {
        fprintf(stderr, "*** fatal error: no inode for %llu\n", ino);
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
    stbuf->st_mode = ancientfs_itp_mode(ip->I_mode, unixfs->s_flags);
}

static int
unixfs_internal_namei(ino_t parentino, const char* name, struct stat* stbuf)
{
    int ret = ENOENT;
    stbuf->st_ino = 0;

    size_t namelen = strlen(name);
    if (namelen > DIRSIZ)
        return ENAMETOOLONG;

    struct inode* dp = unixfs_internal_iget(parentino);
    if (!dp)
        return ENOENT;

    if (!S_ISDIR(ancientfs_itp_mode(dp->I_mode, unixfs->s_flags))) {
        ret = ENOTDIR;
        goto out;
    }

    struct tap_node_info* child =
        ((struct tap_node_info*)dp->I_private)->ti_children;;

    if (!child) {
        ret = ENOENT;
        goto out;
    }

    int found = 0;

    do {
        size_t target_namelen = strlen((const char*)child->ti_name);
        if ((namelen == target_namelen) &&
            (memcmp(name, child->ti_name, target_namelen) == 0)) {
            found = 1;
            break;
        }
        child = child->ti_next_sibling;
    } while (child);

    if (found)
        ret = unixfs_internal_igetattr((ino_t)child->ti_self->I_ino, stbuf);

out:
    unixfs_internal_iput(dp);

    return ret;
}

static int
unixfs_internal_nextdirentry(struct inode* dp, struct unixfs_dirbuf* dirbuf,
                             off_t* offset, struct unixfs_direntry* dent)
{
    if (*offset >= dp->I_size)
        return -1;

    if (*offset < 2) {
        int idx = 0;
        dent->name[idx++] = '.';
        dent->ino = ROOTINO;
        if (*offset == 1) {
            if (dp->I_ino != ROOTINO) {
                struct inode* pdp = unixfs_internal_iget(dp->I_ino);
                if (pdp) {
                    dent->ino = pdp->I_ino;
                    unixfs_internal_iput(pdp);
                }
            }
            dent->name[idx++] = '.';
        }
        dent->name[idx++] = '\0';
        goto out;
    }

    struct tap_node_info* child =
        ((struct tap_node_info*)dp->I_private)->ti_children;;

    off_t i;

    for (i = 0; i < (*offset - 2); i++)
        child = child->ti_next_sibling;

    dent->ino = (ino_t)child->ti_self->I_ino;
    size_t dirnamelen = min(DIRSIZ, UNIXFS_MAXNAMLEN);
    memcpy(dent->name, child->ti_name, dirnamelen);
    dent->name[dirnamelen] = '\0';

out:
    *offset += 1;

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
    return 0;
}

static int
unixfs_internal_statvfs(struct statvfs* svb)
{
    memcpy(svb, &unixfs->s_statvfs, sizeof(struct statvfs));
    return 0;
}
