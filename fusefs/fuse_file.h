/*
 * Copyright (C) 2006 Google. All Rights Reserved.
 * Amit Singh <singh@>
 */

#ifndef _FUSE_FILE_H_
#define _FUSE_FILE_H_

#include <sys/types.h>
#include <sys/kernel_types.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/vnode.h>

typedef enum fufh_type {
    FUFH_INVALID = -1,
    FUFH_RDONLY  = 0,
    FUFH_WRONLY  = 1,
    FUFH_RDWR    = 2,
    FUFH_MAXTYPE = 3,
} fufh_type_t;

#define FUFH_VALID    0x00000001
#define FUFH_MAPPED   0x00000002
#define FUFH_STRATEGY 0x00000004

struct fuse_filehandle {
    uint64_t    fh_id;
    fufh_type_t type;
    int         fufh_flags;
    int         open_count;
    int         open_flags;
    int         fuse_open_flags;
};
typedef struct fuse_filehandle * fuse_filehandle_t;

static __inline__ fufh_type_t
fuse_filehandle_xlate_from_mmap(int fflags)
{
    if (fflags & (PROT_READ | PROT_WRITE)) {
        return FUFH_RDWR;
    } else if (fflags & (PROT_WRITE)) {
        return FUFH_WRONLY;
    } else if ((fflags & PROT_READ) || (fflags & PROT_EXEC)) {
        return FUFH_RDONLY;
    } else {
        return FUFH_INVALID;
    }
}

static __inline__ fufh_type_t
fuse_filehandle_xlate_from_fflags(int fflags)
{
    if ((fflags & FREAD) && (fflags & FWRITE)) {
        return FUFH_RDWR;
    } else if (fflags & (FWRITE)) {
        return FUFH_WRONLY;
    } else if (fflags & (FREAD)) {
        return FUFH_RDONLY;
    } else {
        panic("What kind of a flag is this?");
    }
}

static __inline__ int
fuse_filehandle_xlate_to_oflags(fufh_type_t type)
{
    int oflags = -1;

    switch (type) {
    case FUFH_RDONLY:
        oflags = O_RDONLY;
        break;
    case FUFH_WRONLY:
        oflags = O_WRONLY;
        break;
    case FUFH_RDWR:
        oflags = O_RDWR;
        break;
    }

    return oflags;
}

int fuse_filehandle_get(vnode_t vp, vfs_context_t context,
                        fufh_type_t fufh_type);
int fuse_filehandle_put(vnode_t vp, vfs_context_t context,
                        fufh_type_t fufh_type, int foregrounded);

#endif /* _FUSE_FILE_H_ */
