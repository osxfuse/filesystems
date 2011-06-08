/*
 * Ancient UNIX File Systems for MacFUSE
 * Amit Singh
 * http://osxbook.com
 */

#include "ancientfs_tar.h"
#include "unixfs_common.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

DECL_UNIXFS("UNIX V7 tar/ustar", tar);

#define DECIMAL 10
#define OCTAL    8

#define TAR_ATOI(from, to, len, base) { \
        memmove(buf, from, len); \
        buf[len] = '\0'; \
        to = strtol(buf, (char **)NULL, base); \
}

struct tar_entry {
    char name[UNIXFS_MAXPATHLEN + 1];
    char linktargetname[UNIXFS_MAXPATHLEN + 1];
    struct stat stat;
};

static int ancientfs_tar_readheader(int fd, struct tar_entry* te);
static int ancientfs_tar_chksum(union hblock* hb);

int
ancientfs_tar_chksum(union hblock* hb)
{
    char* cp;
    for (cp = hb->dbuf.chksum; cp < &hb->dbuf.chksum[sizeof(hb->dbuf.chksum)];
         cp++)
        *cp = ' ';

    int i = 0;
    for (cp = hb->dummy; cp < &hb->dummy[TBLOCK]; cp++)
        i += *cp;

    return i;
}

