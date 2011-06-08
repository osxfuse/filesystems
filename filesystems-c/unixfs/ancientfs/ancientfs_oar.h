/*
 * Ancient UNIX File Systems for MacFUSE
 * Amit Singh
 * http://osxbook.com
 */

#ifndef _ANCIENTFS_OAR_H_
#define _ANCIENTFS_OAR_H_

#include "unixfs_internal.h"
#include "ancientfs.h"

typedef int16_t  a_short;       /* ancient short */
typedef uint16_t a_ushort;      /* ancient unsigned short */
typedef int16_t  a_int;         /* ancient int */
typedef uint16_t a_uint;        /* ancient unsigned int */
typedef int32_t  a_long;        /* ancient long */
typedef uint32_t a_ulong;       /* ancient unsigned long */

typedef a_uint   a_ino_t;       /* ancient inode type (ancient unsigned int) */
typedef a_long   a_off_t;       /* ancient offset type (ancient long) */
typedef a_long   a_time_t;      /* ancient time type (ancient long) */
typedef a_long   a_daddr_t;     /* ancient disk address type (ancient long) */
typedef a_int    a_dev_t;       /* ancient device type (ancient int) */

#define BSIZE    512
#define BMASK    0777           /* BSIZE - 1 */

#define ROOTINO  1
#define DIRSIZ   14

struct filsys
{
    uint32_t s_fsize;
    uint32_t s_files;
    uint32_t s_directories;
    uint32_t s_lastino;
    struct inode* s_rootip;
};

#define ARMAG  (uint16_t)0177545

struct ar_hdr
{
    char   ar_name[DIRSIZ];
    a_long ar_date;
    char   ar_uid;
    char   ar_gid;
    a_int  ar_mode;
    a_long ar_size;
} __attribute__((packed));

struct ar_node_info
{ 
    struct inode* ar_self;
    uint8_t ar_name[DIRSIZ + 1];
    struct ar_node_info* ar_parent;
    struct ar_node_info* ar_children;
    struct ar_node_info* ar_next_sibling;
};

/* modes */
#define IALLOC  0100000 /* file is used */
#define IFMT    060000
#define ILARG   010000  /* large addressing algorithm */

static inline a_int
ancientfs_ar_mode(mode_t mode)
{
    a_int newmode = (a_int)mode & ~(IALLOC | ILARG);
    if ((newmode & IFMT) == 0) /* ancient regular file */
        newmode |= S_IFREG;
    return newmode;
}
   
#endif /* _ANCIENTFS_OAR_H_ */
