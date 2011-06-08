/*
 * Ancient UNIX File Systems for MacFUSE
 * Amit Singh
 * http://osxbook.com
 */

#ifndef _ANCIENTFS_TAR_H_
#define _ANCIENTFS_TAR_H_

#include "unixfs_internal.h"
#include "ancientfs.h"

#define TBLOCK   512
#define NAMSIZ   100

#define ROOTINO  1

struct filsys
{
    uint32_t s_fsize;
    uint32_t s_files;
    uint32_t s_directories;
    uint32_t s_lastino;
    uint32_t s_dataoffset;
    struct inode* s_rootip;
};

#define TMAGIC   "ustar" /* space terminated (pre POSIX) or null terminated */
#define TMAGLEN  6
#define TVERSION "00"    /* not null terminated */
#define TVERSLEN 2

union hblock {
    char dummy[TBLOCK];
    struct header {
        char name[NAMSIZ];     /* name of file */
        char mode[8];          /* file mode */
        char uid[8];           /* owner user ID */
        char gid[8];           /* owner group ID */
        char size[12];         /* length of file in bytes; no trailing null */
        char mtime[12];        /* modfiy time of file; no trailing null */
        char chksum[8];        /* checksum for header */
        char typeflag;         /* called linkflag in pre-ustar headers */
        char linkname[NAMSIZ]; /* name of linked file */
        /* ustar below */
        char magic[6];         /* USTAR indicator */
        char version[2];       /* USTAR version; no trailing null */
        char uname[32];        /* owner user name */
        char gname[32];        /* owner group name */
        char devmajor[8];      /* device major number */
        char devminor[8];      /* device minor number */
        char prefix[155];      /* prefix for file name */
   } dbuf;
};

/* values of typeflag / linkflag */
#define TARTYPE_REG  '0'       /* USTAR and old tar */
#define TARTYPE_LNK  '1'       /* USTAR and old tar */
#define TARTYPE_SYM  '2'       /* USTAR and old tar */
#define TARTYPE_CHR  '3'       /* USTAR */
#define TARTYPE_BLK  '4'       /* USTAR */
#define TARTYPE_DIR  '5'       /* USTAR */
#define TARTYPE_FIFO '6'       /* USTAR */

struct tar_node_info {
    struct   inode*         ti_self;
    struct   tar_node_info* ti_parent;
    struct   tar_node_info* ti_children;
    struct   tar_node_info* ti_next_sibling;
    char*                   ti_name;
    char*                   ti_linktargetname;
};

/* modes */
#define IALLOC  0100000 /* i-node is allocated */
#define ILARG   010000  /* large file */
#define IFMT    0170000 /* type of file */

static inline mode_t
ancientfs_tar_mode(mode_t mode, uint32_t flags)
{
    return mode & (uint16_t)~(IALLOC & ILARG);
}

#endif /* _ANCIENTFS_TAR_H_ */