static int
ancientfs_tar_readheader(int fd, struct tar_entry* te)
{
    static int cksum_failed = 0;
    int  nr, ustar;
    char buf[20];
    char hb[sizeof(union hblock) + 1];
    struct header* hdr;

retry:

    ustar = unixfs->s_flags & ANCIENTFS_USTAR;
    nr = read(fd, hb, sizeof(union hblock));
    if (nr != sizeof(union hblock)) {
        if (!nr)
            return 1;
        if (nr < 0)
            return -1;
    }

    hdr = &((union hblock*)hb)->dbuf;

    long chksum = 0;
    TAR_ATOI(hdr->chksum, chksum, sizeof(hdr->chksum), OCTAL);
    if (chksum != ancientfs_tar_chksum((union hblock*)hb)) {
        cksum_failed++;
        if (!(cksum_failed % 10))
            fprintf(stderr,
                    "*** warning: checksum failed (%d failures so far)\n",
                    cksum_failed);
        goto retry;
    }

    memset(te, 0, sizeof(*te));

    TAR_ATOI(hdr->mode, te->stat.st_mode, sizeof(hdr->mode), OCTAL);
    TAR_ATOI(hdr->uid, te->stat.st_uid, sizeof(hdr->uid), OCTAL);

    te->stat.st_mode = ancientfs_tar_mode(te->stat.st_mode, unixfs->s_flags);

    char tbuf[16];

    memset(tbuf, 0, 16);
    memcpy(tbuf, hdr->size, 12);
    TAR_ATOI(tbuf, te->stat.st_size, 16, OCTAL);

    memset(tbuf, 0, 16);
    memcpy(tbuf, hdr->mtime, 12);
    TAR_ATOI(tbuf, te->stat.st_mtime, 16, OCTAL);

    te->stat.st_atime = te->stat.st_ctime = te->stat.st_mtime;

    if ((hdr->typeflag == TARTYPE_SYM) || (hdr->typeflag == TARTYPE_LNK)) {
        /* translate hard link to symbolic link */
        te->stat.st_mode |= S_IFLNK;
        memcpy(te->linktargetname, hdr->linkname, 100);
        te->stat.st_size = strlen(te->linktargetname);
    } else {

        switch (hdr->typeflag) {

        case 0:
        case TARTYPE_REG:
            te->stat.st_mode |= S_IFREG;
            break;

        case TARTYPE_SYM:
        case TARTYPE_LNK:
            te->stat.st_mode |= S_IFLNK;
            break;

        case TARTYPE_CHR:
            te->stat.st_mode |= S_IFCHR;
            break;

        case TARTYPE_BLK:
            te->stat.st_mode |= S_IFBLK;
            break;

        case TARTYPE_DIR:
            te->stat.st_mode |= S_IFDIR;
            break;

        case TARTYPE_FIFO:
            te->stat.st_mode |= S_IFIFO;
            break;

        }
    }

    if (!ustar) {
        if (hdr->name[99] == '/') /* full field used */
            te->stat.st_mode = S_IFDIR | (uint16_t)(te->stat.st_mode & 07777);
        else {
            size_t len = strlen(hdr->name);
            if (hdr->name[len - 1] == '/')
                te->stat.st_mode =
                    S_IFDIR | (uint16_t)(te->stat.st_mode & 07777);
        }
        memcpy(te->name, hdr->name, 100);
        te->name[100] = '\0';
    } else { /* ustar */
        if (hdr->prefix[0]) {
            memcpy(te->name, hdr->name, 100);
            te->name[100] = '\0';
        } else {
            if (hdr->prefix[154]) {
                memcpy(te->name, hdr->prefix, 155);
                memcpy(te->name + 155, hdr->name, 100);
                te->name[255] = '\0';
            } else {
                int n = snprintf(te->name, UNIXFS_MAXPATHLEN, "%s",
                                 hdr->prefix);
                memcpy(te->name + n, hdr->name, 100);
                te->name[n + 99] = '\0';
            }
        }
    }

    uint16_t dmajor;
    uint16_t dminor;
    TAR_ATOI(hdr->devmajor, dmajor, sizeof(hdr->devmajor), OCTAL);
    TAR_ATOI(hdr->devminor, dminor, sizeof(hdr->devminor), OCTAL);
    te->stat.st_rdev = makedev(dmajor, dminor);

    te->stat.st_nlink = 1;

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
        fprintf(stderr, "%s is not a tape image file\n", dmg);
        goto out;
    }

    char hb[sizeof(union hblock) + 1];

    if (read(fd, hb, sizeof(union hblock)) != sizeof(union hblock)) {
        fprintf(stderr, "failed to read data from file\n");
        err = EIO;
        goto out;
    }

    char* magic = (((union hblock*)hb)->dbuf).magic;
    if (memcmp(magic, TMAGIC, TMAGLEN - 1) == 0) {
        flags |= ANCIENTFS_USTAR;
        if (magic[6] == ' ')
            fprintf(stderr, "*** warning: pre-POSIX ustar archive\n");
    } else {
        flags |= ANCIENTFS_V7TAR;
        fprintf(stderr, "*** warning: not ustar; assuming ancient tar\n");
    }

    sb = malloc(sizeof(struct super_block));
    if (!sb) {
        err = ENOMEM;
        goto out;
    }

    assert(sizeof(struct filsys) <= TBLOCK);

    fs = calloc(1, TBLOCK);
    if (!fs) {
        free(sb);
        err = ENOMEM;
        goto out;
    }

    unixfs = sb;

    unixfs->s_flags = flags;

    /* not used */
    unixfs->s_endian = (fse == UNIXFS_FS_INVALID) ? UNIXFS_FS_LITTLE : fse;

    unixfs->s_fs_info = (void*)fs;
    unixfs->s_bdev = fd;

    /* must initialize the inode layer before sanity checking */
    if ((err = unixfs_inodelayer_init(sizeof(struct tar_node_info))) != 0)
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

    struct tar_node_info* rootti = (struct tar_node_info*)rootip->I_private;
    rootti->ti_self = rootip;
    rootti->ti_parent = NULL;
    rootti->ti_children = NULL;
    rootti->ti_next_sibling = NULL;

    unixfs_inodelayer_isucceeded(rootip);

    fs->s_fsize = stbuf.st_size / TBLOCK;
    fs->s_files = 0;
    fs->s_directories = 1 + 1 + 1;
    fs->s_rootip = rootip;
    fs->s_lastino = ROOTINO;

    lseek(fd, (off_t)0, SEEK_SET); /* rewind tape */

    struct tar_entry _te, *te = &_te;

    for (;;) {

        off_t toseek = 0;

        if ((err = ancientfs_tar_readheader(fd, te)) != 0) {
            if (err == 1)
                break;
            else {
                fprintf(stderr,
                        "*** fatal error: cannot read block (error %d)\n", err);
                err = EIO;
                goto out;
            }
        }

        char* path = te->name;
        ino_t parent_ino = ROOTINO;
        size_t pathlen = strlen(te->name);

        if ((*path == '.') && ((pathlen == 1) ||
            ((pathlen == 2) && (*(path + 1) == '/')))) {
            /* root */
            rootip->I_mode = te->stat.st_mode;
            rootip->I_atime_sec = \
                rootip->I_mtime_sec = \
                    rootip->I_ctime_sec = te->stat.st_mtime;
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
                        (ino64_t)(fs->s_lastino + 1));
                abort();
            }

            ip->I_mode  = te->stat.st_mode;
            ip->I_uid   = te->stat.st_uid;
            ip->I_gid   = te->stat.st_gid;
            ip->I_size  = te->stat.st_size;
            ip->I_nlink = te->stat.st_nlink;
            ip->I_rdev  = te->stat.st_rdev;

            ip->I_atime_sec = ip->I_mtime_sec = ip->I_ctime_sec =
                te->stat.st_mtime;

            struct tar_node_info* ti = (struct tar_node_info*)ip->I_private;

            size_t namelen = strlen(cnp);
            ti->ti_name = malloc(namelen + 1);
            if (!ti->ti_name) {
                fprintf(stderr, "*** fatal error: cannot allocate memory\n");
                abort();
            }
            memcpy(ti->ti_name, cnp, namelen);
            ti->ti_name[namelen] = '\0';

            ip->I_daddr[0] = 0;

            if (S_ISLNK(ip->I_mode)) {
                namelen = strlen(te->linktargetname);
                ti->ti_linktargetname = malloc(namelen + 1);
                if (!ti->ti_name) {
                    fprintf(stderr,
                            "*** fatal error: cannot allocate memory\n");
                    abort();
                }
                memcpy(ti->ti_linktargetname, te->linktargetname, namelen);
                ti->ti_linktargetname[namelen] = '\0';
            } else if (S_ISREG(ip->I_mode)) {

                ip->I_daddr[0] = (uint32_t)lseek(fd, (off_t)0, SEEK_CUR);
                toseek = ip->I_size;

            }
             
            ti->ti_self = ip;
            ti->ti_children = NULL;
            struct inode* parent_ip = unixfs_internal_iget(parent_ino);
            parent_ip->I_size += 1;
            ti->ti_parent = (struct tar_node_info*)(parent_ip->I_private);
            ti->ti_next_sibling = ti->ti_parent->ti_children;
            ti->ti_parent->ti_children = ti;

            if (S_ISDIR(ip->I_mode)) {
                fs->s_directories++;
                parent_ino = fs->s_lastino + 1;
                ip->I_size = 2;
            } else
                fs->s_files++;

            fs->s_lastino++;

            unixfs_internal_iput(parent_ip);
            unixfs_inodelayer_isucceeded(ip);
            /* no put */

        } /* for each component */

        if (toseek) {
            toseek = (toseek + TBLOCK - 1)/TBLOCK;
            toseek *= TBLOCK;
            (void)lseek(fd, (off_t)toseek, SEEK_CUR);
        }

    } /* for each block */

    err = 0;

    unixfs->s_statvfs.f_bsize = TBLOCK;
    unixfs->s_statvfs.f_frsize = TBLOCK;
    unixfs->s_statvfs.f_ffree = 0;
    unixfs->s_statvfs.f_files = fs->s_files + fs->s_directories;
    unixfs->s_statvfs.f_blocks = fs->s_fsize;
    unixfs->s_statvfs.f_bfree = 0;
    unixfs->s_statvfs.f_bavail = 0;
    unixfs->s_dentsize = 1;
    unixfs->s_statvfs.f_namemax = UNIXFS_MAXNAMLEN;

    snprintf(unixfs->s_fsname, UNIXFS_MNAMELEN, "UNIX %star",
             (unixfs->s_flags & ANCIENTFS_V7TAR) ? "V7 " : "us");

    char* dmg_basename = basename((char*)dmg);
    snprintf(unixfs->s_volname, UNIXFS_MAXNAMLEN, "%s (tape=%s)",
             unixfs_fstype, (dmg_basename) ? dmg_basename : "Tar Image");

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
            struct tar_node_info* ti = (struct tar_node_info*)tmp->I_private;
            if (ti) {
                free(ti->ti_name);
                if (ti->ti_linktargetname)
                    free(ti->ti_linktargetname);
            }
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

    struct tar_node_info* child =
        ((struct tar_node_info*)dp->I_private)->ti_children;;

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

    struct tar_node_info* child =
        ((struct tar_node_info*)dp->I_private)->ti_children;;

    off_t i;

    for (i = 0; i < (*offset - 2); i++)
        child = child->ti_next_sibling;

    dent->ino = (ino_t)child->ti_self->I_ino;
    size_t dirnamelen = strlen(child->ti_name);
    dirnamelen = min(dirnamelen, UNIXFS_MAXNAMLEN);
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
    off_t start = (off_t)ip->I_daddr[0];

    /* caller already checked for bounds */

    return pread(unixfs->s_bdev, buf, nbyte, start + offset);
}

static int
unixfs_internal_readlink(ino_t ino, char path[UNIXFS_MAXPATHLEN])
{
    struct inode* ip = unixfs_internal_iget(ino);
    if (!ip)
        return ENOENT;

    int error;

    struct tar_node_info* ti = (struct tar_node_info*)ip->I_private;
    if (!ti->ti_linktargetname) {
        error = ENOENT;
        goto out;
    } 

    size_t linklen = min(ip->I_size, TBLOCK);
    memcpy(path, ti->ti_linktargetname, linklen);
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
