/*
 * UnixFS
 *
 * A general-purpose file system layer for writing/reimplementing/porting
 * Unix file systems through MacFUSE.

 * Copyright (c) 2008 Amit Singh. All Rights Reserved.
 * http://osxbook.com
 */

#include "unixfs.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <dlfcn.h>

#include <fuse/fuse_opt.h>
#include <fuse/fuse_lowlevel.h>

#define UNIXFS_META_TIMEOUT 60.0 /* timeout for nodes and their attributes */

static struct unixfs* unixfs = (struct unixfs*)0;

static void
unixfs_ll_statfs(fuse_req_t req, fuse_ino_t ino)
{
    struct statvfs sv;
    unixfs->ops->statvfs(&sv);
    fuse_reply_statfs(req, &sv);
}

/* no unixfs_ll_init() since we do initialization before mounting */

static void
unixfs_ll_destroy(void* data)
{
    unixfs->ops->fini(unixfs->filsys);
}

static void
unixfs_ll_lookup(fuse_req_t req, fuse_ino_t parent, const char* name)
{
    struct fuse_entry_param e;
    memset(&e, 0, sizeof(e));

    int error = unixfs->ops->namei(parent, name, &(e.attr));
    if (error) {
        fuse_reply_err(req, error);
        return;
    }

    e.ino = e.attr.st_ino;
    e.attr_timeout = e.entry_timeout = UNIXFS_META_TIMEOUT;

    fuse_reply_entry(req, &e);
}

static
void unixfs_ll_getattr(fuse_req_t req, fuse_ino_t ino,
                       struct fuse_file_info* fi)
{
    struct stat stbuf;
    int error = unixfs->ops->igetattr(ino, &stbuf);
    if (!error)
        fuse_reply_attr(req, &stbuf, UNIXFS_META_TIMEOUT);
    else
        fuse_reply_err(req, error);
}

static void
unixfs_ll_readlink(fuse_req_t req, fuse_ino_t ino)
{
    int ret = ENOSYS;

    char path[UNIXFS_MAXPATHLEN];

    if ((ret = unixfs->ops->readlink(ino, path)) != 0)
        fuse_reply_err(req, ret);

    fuse_reply_readlink(req, path);
}

static void
unixfs_ll_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                  struct fuse_file_info* fi)
{
    (void)fi;

    struct inode* dp = unixfs->ops->iget(ino);
    if (!dp) {
        fuse_reply_err(req, ENOENT);
        return;
    }

    struct stat stbuf;
    unixfs->ops->istat(dp, &stbuf);

    if (!S_ISDIR(stbuf.st_mode)) {
        unixfs->ops->iput(dp);
        fuse_reply_err(req, ENOTDIR);
        return;
    }

    off_t offset = 0;
    struct unixfs_direntry dent;

    struct replybuf {
        char*  p;
        size_t size;
    } b;

    memset(&b, 0, sizeof(b));

    struct unixfs_dirbuf dirbuf;

    while (unixfs->ops->nextdirentry(dp, &dirbuf, &offset, &dent) == 0) {

        if (dent.ino == 0)
            continue;

        if (unixfs->ops->igetattr(dent.ino, &stbuf) != 0)
            continue;

        size_t oldsize = b.size;
        b.size += fuse_add_direntry(req, NULL, 0, dent.name, NULL, 0);
        char* newp = (char *)realloc(b.p, b.size);
        if (!newp) {
            fprintf(stderr, "*** fatal error: cannot allocate memory\n");
            abort();
        }
        b.p = newp;
        fuse_add_direntry(req, b.p + oldsize, b.size - oldsize, dent.name,
                          &stbuf, b.size);
    }

    unixfs->ops->iput(dp);

    if (off < b.size)
        fuse_reply_buf(req, b.p + off, min(b.size - off, size));
    else
        fuse_reply_buf(req, NULL, 0);

    free(b.p);
}

static void
unixfs_ll_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi)
{
    struct inode* ip = unixfs->ops->iget(ino);
    if (!ip)
        fuse_reply_err(req, ENOENT);

    struct stat stbuf;
    unixfs->ops->istat(ip, &stbuf);

    if (!S_ISREG(stbuf.st_mode)) {
        if (S_ISDIR(stbuf.st_mode))
            fuse_reply_err(req, EISDIR);
        else if (S_ISBLK(stbuf.st_mode) || S_ISCHR(stbuf.st_mode))
            fuse_reply_err(req, ENXIO);
        else
            fuse_reply_err(req, EACCES);
        unixfs->ops->iput(ip);
    } else {
        fi->fh = (uint64_t)(long)ip;
        fuse_reply_open(req, fi);
    }
}

static void
unixfs_ll_release(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi)
{
    if (fi->fh)
        unixfs->ops->iput((struct inode *)(long)(fi->fh));

    fi->fh = 0;

    fuse_reply_err(req, 0);
}

static void
unixfs_ll_read(fuse_req_t req, fuse_ino_t ino, size_t count, off_t offset,
               struct fuse_file_info* fi)
{
    struct inode* ip = (struct inode*)(long)(fi->fh);
    if (!ip) {
        fuse_reply_err(req, EBADF);
        return;
    }

