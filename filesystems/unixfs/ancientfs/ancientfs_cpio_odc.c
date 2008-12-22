/*
 * Ancient UNIX File Systems for MacFUSE
 * Amit Singh
 * http://osxbook.com
 */

#include "ancientfs_cpio_odc.h"
#include "unixfs_common.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

DECL_UNIXFS("UNIX cpio_odc", cpio_odc);

#define OCTAL    8

#define CPIO_ODC_ATOI(from, to, len, base) { \
    memmove(buf, from, len); \
    buf[len] = '\0'; \
    to = strtol(buf, (char **)NULL, base); \
}

struct cpio_odc_entry {
    char   name[UNIXFS_MAXPATHLEN + 1];
    char   linktargetname[UNIXFS_MAXPATHLEN + 1];
    off_t  daddr;
    struct stat stat;
};

static int ancientfs_cpio_odc_readheader(int fd, struct cpio_odc_entry* ce);

static int
ancientfs_cpio_odc_readheader(int fd, struct cpio_odc_entry* ce)
{
    int nr;
    char buf[20];
    struct cpio_odc_header _hdr, *hdr = &_hdr;

    nr = read(fd, hdr, sizeof(struct cpio_odc_header));
    if (nr != sizeof(struct cpio_odc_header)) {
        if (!nr)
            return 1;
        if (nr < 0)
            return -1;
    }

    if (strncmp(hdr->c_magic, CPIO_ODC_MAGIC, CPIO_ODC_MAGLEN) != 0) {
        fprintf(stderr, "*** fatal error: bad magic in record @ %llu - %lu\n",
                lseek(fd, (off_t)0, SEEK_CUR),
                (unsigned long)sizeof(struct cpio_odc_header));
        return -1;
    }

    memset(ce, 0, sizeof(*ce));

    /* nothing to do with h_dev */

    CPIO_ODC_ATOI(hdr->c_ino, ce->stat.st_ino, sizeof(hdr->c_ino), OCTAL);
    CPIO_ODC_ATOI(hdr->c_mode, ce->stat.st_mode, sizeof(hdr->c_mode), OCTAL);
    ce->stat.st_mode = ancientfs_cpio_odc_mode(ce->stat.st_mode,
                                               unixfs->s_flags);
    CPIO_ODC_ATOI(hdr->c_uid, ce->stat.st_uid, sizeof(hdr->c_uid), OCTAL);
    CPIO_ODC_ATOI(hdr->c_gid, ce->stat.st_gid, sizeof(hdr->c_gid), OCTAL);
    CPIO_ODC_ATOI(hdr->c_nlink, ce->stat.st_nlink, sizeof(hdr->c_nlink), OCTAL);

    unsigned long rdev;
    CPIO_ODC_ATOI(hdr->c_rdev, rdev, sizeof(hdr->c_rdev), OCTAL);
    ce->stat.st_rdev = makedev(((uint16_t)rdev >> 8) & 255,
                                (uint16_t)rdev & 255);

    long mtime;
    CPIO_ODC_ATOI(hdr->c_mtime, mtime, sizeof(hdr->c_mtime), OCTAL);
    ce->stat.st_atime = ce->stat.st_ctime = ce->stat.st_mtime = mtime;

    unsigned long filesize;
    CPIO_ODC_ATOI(hdr->c_filesize, filesize, sizeof(hdr->c_filesize), OCTAL);
    ce->stat.st_size = filesize;

    unsigned long namesize;
    CPIO_ODC_ATOI(hdr->c_namesize, namesize, sizeof(hdr->c_namesize), OCTAL);
    if (namesize < 2) {
        fprintf(stderr, "*** fatal error: file name too small\n");
        return -1;
    }

    if (namesize > UNIXFS_MAXPATHLEN) {
        fprintf(stderr, "*** fatal error: file name too large (%#lx) @ %llu\n",
                namesize, lseek(fd, (off_t)0, SEEK_CUR));
        return -1;
    }

    if (read(fd, &ce->name, namesize) != namesize)
        return -1;

    if (ce->name[0] == '\0' || ce->name[namesize - 1] != '\0') { /* corrupt */
        fprintf(stderr, "*** fatal error: file name corrupt @ %llu\n",
                lseek(fd, (off_t)0, SEEK_CUR));
        return -1;
    }

    ce->daddr = lseek(fd, (off_t)0, SEEK_CUR);
    if (ce->daddr < 0) {
        fprintf(stderr, "*** fatal error: cannot read archive\n");
        return -1;
    }

    /* ce->daddr now contains the start of data */

    if (!ce->stat.st_size && (namesize == CPIO_ODC_TRAILER_LEN) &&
        !memcmp(ce->name, CPIO_ODC_TRAILER, CPIO_ODC_TRAILER_LEN))
        return 1;

    if (!S_ISLNK(ce->stat.st_mode) || !ce->stat.st_size) {
        off_t dataend = ce->stat.st_size;
        (void)lseek(fd, dataend, SEEK_CUR); 
        return 0;
    }

    /* link */

    if (ce->stat.st_size < 2) {
        fprintf(stderr, "*** fatal error: link target name too small\n");
        return -1;
    }

    if (ce->stat.st_size > UNIXFS_MAXPATHLEN) {
        fprintf(stderr, "*** fatal error: link target name too large\n");
        return -1;
    }

    if (read(fd, ce->linktargetname, ce->stat.st_size) != ce->stat.st_size)
        return -1;

    if (ce->linktargetname[0] == '\0') {
        fprintf(stderr, "*** fatal error: link target name corrupt\n");
        return -1;
    }

    ce->linktargetname[ce->stat.st_size] = '\0';

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
    fs_endian_t mye, e = UNIXFS_FS_INVALID;
    struct stat stbuf;
    struct super_block* sb = (struct super_block*)0;
    struct filsys* fs = (struct filsys*)0;

    if ((err = fstat(fd, &stbuf)) != 0) {
        perror("fstat");
        goto out;
    }

    if (!S_ISREG(stbuf.st_mode) && !(flags & UNIXFS_FORCE)) {
        err = EINVAL;
        fprintf(stderr, "%s is not a cpio image file\n", dmg);
        goto out;
    }

    struct cpio_odc_header hdr;

    if (read(fd, &hdr, sizeof(hdr)) != sizeof(hdr)) {
        fprintf(stderr, "failed to read data from file\n");
        err = EIO;
        goto out;
    }

    if (strncmp(hdr.c_magic, CPIO_ODC_MAGIC, CPIO_ODC_MAGLEN) != 0) {
        fprintf(stderr, "not recognized as a cpio_odc archive\n");
        err = EINVAL;
        goto out;
    }

    sb = malloc(sizeof(struct super_block));
    if (!sb) {
        err = ENOMEM;
        goto out;
    }

    assert(sizeof(struct filsys) <= CPIO_ODC_BLOCK);

    fs = calloc(1, CPIO_ODC_BLOCK);
    if (!fs) {
        free(sb);
        err = ENOMEM;
        goto out;
    }

    unixfs = sb;

    unixfs->s_flags = flags;
    unixfs->s_endian = UNIXFS_FS_LITTLE; /* not used */
    if (e != mye)
        fs->s_needsswap = 1;
    unixfs->s_fs_info = (void*)fs;
    unixfs->s_bdev = fd;

    /* must initialize the inode layer before sanity checking */
    if ((err = unixfs_inodelayer_init(sizeof(struct cpio_odc_node_info))) != 0)
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

    struct cpio_odc_node_info* rootci =
        (struct cpio_odc_node_info*)rootip->I_private;
    rootci->ci_self = rootip;
    rootci->ci_parent = NULL;
    rootci->ci_children = NULL;
    rootci->ci_next_sibling = NULL;

    unixfs_inodelayer_isucceeded(rootip);

    fs->s_fsize = stbuf.st_size / CPIO_ODC_BLOCK;
    fs->s_files = 0;
    fs->s_directories = 1 + 1 + 1;
    fs->s_rootip = rootip;
    fs->s_lastino = ROOTINO;

    lseek(fd, (off_t)0, SEEK_SET); /* rewind archive */

    struct cpio_odc_entry _ce, *ce = &_ce;

    for (;;) {
        if ((err = ancientfs_cpio_odc_readheader(fd, ce)) != 0) {
            if (err == 1)
                break;
            else {
                fprintf(stderr,
                        "*** fatal error: cannot read block (error %d)\n", err);
                err = EIO;
                goto out;
            }
        }

        char* path = ce->name;
        ino_t parent_ino = ROOTINO;
        size_t pathlen = strlen(ce->name);

        if ((*path == '.') && ((pathlen == 1) ||
            ((pathlen == 2) && (*(path + 1) == '/')))) {
            /* root */
            rootip->I_mode = ce->stat.st_mode;
            rootip->I_atime_sec = \
                rootip->I_mtime_sec = \
                    rootip->I_ctime_sec = ce->stat.st_mtime;
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
                if (!term) { /* out of order */
                    struct inode* dirp = unixfs_inodelayer_iget(parent_ino);
                    if (!dirp || !dirp->I_initialized) {
                        fprintf(stderr,
                                "*** fatal error: inode %llu inconsistent\n",
                                (ino64_t)parent_ino);
                        abort();
                    }
                    dirp->I_mode = ce->stat.st_mode;
                    dirp->I_uid = ce->stat.st_uid;
                    dirp->I_gid = ce->stat.st_gid;
                    unixfs_inodelayer_iput(dirp);
                }
                continue;
            }
            struct inode* ip =
                unixfs_inodelayer_iget((ino_t)(fs->s_lastino + 1));
                /* unixfs_inodelayer_iget(ce->stat.st_ino); */
            if (!ip) {
                fprintf(stderr, "*** fatal error: no inode for %llu\n",
                        (ino64_t)(fs->s_lastino + 1));
                abort();
            }

            ip->I_mode  = ce->stat.st_mode;
            ip->I_uid   = ce->stat.st_uid;
            ip->I_gid   = ce->stat.st_gid;
            ip->I_size  = ce->stat.st_size;
            ip->I_nlink = ce->stat.st_nlink;
            ip->I_rdev  = ce->stat.st_rdev;

            ip->I_atime_sec = ip->I_mtime_sec = ip->I_ctime_sec =
                ce->stat.st_mtime;

            struct cpio_odc_node_info* ci =
                (struct cpio_odc_node_info*)ip->I_private;

            size_t namelen = strlen(cnp);
            ci->ci_name = malloc(namelen + 1);
            if (!ci->ci_name) {
                fprintf(stderr, "*** fatal error: cannot allocate memory\n");
                abort();
            }
            memcpy(ci->ci_name, cnp, namelen);
            ci->ci_name[namelen] = '\0';

            ip->I_daddr[0] = 0;

            if (S_ISLNK(ip->I_mode)) {
                namelen = strlen(ce->linktargetname);
                ci->ci_linktargetname = malloc(namelen + 1);
                if (!ci->ci_name) {
                    fprintf(stderr,
                            "*** fatal error: cannot allocate memory\n");
                    abort();
                }
                memcpy(ci->ci_linktargetname, ce->linktargetname, namelen);
                ci->ci_linktargetname[namelen] = '\0';
            } else if (S_ISREG(ip->I_mode)) {

                ip->I_daddr[0] = ce->daddr;
            }
             
            ci->ci_self = ip;
            ci->ci_children = NULL;
            struct inode* parent_ip = unixfs_internal_iget(parent_ino);
            parent_ip->I_size += 1;
            ci->ci_parent = (struct cpio_odc_node_info*)(parent_ip->I_private);
            ci->ci_next_sibling = ci->ci_parent->ci_children;
            ci->ci_parent->ci_children = ci;

            if (term && !S_ISDIR(ip->I_mode)) /* out of order */
                ip->I_mode = S_IFDIR | 0755;

            if (S_ISDIR(ip->I_mode)) {
                fs->s_directories++;
                parent_ino = fs->s_lastino + 1;
                /* parent_ino = ip->I_ino; */
                ip->I_size = 2;
            } else
                fs->s_files++;

            fs->s_lastino++;

            /* if (ip->I_ino > fs->s_lastino)
                fs->s_lastino = ip->I_ino; */

            unixfs_internal_iput(parent_ip);
            unixfs_inodelayer_isucceeded(ip);
            /* no put */

        } /* for each component */

    } /* for each block */

    err = 0;

    unixfs->s_statvfs.f_bsize = CPIO_ODC_BLOCK;
    unixfs->s_statvfs.f_frsize = CPIO_ODC_BLOCK;
    unixfs->s_statvfs.f_ffree = 0;
    unixfs->s_statvfs.f_files = fs->s_files + fs->s_directories;
    unixfs->s_statvfs.f_blocks = fs->s_fsize;
    unixfs->s_statvfs.f_bfree = 0;
    unixfs->s_statvfs.f_bavail = 0;
    unixfs->s_dentsize = 1;
    unixfs->s_statvfs.f_namemax = UNIXFS_MAXNAMLEN;

    snprintf(unixfs->s_fsname, UNIXFS_MNAMELEN, "ASCII cpio (odc)");

    char* dmg_basename = basename((char*)dmg);
    snprintf(unixfs->s_volname, UNIXFS_MAXNAMLEN, "%s (archive=%s)",
             unixfs_fstype, (dmg_basename) ? dmg_basename : "cpio_odc Image");

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
            struct cpio_odc_node_info* ci =
                (struct cpio_odc_node_info*)tmp->I_private;
            if (ci) {
                free(ci->ci_name);
                if (ci->ci_linktargetname)
                    free(ci->ci_linktargetname);
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

    struct cpio_odc_node_info* child =
        ((struct cpio_odc_node_info*)dp->I_private)->ci_children;;

    if (!child) {
        ret = ENOENT;
        goto out;
    }

    int found = 0;

    do {
        size_t target_namelen = strlen((const char*)child->ci_name);
        if ((namelen == target_namelen) &&
            (memcmp(name, child->ci_name, target_namelen) == 0)) {
            found = 1;
            break;
        }
        child = child->ci_next_sibling;
    } while (child);

    if (found)
        ret = unixfs_internal_igetattr((ino_t)child->ci_self->I_ino, stbuf);

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

    struct cpio_odc_node_info* child =
        ((struct cpio_odc_node_info*)dp->I_private)->ci_children;;

    off_t i;

    for (i = 0; i < (*offset - 2); i++)
        child = child->ci_next_sibling;

    dent->ino = (ino_t)child->ci_self->I_ino;
    size_t dirnamelen = strlen(child->ci_name);
    dirnamelen = min(dirnamelen, UNIXFS_MAXNAMLEN);
    memcpy(dent->name, child->ci_name, dirnamelen);
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

    struct cpio_odc_node_info* ci = (struct cpio_odc_node_info*)ip->I_private;
    if (!ci->ci_linktargetname) {
        error = ENOENT;
        goto out;
    } 

    size_t linklen = min(ip->I_size, CPIO_ODC_BLOCK);
    memcpy(path, ci->ci_linktargetname, linklen);
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
