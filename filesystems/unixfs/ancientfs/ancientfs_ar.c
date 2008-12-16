/*
 * Ancient UNIX File Systems for MacFUSE
 * Amit Singh
 * http://osxbook.com
 */

#include "ancientfs_ar.h"
#include "unixfs_common.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

DECL_UNIXFS("UNIX ar", ar);

/*
 * The header reading function is adapted from the BSD version.
 */

/*-
 * Copyright (c) 1990, 1993, 1994
 *      The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Hugh Smith at The University of Guelph.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* Convert ar header field to an integer. */
#define AR_ATOI(from, to, len, base) { \
        memmove(buf, from, len); \
        buf[len] = '\0'; \
        to = strtol(buf, (char **)NULL, base); \
}

struct chdr {
    off_t   size;  /* size of the object in bytes */
    off_t   addr;  /* where the file begins */
    time_t  date;  /* date */
    int     lname; /* size of the long name in bytes */
    gid_t   gid;   /* group */
    uid_t   uid;   /* owner */
    u_short mode;  /* permissions */
    char    name[UNIXFS_MAXNAMLEN + 1]; /* name */
};

static int ancientfs_ar_readheader(int fd, struct chdr* chdr);

static int
ancientfs_ar_readheader(int fd, struct chdr* chdr)
{
    int len, nr;
    char *p, buf[20];
    char hb[sizeof(struct ar_hdr) + 1];
    struct ar_hdr* hdr;

    nr = read(fd, hb, sizeof(struct ar_hdr));
    if (nr != sizeof(struct ar_hdr)) {
        if (!nr)
            return 1;
        if (nr < 0)
            return -1;
    }

    hdr = (struct ar_hdr*)hb;
    if (strncmp(hdr->ar_fmag, ARFMAG, sizeof(ARFMAG) - 1))
        return -2;

    /* Convert the header into the internal format. */
#define DECIMAL 10
#define OCTAL    8

    AR_ATOI(hdr->ar_date, chdr->date, sizeof(hdr->ar_date), DECIMAL);
    AR_ATOI(hdr->ar_uid, chdr->uid, sizeof(hdr->ar_uid), DECIMAL);
    AR_ATOI(hdr->ar_gid, chdr->gid, sizeof(hdr->ar_gid), DECIMAL);
    AR_ATOI(hdr->ar_mode, chdr->mode, sizeof(hdr->ar_mode), OCTAL);
    AR_ATOI(hdr->ar_size, chdr->size, sizeof(hdr->ar_size), DECIMAL);

    /* Leading spaces should never happen. */
    if (hdr->ar_name[0] == ' ')
        return -2;

    /*
     * Long name support.  Set the "real" size of the file, and the
     * long name flag/size.
     */
    if (!bcmp(hdr->ar_name, AR_EFMT1, sizeof(AR_EFMT1) - 1)) {
        chdr->lname = len = atoi(hdr->ar_name + sizeof(AR_EFMT1) - 1);
        if (len <= 0 || len > UNIXFS_MAXNAMLEN)
                return -1;
        nr = read(fd, chdr->name, len);
        if (nr != len) {
            if (nr < 0)
                return -1; 
        }
        chdr->name[len] = 0;
        chdr->size -= len;
    } else {
        memmove(chdr->name, hdr->ar_name, sizeof(hdr->ar_name));

        /* Strip trailing spaces, null terminate. */
        for (p = chdr->name + sizeof(hdr->ar_name) - 1; *p == ' '; --p);
        *++p = '\0';
        chdr->lname = strlen(chdr->name);
    }

    /* limiting to 32-bit offsets */
    chdr->addr = (uint32_t)lseek(fd, (off_t)0, SEEK_CUR);

    return 0;
}

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
    struct filsys* fs = (struct filsys*)0;

    if ((err = fstat(fd, &stbuf)) != 0) {
        perror("fstat");
        goto out;
    }

    if (!S_ISREG(stbuf.st_mode) && !(flags & UNIXFS_FORCE)) {
        err = EINVAL;
        fprintf(stderr, "%s is not an ar image file\n", dmg);
        goto out;
    }

    char magic[SARMAG];
    if (read(fd, magic, SARMAG) != SARMAG) {
        err = EIO;
        fprintf(stderr, "failed to read magic from file\n");
        goto out;
    }

    if (memcmp(magic, ARMAG, SARMAG) != 0) {
        err = EINVAL;
        fprintf(stderr, "%s is not an ar image file\n", dmg);
        goto out;
    }

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
    unixfs->s_endian = UNIXFS_FS_LITTLE; /* unused */
    unixfs->s_fs_info = (void*)fs;
    unixfs->s_bdev = fd;

    /* must initialize the inode layer before sanity checking */
    if ((err = unixfs_inodelayer_init(sizeof(struct ar_node_info))) != 0)
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
    rootip->I_atime.tv_sec = rootip->I_mtime.tv_sec = rootip->I_ctime.tv_sec =        time(0);

    struct ar_node_info* rootai = (struct ar_node_info*)rootip->I_private;
    rootai->ar_self = rootip;
    rootai->ar_parent = NULL;
    rootai->ar_children = NULL;
    rootai->ar_next_sibling = NULL;

    unixfs_inodelayer_isucceeded(rootip);

    fs->s_fsize = (stbuf.st_size / BSIZE) + 1;
    fs->s_files = 0;
    fs->s_directories = 1 + 1 + 1;
    fs->s_rootip = rootip;
    fs->s_lastino = ROOTINO;

    struct chdr ar;
    ino_t parent_ino = ROOTINO;

    for (;;) {

        if (ancientfs_ar_readheader(fd, &ar) != 0)
            break;

        int missing = unixfs_internal_namei(parent_ino, ar.name, &stbuf);
        if (!missing) /* duplicate */
            goto next;

        struct inode* ip = unixfs_inodelayer_iget((ino_t)(fs->s_lastino + 1));
        if (!ip) {
            fprintf(stderr, "*** fatal error: no inode for %llu\n",
                   (ino_t)(fs->s_lastino + 1));
            abort();
        }
        ip->I_mode  = ar.mode;
        ip->I_uid   = ar.uid;
        ip->I_gid   = ar.gid;
        ip->I_nlink = 1;
        ip->I_size  = ar.size;
        ip->I_atime.tv_sec = ip->I_mtime.tv_sec = ip->I_ctime.tv_sec = ar.date;
        ip->I_daddr[0] = ar.addr;

        struct ar_node_info* ai = (struct ar_node_info*)ip->I_private;
        ai->ar_name = malloc(ar.lname + 1);
        if (!ai->ar_name) {
            fprintf(stderr, "*** fatal error: cannot allocate memory\n");
            abort();
        }

        memcpy(ai->ar_name, ar.name, ar.lname);
        ai->ar_name[ar.lname] = '\0';
        ai->ar_namelen = ar.lname;

        ai->ar_self = ip;
        ai->ar_children = NULL;
        struct inode* parent_ip = unixfs_internal_iget(parent_ino);
        parent_ip->I_size += 1;
        ai->ar_parent = (struct ar_node_info*)(parent_ip->I_private);
        ai->ar_next_sibling = ai->ar_parent->ar_children;
        ai->ar_parent->ar_children = ai;
        if (S_ISDIR(ip->I_mode)) {
            fs->s_directories++;
            parent_ino = fs->s_lastino + 1;
            ip->I_size = 2;
            ip->I_daddr[0] = 0;
        } else {
            fs->s_files++;
            fs->s_lastino++;
            unixfs_internal_iput(parent_ip);
            unixfs_inodelayer_isucceeded(ip);
           /* no put */
        }

        fs->s_lastino++;
next:
        (void)lseek(fd, (off_t)(ar.size + (ar.size & 1)), SEEK_CUR);
    }

    unixfs->s_statvfs.f_bsize = BSIZE;
    unixfs->s_statvfs.f_frsize = BSIZE;
    unixfs->s_statvfs.f_ffree = 0;
    unixfs->s_statvfs.f_files = fs->s_files + fs->s_directories;
    unixfs->s_statvfs.f_blocks = fs->s_fsize;
    unixfs->s_statvfs.f_bfree = 0;
    unixfs->s_statvfs.f_bavail = 0;
    unixfs->s_dentsize = 1;
    unixfs->s_statvfs.f_namemax = UNIXFS_MAXNAMLEN;

    snprintf(unixfs->s_fsname, UNIXFS_MNAMELEN, "UNIX ar");

    char* dmg_basename = basename((char*)dmg);
    snprintf(unixfs->s_volname, UNIXFS_MAXNAMLEN, "%s (tape=%s)",
             unixfs_fstype, (dmg_basename) ? dmg_basename : "Archive Image");

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
            struct ar_node_info* ai = (struct ar_node_info*)tmp->I_private;
            if (ai->ar_name)
                free(ai->ar_name);
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
    return (off_t)0;
}