    struct stat stbuf;
    unixfs->ops->istat(ip, &stbuf);
    off_t size = stbuf.st_size;

    if ((count == 0) || (offset > size)) {
        fuse_reply_buf(req, NULL, 0);
        return;
    }

    if ((offset + count) > size)
        count = size - offset;

    char *buf = calloc(count, 1);
    if (!buf) {
        fuse_reply_err(req, ENOMEM);
        return;
    }

    int error = 0;
    char* bp = buf;
    size_t nbytes = 0;

    do {
        ssize_t ret = unixfs->ops->pbread(ip, bp, count, offset, &error);
        if (ret < 0)
            goto out; 
        count -= ret;
        offset += ret;
        nbytes += ret;
        bp += ret;
    } while (!error && count);

out:
    fuse_reply_buf(req, buf, nbytes);

    free(buf);
}

static struct fuse_lowlevel_ops unixfs_ll_oper = {
    .statfs     = unixfs_ll_statfs,
    .destroy    = unixfs_ll_destroy,
    .lookup     = unixfs_ll_lookup,
    .getattr    = unixfs_ll_getattr,
    .readlink   = unixfs_ll_readlink,
    .readdir    = unixfs_ll_readdir,
    .open       = unixfs_ll_open,
    .release    = unixfs_ll_release,
    .read       = unixfs_ll_read,
};

struct options {
    char* dmg;
    int   force;
    char* fsendian;
    char* type;
} options;

#define UNIXFS_OPT_KEY(t, p, v) { t, offsetof(struct options, p), v }

static struct fuse_opt unixfs_opts[] = {

    UNIXFS_OPT_KEY("--dmg %s", dmg, 0),
    UNIXFS_OPT_KEY("--force", force, 1),
    UNIXFS_OPT_KEY("--fsendian %s", fsendian, 0),
    UNIXFS_OPT_KEY("--type %s", type, 0),

    FUSE_OPT_END
};

int
main(int argc, char* argv[])
{
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    memset(&options, 0, sizeof(struct options));

    if ((fuse_opt_parse(&args, &options, unixfs_opts, NULL) == -1) ||
        !options.dmg) {
        unixfs_usage();
        return -1;
    }

    char* mountpoint;
    int   multithreaded;
    int   foregrounded;

    if (fuse_parse_cmdline(&args, &mountpoint,
                           &multithreaded, &foregrounded) == -1) {
       unixfs_usage();
       return -1;
    }

    if (!(unixfs = unixfs_preflight(options.dmg, &(options.type), &unixfs))) {
        if (options.type)
            fprintf(stderr, "invalid file system type %s\n", options.type);
        else
            fprintf(stderr, "missing file system type\n");
        return -1;
    }

    if (options.force)
        unixfs->flags |= UNIXFS_FORCE;

    unixfs->fsname = options.type; /* XXX quick fix */

    unixfs->fsendian = UNIXFS_FS_INVALID;

    if (options.fsendian) {
        if (strcasecmp(options.fsendian, "pdp") == 0) {
            unixfs->fsendian = UNIXFS_FS_PDP;
        } else if (strcasecmp(options.fsendian, "big") == 0) {
            unixfs->fsendian = UNIXFS_FS_BIG;
        } else if (strcasecmp(options.fsendian, "little") == 0) {
            unixfs->fsendian = UNIXFS_FS_LITTLE;
        } else {
            fprintf(stderr, "invalid endian type %s\n", options.fsendian);
            return -1;
        }
    }

    if ((unixfs->filsys =
        unixfs->ops->init(options.dmg, unixfs->flags, unixfs->fsendian,
                          &unixfs->fsname, &unixfs->volname)) == NULL) {
        fprintf(stderr, "failed to initialize file system\n");
        return -1;
    }

    char extra_args[UNIXFS_ARGLEN] = { 0 };
    unixfs_postflight(unixfs->fsname, unixfs->volname, extra_args);

    fuse_opt_add_arg(&args, extra_args);

    int err = -1;
    struct fuse_chan *ch;

    if ((ch = fuse_mount(mountpoint, &args)) != NULL) {

        struct fuse_session* se;

        se = fuse_lowlevel_new(&args, &unixfs_ll_oper, sizeof(unixfs_ll_oper),
                               (void*)&unixfs);
        if (se != NULL) {
            if ((err = fuse_daemonize(foregrounded)) == -1)
                goto bailout;
            if (fuse_set_signal_handlers(se) != -1) {
                fuse_session_add_chan(se, ch);
                if (multithreaded)
                    err = fuse_session_loop_mt(se);
                else
                    err = fuse_session_loop(se);
                fuse_remove_signal_handlers(se);
                fuse_session_remove_chan(ch);
            }
bailout:
            fuse_session_destroy(se);
        }
        fuse_unmount(mountpoint, ch);
    }

    fuse_opt_free_args(&args);

    return err ? 1 : 0;
}
