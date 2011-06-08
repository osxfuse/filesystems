/*
 * Ancient UNIX File Systems for MacFUSE
 * Amit Singh
 * http://osxbook.com
 */

#ifndef _ANCIENTFS_CPIO_ODC_H_
#define _ANCIENTFS_CPIO_ODC_H_

#include "unixfs_internal.h"
#include "ancientfs.h"

#define ROOTINO  1

struct filsys
{
    uint32_t s_fsize;
    uint32_t s_files;
    uint32_t s_directories;
    uint32_t s_lastino;
    uint32_t s_dataoffset;
    uint32_t s_needsswap;
    struct inode* s_rootip;
};

#define CPIO_ODC_BLOCK       512
#define CPIO_ODC_MAGIC       "070707"
#define CPIO_ODC_MAGLEN      6
#define CPIO_ODC_TRAILER     "TRAILER!!!"
#define CPIO_ODC_TRAILER_LEN (sizeof(CPIO_ODC_TRAILER)/sizeof(unsigned char))

struct cpio_odc_header {
    char c_magic[6];
    char c_dev[6];
    char c_ino[6];
    char c_mode[6];
    char c_uid[6];
    char c_gid[6];
    char c_nlink[6];
    char c_rdev[6];
    char c_mtime[11];
    char c_namesize[6];
    char c_filesize[11];
/*  char c_name[c_namesize]; */
/*  char c_data[c_filesize]; */
} __attribute__((packed));

struct cpio_odc_node_info {
    struct inode*              ci_self;
    struct cpio_odc_node_info* ci_parent;
    struct cpio_odc_node_info* ci_children;
    struct cpio_odc_node_info* ci_next_sibling;
    char*                      ci_name;
    char*                      ci_linktargetname;
};

/* modes */
#define IALLOC  0100000 /* i-node is allocated */
#define ILARG   010000  /* large file */
#define IFMT    0170000 /* type of file */

static inline mode_t
ancientfs_cpio_odc_mode(mode_t mode, uint32_t flags)
{
    return mode & (uint16_t)~(IALLOC & ILARG);
}

#endif /* _ANCIENTFS_CPIO_ODC_H_ */
