/*
 * Copyright (C) 2006 Google. All Rights Reserved.
 * Amit Singh <singh@>
 */

#ifndef _FUSE_IPC_H_
#define _FUSE_IPC_H_

#include <mach/mach_types.h>
#include <sys/errno.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/vnode_if.h>
#include <sys/kernel_types.h>
#include <sys/stat.h>
#include <sys/dirent.h>
#include <sys/uio.h>
#include <sys/proc.h>
#include <sys/fcntl.h>
#include <kern/assert.h>
#include <libkern/libkern.h>
#include <libkern/OSMalloc.h>
#include <libkern/locks.h>

#include "fuse.h"

struct fuse_iov {
    void   *base;
    size_t  len;
    size_t  allocated_size;
    int     credit;
};

void fiov_init(struct fuse_iov *fiov, size_t size);
void fiov_teardown(struct fuse_iov *fiov);
void fiov_refresh(struct fuse_iov *fiov);
void fiov_adjust(struct fuse_iov *fiov, size_t size);

#define FUSE_DIMALLOC(fiov, spc1, spc2, amnt)          \
do {                                                   \
    fiov_adjust(fiov, (sizeof(*(spc1)) + (amnt)));     \
    (spc1) = (fiov)->base;                             \
    (spc2) = (char *)(fiov)->base + (sizeof(*(spc1))); \
} while (0)

#define FU_AT_LEAST(siz) max((siz), 160)

struct fuse_ticket;
struct fuse_data;

typedef int fuse_handler_t(struct fuse_ticket *ftick, uio_t uio);

struct fuse_ticket {
    uint64_t                     tk_unique;
    struct fuse_data            *tk_data;
    int                          tk_flag;
    unsigned int                 tk_age;

    STAILQ_ENTRY(fuse_ticket)    tk_freetickets_link;
    TAILQ_ENTRY(fuse_ticket)     tk_alltickets_link;

    struct fuse_iov              tk_ms_fiov;
    void                        *tk_ms_bufdata;
    unsigned long                tk_ms_bufsize;
    enum { FT_M_FIOV, FT_M_BUF } tk_ms_type;
    STAILQ_ENTRY(fuse_ticket)    tk_ms_link;

    struct fuse_iov              tk_aw_fiov;
    void                        *tk_aw_bufdata;
    unsigned long                tk_aw_bufsize;
    enum { FT_A_FIOV, FT_A_BUF } tk_aw_type;

    struct fuse_out_header       tk_aw_ohead;
    int                          tk_aw_errno;
    lck_mtx_t                   *tk_aw_mtx;
    fuse_handler_t              *tk_aw_handler;
    TAILQ_ENTRY(fuse_ticket)     tk_aw_link;
};

#define FT_ANSW  0x01  // request of ticket has already been answered
#define FT_INVAL 0x02  // ticket is invalidated
#define FT_DIRTY 0x04  // ticket has been used

static __inline__
struct fuse_iov *
fticket_resp(struct fuse_ticket *ftick)
{
    kdebug_printf("-> ftick=%p\n", ftick);
    return (&ftick->tk_aw_fiov);
}

static __inline__
int
fticket_answered(struct fuse_ticket *ftick)
{
    kdebug_printf("-> ftick=%p\n", ftick);
    return (ftick->tk_flag & FT_ANSW);
}

static __inline__
void
fticket_set_answered(struct fuse_ticket *ftick)
{
    kdebug_printf("-> ftick=%p\n", ftick);
    ftick->tk_flag |= FT_ANSW;
}

static __inline__
enum fuse_opcode
fticket_opcode(struct fuse_ticket *ftick)
{
    kdebug_printf("-> ftick=%p\n", ftick);
    return (((struct fuse_in_header *)(ftick->tk_ms_fiov.base))->opcode);
}

static __inline__
void
fticket_invalidate(struct fuse_ticket *ftick)
{
    kdebug_printf("-> ftick=%p\n", ftick);
    ftick->tk_flag |= FT_INVAL;
}

int fticket_pull(struct fuse_ticket *ftick, uio_t uio);

enum mountpri { FM_NOMOUNTED, FM_PRIMARY, FM_SECONDARY };

/*
 * The data representing a FUSE session.
 */
