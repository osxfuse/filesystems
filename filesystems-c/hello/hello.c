#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <fuse.h>

static const char  *file_path      = "/hello.txt";
static const char   file_content[] = "Hello World!\n";
static const size_t file_size      = sizeof(file_content)/sizeof(char) - 1;

static int
hello_getattr(const char *path, struct stat *stbuf)
{
    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0) { /* The root directory of our file system. */
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 3;
    } else if (strcmp(path, file_path) == 0) { /* The only file we have. */
        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size = file_size;
    } else /* We reject everything else. */
        return -ENOENT;

    return 0;
}

static int
hello_open(const char *path, struct fuse_file_info *fi)
{
    if (strcmp(path, file_path) != 0) /* We only recognize one file. */
        return -ENOENT;

    if ((fi->flags & O_ACCMODE) != O_RDONLY) /* Only reading allowed. */
        return -EACCES;

    return 0;
}

static int
hello_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
              off_t offset, struct fuse_file_info *fi)
{
    if (strcmp(path, "/") != 0) /* We only recognize the root directory. */
        return -ENOENT;

    filler(buf, ".", NULL, 0);           /* Current directory (.)  */
    filler(buf, "..", NULL, 0);          /* Parent directory (..)  */
    filler(buf, file_path + 1, NULL, 0); /* The only file we have. */

    return 0;
}

static int
hello_read(const char *path, char *buf, size_t size, off_t offset,
           struct fuse_file_info *fi)
{
    if (strcmp(path, file_path) != 0)
        return -ENOENT;

    if (offset >= file_size) /* Trying to read past the end of file. */
        return 0;

    if (offset + size > file_size) /* Trim the read to the file size. */
        size = file_size - offset;

    memcpy(buf, file_content + offset, size); /* Provide the content. */

    return size;
}

static struct fuse_operations hello_filesystem_operations = {
    .getattr = hello_getattr, /* To provide size, permissions, etc. */
    .open    = hello_open,    /* To enforce read-only access.       */
    .read    = hello_read,    /* To provide file content.           */
    .readdir = hello_readdir, /* To provide directory listing.      */
};

int
main(int argc, char **argv)
{
    return fuse_main(argc, argv, &hello_filesystem_operations, NULL);
}
