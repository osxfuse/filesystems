/*
 * Ancient UNIX File Systems for MacFUSE
 * Amit Singh
 * http://osxbook.com
 */

#ifndef _ANCIENTFS_CPIO_NEWC_H_
#define _ANCIENTFS_CPIO_NEWC_H_

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

#define CPIO_NEWC_BLOCK       512
#define CPIO_NEWC_MAGIC       "070701"
#define CPIO_NEWCRC_MAGIC     "070702"
#define CPIO_NEWC_MAGLEN      6
#define CPIO_NEWC_TRAILER     "TRAILER!!!"
#define CPIO_NEWC_TRAILER_LEN (sizeof(CPIO_NEWC_TRAILER)/sizeof(unsigned char))

struct cpio_newc_header {
    char c_magic[6];
    char c_ino[8];
    char c_mode[8];
    char c_uid[8];
    char c_gid[8];
    char c_nlink[8];
    char c_mtime[8];
    char c_filesize[8];
    char c_devmajor[8];
    char c_devminor[8];
    char c_rdevmajor[8];
    char c_rdevminor[8];
    char c_namesize[8];
    char c_check[8];
/*  char c_name[c_namesize rounded to 4 bytes]; */
/*  char c_data[c_filesize rounded to 4 bytes]; */
} __attribute__((packed));

struct cpio_newc_node_info {
    struct inode*               ci_self;
    struct cpio_newc_node_info* ci_parent;
    struct cpio_newc_node_info* ci_children;
    struct cpio_newc_node_info* ci_next_sibling;
    char*                       ci_name;
    char*                       ci_linktargetname;
};

/* modes */
#define IALLOC  0100000 /* i-node is allocated */
#define ILARG   010000  /* large file */
#define IFMT    0170000 /* type of file */

static inline mode_t
ancientfs_cpio_newc_mode(mode_t mode, uint32_t flags)
{
    return mode & (uint16_t)~(IALLOC & ILARG);
}

#endif /* _ANCIENTFS_CPIO_NEWC_H_ */