struct fuse_data {
    enum mountpri              mpri;
    int                        mntco;
    struct fuse_softc         *fdev;
    mount_t                    mp;
    kauth_cred_t               daemoncred;
    pid_t                      daemonpid;
    uint32_t                   dataflags;     /* effective fuse_data flags */
    uint64_t                   mountaltflags; /* as-is copy of altflags    */
    uint64_t                   noimplflags;   /* not-implemented flags     */

    lck_mtx_t                 *ms_mtx;
    STAILQ_HEAD(, fuse_ticket) ms_head;

    lck_mtx_t                 *aw_mtx;
    TAILQ_HEAD(, fuse_ticket)  aw_head;

    lck_mtx_t                 *ticket_mtx;
    STAILQ_HEAD(, fuse_ticket) freetickets_head;
    TAILQ_HEAD(, fuse_ticket)  alltickets_head;
    unsigned                   freeticket_counter;
    uint64_t                   ticketer;

#if M_MACFUSE_EXPLICIT_RENAME_LOCK
    lck_rw_t                  *rename_lock;
#endif

    uint32_t                   fuse_libabi_major;
    uint32_t                   fuse_libabi_minor;

    uint32_t                   max_write;
    uint32_t                   max_read;
    uint32_t                   blocksize;
    uint32_t                   iosize;
    uint32_t                   subtype;
    char                       volname[MAXPATHLEN];

#if M_MACFUSE_ENABLE_INIT_TIMEOUT
    uint32_t                   callout_status;
    lck_mtx_t                 *callout_mtx;
    thread_call_t              thread_call;
#endif

    uint32_t                   timeout_status;
    lck_mtx_t                 *timeout_mtx;
    struct timespec            daemon_timeout;
    struct timespec           *daemon_timeout_p;
    struct timespec            init_timeout;
};

enum {
    FUSE_DAEMON_TIMEOUT_NONE       = 0,
    FUSE_DAEMON_TIMEOUT_PROCESSING = 1, 
    FUSE_DAEMON_TIMEOUT_DEAD       = 2,
};

enum {
    INIT_CALLOUT_INACTIVE = 0,
    INIT_CALLOUT_ACTIVE   = 1,
};

/* Not-Implemented Bits */

#define FSESS_NOIMPL(MSG)         (1LL << FUSE_##MSG)

#define FSESS_KICK                0x00000001 // session is to be closed
#define FSESS_OPENED              0x00000002 // session device has been opened
#define FSESS_INITED              0x00000004 // session has been inited
#define FSESS_ALLOW_OTHER         0x00000008
#define FSESS_ALLOW_ROOT          0x00000010
#define FSESS_DEFAULT_PERMISSIONS 0x00000020
#define FSESS_DEFER_AUTH          0x00000040
#define FSESS_DIRECT_IO           0x00000080
#define FSESS_EXTENDED_SECURITY   0x00000100
#define FSESS_JAIL_SYMLINKS       0x00000200
#define FSESS_KILL_ON_UNMOUNT     0x00000400
#define FSESS_NO_ALERTS           0x00000800
#define FSESS_NO_APPLESPECIAL     0x00001000
#define FSESS_NO_ATTRCACHE        0x00002000
#define FSESS_NO_READAHEAD        0x00004000
#define FSESS_NO_SYNCWRITES       0x00008000
#define FSESS_NO_SYNCONCLOSE      0x00010000
#define FSESS_NO_VNCACHE          0x00020000
#define FSESS_NO_UBC              0x00040000
#define FSESS_VOL_RENAME          0x00080000

static __inline__
struct fuse_data *
fuse_get_mpdata(mount_t mp)
{
    struct fuse_data *data = vfs_fsprivate(mp);
    kdebug_printf("-> mp=%p\n", mp);
    return (data->mpri == FM_PRIMARY ? data : NULL);
}

static __inline__
void
fuse_ms_push(struct fuse_ticket *ftick)
{
    kdebug_printf("-> ftick=%p\n", ftick);
    STAILQ_INSERT_TAIL(&ftick->tk_data->ms_head, ftick, tk_ms_link);
}

static __inline__
struct fuse_ticket *
fuse_ms_pop(struct fuse_data *data)
{
    struct fuse_ticket *ftick = NULL;

    kdebug_printf("-> data=%p\n", data);

    if ((ftick = STAILQ_FIRST(&data->ms_head))) {
        STAILQ_REMOVE_HEAD(&data->ms_head, tk_ms_link);
    }

    return (ftick);
}