static int
unixfs_internal_bread(off_t blkno, char* blkbuf)
{
    return EIO;
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
}

static int
unixfs_internal_namei(ino_t parentino, const char* name, struct stat* stbuf)
{
    int ret = ENOENT;
    stbuf->st_ino = 0;

    size_t namelen = strlen(name);
    if (namelen > UNIXFS_MAXNAMLEN)
        return ENAMETOOLONG;

    struct inode* dp = unixfs_internal_iget(parentino);
    if (!dp)
        return ENOENT;

    if (!S_ISDIR(dp->I_mode)) {
        ret = ENOTDIR;
        goto out;
    }

    struct ar_node_info* child =
        ((struct ar_node_info*)dp->I_private)->ar_children;;

    if (!child) {
        ret = ENOENT;
        goto out;
    }

    int found = 0;

    do {
        size_t target_namelen = strlen((const char*)child->ar_name);
        if ((namelen == target_namelen) &&
            (memcmp(name, child->ar_name, target_namelen) == 0)) {
            found = 1;
            break;
        }
        child = child->ar_next_sibling;
    } while (child);

    if (found)
        ret = unixfs_internal_igetattr((ino_t)child->ar_self->I_ino, stbuf);

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

    struct ar_node_info* child =
        ((struct ar_node_info*)dp->I_private)->ar_children;;

    off_t i;

    for (i = 0; i < (*offset - 2); i++)
        child = child->ar_next_sibling;

    dent->ino = (ino_t)child->ar_self->I_ino;
    size_t dirnamelen = min(child->ar_namelen, UNIXFS_MAXNAMLEN);
    memcpy(dent->name, child->ar_name, dirnamelen);
    dent->name[dirnamelen] = '\0';

out:
    *offset += 1;

    return 0;
}

static ssize_t
unixfs_internal_pbread(struct inode* ip, char* buf, size_t nbyte, off_t offset,
                       int* error)
{
    off_t start = (off_t)ip->I_daddr[0];

    /* caller already checked for bounds */

    return pread(unixfs->s_bdev, buf, nbyte, start + offset);
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
