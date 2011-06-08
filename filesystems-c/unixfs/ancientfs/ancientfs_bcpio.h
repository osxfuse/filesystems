/*
 * Ancient UNIX File Systems for MacFUSE
 * Amit Singh
 * http://osxbook.com
 */

#ifndef _ANCIENTFS_BCPIO_H_
#define _ANCIENTFS_BCPIO_H_

#include "unixfs_internal.h"
#include "ancientfs.h"

typedef int16_t  a_short;      /* ancient short */
typedef uint16_t a_ushort;     /* ancient unsigned short */
typedef int16_t  a_int;        /* ancient int */
typedef uint16_t a_uint;       /* ancient unsigned int */
typedef int32_t  a_long;       /* ancient long */
typedef uint32_t a_ulong;      /* ancient unsigned long */

typedef a_uint   a_ino_t;      /* ancient inode type (ancient unsigned int) */
typedef a_long   a_off_t;      /* ancient offset type (ancient long) */
typedef a_ulong  a_time_t;     /* ancient time type (ancient long) */
typedef a_long   a_daddr_t;    /* ancient disk address type (ancient long) */
typedef a_int    a_dev_t;      /* ancient device type (ancient int) */

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

#define BCBLOCK       512
#define BCPIO_MAGIC   070707
#define BCPIO_TRAILER "TRAILER!!!"
#define BCPIO_TRAILER_LEN (sizeof(BCPIO_TRAILER)/sizeof(unsigned char))

struct bcpio_header {
    a_uint  h_magic;
    a_uint  h_dev;
    a_uint  h_ino;
    a_uint  h_mode;
    a_uint  h_uid;
    a_uint  h_gid;
    a_uint  h_nlink;
    a_uint  h_majmin;
    a_int   h_mtime_msb16;
    a_int   h_mtime_lsb16;
    a_uint  h_namesize;
    a_uint  h_filesize_msb16;
    a_uint  h_filesize_lsb16;
 /* char    h_name[h_namesize rounded to word]; */
 /* char    h_data[h_filesize rounded to word]; */
} __attribute__((packed));

struct bcpio_node_info {
    struct inode*           ci_self;
    struct bcpio_node_info* ci_parent;
    struct bcpio_node_info* ci_children;
    struct bcpio_node_info* ci_next_sibling;
    char*                   ci_name;
    char*                   ci_linktargetname;
};

/* modes */
#define IALLOC  0100000 /* i-node is allocated */
#define ILARG   010000  /* large file */
#define IFMT    0170000 /* type of file */

static inline mode_t
ancientfs_bcpio_mode(mode_t mode, uint32_t flags)
{
    return mode & (uint16_t)~(IALLOC & ILARG);
}

#endif /* _ANCIENTFS_BCPIO_H_ */