static __inline__
void
fuse_aw_push(struct fuse_ticket *ftick)
{
    kdebug_printf("-> ftick=%p\n", ftick);
    TAILQ_INSERT_TAIL(&ftick->tk_data->aw_head, ftick, tk_aw_link);
}

static __inline__
void
fuse_aw_remove(struct fuse_ticket *ftick)
{
    kdebug_printf("-> ftick=%p\n", ftick);
    TAILQ_REMOVE(&ftick->tk_data->aw_head, ftick, tk_aw_link);
}

static __inline__
struct fuse_ticket *
fuse_aw_pop(struct fuse_data *data)
{
    struct fuse_ticket *ftick = NULL;

    kdebug_printf("-> data=%p\n", data);

    if ((ftick = TAILQ_FIRST(&data->aw_head))) {
        fuse_aw_remove(ftick);
    }

    return (ftick);
}

struct fuse_ticket *fuse_ticket_fetch(struct fuse_data *data);
void fuse_ticket_drop(struct fuse_ticket *ftick);
void fuse_ticket_drop_invalid(struct fuse_ticket *ftick);
void fuse_insert_callback(struct fuse_ticket *ftick, fuse_handler_t *handler);
void fuse_insert_message(struct fuse_ticket *ftick);

static __inline__
int
fuse_libabi_geq(struct fuse_data *data, uint32_t abi_maj, uint32_t abi_min)
{
    return (data->fuse_libabi_major > abi_maj ||
            (data->fuse_libabi_major == abi_maj &&
             data->fuse_libabi_minor >= abi_min));
}

struct fuse_secondary_data {
    enum mountpri     mpri;
    mount_t           mp;
    struct fuse_data *master;

    LIST_ENTRY(fuse_secondary_data) slaves_link;
};

static __inline__
struct fuse_secondary_data *
fuse_get_secondary_mpdata(mount_t mp)
{
    struct fuse_secondary_data *fsdat = vfs_fsprivate(mp);
    return (fsdat->mpri == FM_SECONDARY ? fsdat : NULL);
}

struct fuse_data *fdata_alloc(struct fuse_softc *fdev, struct proc *p);
void fdata_destroy(struct fuse_data *data);
int  fdata_kick_get(struct fuse_data *data);
void fdata_kick_set(struct fuse_data *data);

struct fuse_dispatcher {

    struct fuse_ticket    *tick;
    struct fuse_in_header *finh;

    void    *indata;
    size_t   iosize;
    uint64_t nodeid;
    int      answ_stat;
    void    *answ;
};

static __inline__
void
fdisp_init(struct fuse_dispatcher *fdisp, size_t iosize)
{
    kdebug_printf("-> fdisp=%p, iosize=%lx\n", fdisp, iosize);
    fdisp->iosize = iosize;
    fdisp->tick = NULL;
}

void fdisp_make(struct fuse_dispatcher *fdip, enum fuse_opcode op,
                mount_t mp, uint64_t nid, vfs_context_t context);

void fdisp_make_vp(struct fuse_dispatcher *fdip, enum fuse_opcode op,
                   vnode_t vp, vfs_context_t context);

int  fdisp_wait_answ(struct fuse_dispatcher *fdip);

static __inline__
int
fdisp_simple_putget_vp(struct fuse_dispatcher *fdip, enum fuse_opcode op,
                       vnode_t vp, vfs_context_t context)
{
    kdebug_printf("-> fdip=%p, opcode=%d, vp=%p, context=%p\n", fdip, op, vp, context);
    fdisp_init(fdip, 0);
    fdisp_make_vp(fdip, op, vp, context);
    return (fdisp_wait_answ(fdip));
}

static __inline__
int
fdisp_simple_vfs_getattr(struct fuse_dispatcher *fdip,
                         mount_t                 mp,
                         vfs_context_t           context)
{
   fdisp_init(fdip, 0);
   fdisp_make(fdip, FUSE_STATFS, mp, FUSE_ROOT_ID, context);
   return (fdisp_wait_answ(fdip));
}

#endif /* _FUSE_IPC_H_ */
