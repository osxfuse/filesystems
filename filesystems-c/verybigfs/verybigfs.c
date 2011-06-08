/*
 * A "very big" file system with a "very big" file. All that you can read.
 *
 * Copyright Amit Singh. All Rights Reserved.
 * http://osxbook.com
 *
 * http://code.google.com/p/macfuse/
 *
 * Source License: GNU GENERAL PUBLIC LICENSE (GPL)
 */

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/statvfs.h>
#include <fuse.h>

char *bigfile_path = "/copyme.txt";

static int
verybigfs_statfs(const char *path, struct statvfs *stbuf)
{
    stbuf->f_bsize  = 1024 * 1024;  /* 1MB */
    stbuf->f_frsize = 128  * 1024;  /* MAXPHYS */
    stbuf->f_blocks = 0xFFFFFFFFUL; /* aim for a lot; this is 32-bit though  */
    stbuf->f_bfree  = stbuf->f_bavail = stbuf->f_ffree = stbuf->f_favail = 0;
    stbuf->f_files  = 3;
    return 0;
}

static int
verybigfs_getattr(const char *path, struct stat *stbuf)
{
    memset(stbuf, 0, sizeof(struct stat));
    stbuf->st_atime = stbuf->st_ctime = stbuf->st_mtime = time(NULL);
    stbuf->st_uid = getuid();
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    } else if (strcmp(path, bigfile_path) == 0) {
        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size = 128ULL * 1024ULL * 0xFFFFFFFFULL;
    } else
        return -ENOENT;
    return 0;
}

static int
verybigfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                  off_t offset, struct fuse_file_info *fi)
{
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    filler(buf, bigfile_path + 1, NULL, 0);
    return 0;
}

static int
verybigfs_read(const char *path, char *buf, size_t size, off_t offset,
               struct fuse_file_info *fi)
{
    return 0;
}

static struct fuse_operations verybigfs_oper = {
    .getattr = verybigfs_getattr,
    .read    = verybigfs_read,
    .readdir = verybigfs_readdir,
    .statfs  = verybigfs_statfs,
};

int
main(int argc, char *argv[])
{
    return fuse_main(argc, argv, &verybigfs_oper, NULL);
}
