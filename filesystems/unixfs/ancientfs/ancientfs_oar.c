/*
 * Ancient UNIX File Systems for MacFUSE
 * Amit Singh
 * http://osxbook.com
 */

#include "ancientfs_oar.h"
#include "unixfs_common.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

DECL_UNIXFS("UNIX Old ar", oar);

static int ancientfs_ar_readheader(int fd, struct ar_hdr* ar);

static int
ancientfs_ar_readheader(int fd, struct ar_hdr* ar)
{
    ssize_t ret;

    if ((ret = read(fd, ar, sizeof(struct ar_hdr)))
                    != sizeof(struct ar_hdr)) {
        if (ret == 0) /* EOF */
            return 1;
        return -1;
    }

    ar->ar_date = fs32_to_host(unixfs->s_endian, ar->ar_date);
    ar->ar_mode = fs16_to_host(unixfs->s_endian, ar->ar_mode);
    ar->ar_size = fs32_to_host(unixfs->s_endian, ar->ar_size);

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

    uint16_t magic;
    if (read(fd, &magic, sizeof(uint16_t)) != sizeof(uint16_t)) {
        err = EIO;
        fprintf(stderr, "failed to read magic from file\n");
        goto out;
    }

    magic = fs16_to_host(UNIXFS_FS_PDP, magic);

    if (magic != ARMAG) {
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
    unixfs->s_endian = UNIXFS_FS_PDP;
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
    rootip->I_atime_sec = rootip->I_mtime_sec = rootip->I_ctime_sec =        time(0);

    struct ar_node_info* rootai = (struct ar_node_info*)rootip->I_private;
    rootai->ar_self = rootip;
    rootai->ar_name[0] = '\0';
    rootai->ar_parent = NULL;
    rootai->ar_children = NULL;
    rootai->ar_next_sibling = NULL;

    unixfs_inodelayer_isucceeded(rootip);

    fs->s_fsize = (stbuf.st_size / BSIZE) + 1;
    fs->s_files = 0;
    fs->s_directories = 1 + 1 + 1;
    fs->s_rootip = rootip;
    fs->s_lastino = ROOTINO;

    char cnp[DIRSIZ + 1];
    struct ar_hdr ar;
    ino_t parent_ino = ROOTINO;

    for (;;) {

        if (ancientfs_ar_readheader(fd, &ar) != 0)
            break;

        snprintf(cnp, DIRSIZ + 1, "%s", ar.ar_name);
        cnp[DIRSIZ] = '\0';
        int missing = unixfs_internal_namei(parent_ino, cnp, &stbuf);
        if (!missing) /* duplicate */
            goto next;

        struct inode* ip = unixfs_inodelayer_iget((ino_t)(fs->s_lastino + 1));
        if (!ip) {
            fprintf(stderr, "*** fatal error: no inode for %llu\n",
                   (ino_t)(fs->s_lastino + 1));
            abort();
        }
        ip->I_mode  = ancientfs_ar_mode(ar.ar_mode);
        ip->I_uid   = ar.ar_uid;
        ip->I_gid   = ar.ar_gid;
        ip->I_nlink = 1;
        ip->I_size  = ar.ar_size;
        ip->I_atime_sec = ip->I_mtime_sec = ip->I_ctime_sec =
            ar.ar_date;

        struct ar_node_info* ai = (struct ar_node_info*)ip->I_private;

        ip->I_daddr[0] = (uint32_t)lseek(fd, (off_t)0, SEEK_CUR);

        memcpy(ai->ar_name, cnp, strlen(cnp));

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
        (void)lseek(fd, (off_t)(ar.ar_size + (ar.ar_size & 1)), SEEK_CUR);
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

    snprintf(unixfs->s_fsname, UNIXFS_MNAMELEN, "UNIX Old ar");

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
    if (namelen > DIRSIZ)
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
    size_t dirnamelen = min(DIRSIZ, UNIXFS_MAXNAMLEN);
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
