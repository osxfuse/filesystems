/*
 * Copyright (C) 2006 Google. All Rights Reserved.
 * Amit Singh <singh@>
 */

#include <sys/types.h>
#include <sys/malloc.h>

#include "fuse.h"
#include "fuse_ipc.h"
#include "fuse_locking.h"
#include "fuse_node.h"
#include "fuse_sysctl.h"

#include <fuse_param.h>

static struct fuse_ticket *fticket_alloc(struct fuse_data *data);
static void                fticket_refresh(struct fuse_ticket *tick);
static void                fticket_destroy(struct fuse_ticket *tick);
static int                 fticket_wait_answer(struct fuse_ticket *tick);
static __inline__ int  fticket_aw_pull_uio(struct fuse_ticket *tick, uio_t uio);
static __inline__ void fuse_push_freeticks(struct fuse_ticket *tick);

static __inline__ struct fuse_ticket *
fuse_pop_freeticks(struct fuse_data *data);

static __inline__ void     fuse_push_allticks(struct fuse_ticket *tick);
static __inline__ void     fuse_remove_allticks(struct fuse_ticket *tick);
static struct fuse_ticket *fuse_pop_allticks(struct fuse_data *data);

static int             fuse_body_audit(struct fuse_ticket *tick, size_t blen);
static __inline__ void fuse_setup_ihead(struct fuse_in_header *ihead,
                                        struct fuse_ticket *tick, uint64_t nid,
                                        enum fuse_opcode op, size_t blen,
                                        vfs_context_t context);

static fuse_handler_t  fuse_standard_handler;

void
fiov_init(struct fuse_iov *fiov, size_t size)
{
    uint32_t msize = FU_AT_LEAST(size);

    debug_printf("fiov=%p, size=%lx\n", fiov, size);

    fiov->len = 0;

    fiov->base = FUSE_OSMalloc(msize, fuse_malloc_tag);
    if (!fiov->base) {
        panic("MacFUSE: OSMalloc failed in fiov_init");
    }

    FUSE_OSAddAtomic(1, (SInt32 *)&fuse_iov_current);

    bzero(fiov->base, msize);

    fiov->allocated_size = msize;
    fiov->credit = fuse_iov_credit;
}

void
fiov_teardown(struct fuse_iov *fiov)
{
    debug_printf("fiov=%p\n", fiov);

    FUSE_OSFree(fiov->base, fiov->allocated_size, fuse_malloc_tag);
    fiov->allocated_size = 0;

    FUSE_OSAddAtomic(-1, (SInt32 *)&fuse_iov_current);
}

void
fiov_adjust(struct fuse_iov *fiov, size_t size)
{
    debug_printf("fiov=%p, size=%lx\n", fiov, size);

    if (fiov->allocated_size < size ||
        (fuse_iov_permanent_bufsize >= 0 &&
         fiov->allocated_size - size > fuse_iov_permanent_bufsize &&
             --fiov->credit < 0)) {

        fiov->base = FUSE_OSRealloc(fiov->base, fiov->allocated_size,
                                    FU_AT_LEAST(size));
        if (!fiov->base) {
            panic("MacFUSE: realloc failed");
        }

        fiov->allocated_size = FU_AT_LEAST(size);
        fiov->credit = fuse_iov_credit;
    }

    fiov->len = size;
}

void
fiov_refresh(struct fuse_iov *fiov)
{
    debug_printf("fiov=%p\n", fiov);

    bzero(fiov->base, fiov->len);    
    fiov_adjust(fiov, 0);
}

static struct fuse_ticket *
fticket_alloc(struct fuse_data *data)
{
    struct fuse_ticket *tick;

    debug_printf("data=%p\n", data);

    tick = (struct fuse_ticket *)FUSE_OSMalloc(sizeof(struct fuse_ticket),
                                               fuse_malloc_tag);
    if (!tick) {
        panic("MacFUSE: OSMalloc failed in fticket_alloc");
    }

    FUSE_OSAddAtomic(1, (SInt32 *)&fuse_tickets_current);

    bzero(tick, sizeof(struct fuse_ticket));

    tick->tk_unique = data->ticketer++;
    tick->tk_data = data;

    fiov_init(&tick->tk_ms_fiov, sizeof(struct fuse_in_header));
    tick->tk_ms_type = FT_M_FIOV;

    tick->tk_aw_mtx = lck_mtx_alloc_init(fuse_lock_group, fuse_lock_attr);
    fiov_init(&tick->tk_aw_fiov, 0);
    tick->tk_aw_type = FT_A_FIOV;

    return (tick);
}

static __inline__
void
fticket_refresh(struct fuse_ticket *tick)
{
    debug_printf("tick=%p\n", tick);

    fiov_refresh(&tick->tk_ms_fiov);
    tick->tk_ms_bufdata = NULL;
    tick->tk_ms_bufsize = 0;
    tick->tk_ms_type = FT_M_FIOV;

    bzero(&tick->tk_aw_ohead, sizeof(struct fuse_out_header));

    fiov_refresh(&tick->tk_aw_fiov);
    tick->tk_aw_errno = 0;
    tick->tk_aw_bufdata = NULL;
    tick->tk_aw_bufsize = 0;
    tick->tk_aw_type = FT_A_FIOV;

    tick->tk_flag = 0;
    tick->tk_age++;
}

static void
fticket_destroy(struct fuse_ticket *tick)
{
    debug_printf("tick=%p\n", tick);

    fiov_teardown(&tick->tk_ms_fiov);

    lck_mtx_free(tick->tk_aw_mtx, fuse_lock_group);
    tick->tk_aw_mtx = NULL;
    fiov_teardown(&tick->tk_aw_fiov);

    FUSE_OSFree(tick, sizeof(struct fuse_ticket), fuse_malloc_tag);

    FUSE_OSAddAtomic(-1, (SInt32 *)&fuse_tickets_current);
}

static int
fticket_wait_answer(struct fuse_ticket *tick)
{
    int err = 0;
    struct fuse_data *data;

    debug_printf("tick=%p\n", tick);
    fuse_lck_mtx_lock(tick->tk_aw_mtx);

    if (fticket_answered(tick)) {
        goto out;
    }

    data = tick->tk_data;

    if (fdata_kick_get(data)) {
        err = ENOTCONN;
        fticket_set_answered(tick);
        goto out;
    }

    err = msleep(tick, tick->tk_aw_mtx, PCATCH, "fu_ans",
                 data->daemon_timeout_p);
    if (err == EAGAIN) {
        if (!fdata_kick_get(data)) {
            fdata_kick_set(data);
        }
        vfs_event_signal(&vfs_statfs(data->mp)->f_fsid, VQ_DEAD, 0);
        err = ENOTCONN;
        fticket_set_answered(tick);
        goto out;
    }

    /*
     * An experimental version of the above:
    {
        struct timespec ts = { 1, 0 };
again:
        err = msleep(tick, tick->tk_aw_mtx, PCATCH, "fu_ans", &ts);
        if (err == EAGAIN) {
            if (fdata_kick_get(tick->tk_data)) {
                err = ENOTCONN;
                fticket_set_answered(tick);
                goto out;
            }
            goto again;
        }
    }
    *
    */

out:
    fuse_lck_mtx_unlock(tick->tk_aw_mtx);

    if (!(err || fticket_answered(tick))) {
        debug_printf("fuse requester was woken up but still no answer");
        err = ENXIO;
    }

    return (err);
}

static __inline__
int
fticket_aw_pull_uio(struct fuse_ticket *tick, uio_t uio)
{
    int err = 0;
    size_t len = uio_resid(uio);

    debug_printf("tick=%p, uio=%p\n", tick, uio);

    if (len) {
        switch (tick->tk_aw_type) {
        case FT_A_FIOV:
            fiov_adjust(fticket_resp(tick), len);
            err = uiomove(fticket_resp(tick)->base, len, uio);
            if (err) {
                debug_printf("FT_A_FIOV: error is %d (%p, %ld, %p)\n",
                             err, fticket_resp(tick)->base, len, uio);
            }
            break;

        case FT_A_BUF:
            tick->tk_aw_bufsize = len;
            err = uiomove(tick->tk_aw_bufdata, len, uio);
            if (err) {
                debug_printf("FT_A_BUF: error is %d (%p, %ld, %p)\n",
                             err, tick->tk_aw_bufdata, len, uio);
            }
            break;

        default:
            panic("MacFUSE: unknown answer type for ticket %p", tick);
        }
    }

    return (err);
}

int
fticket_pull(struct fuse_ticket *tick, uio_t uio)
{
    int err = 0;

    debug_printf("tick=%p, uio=%p\n", tick, uio);

    if (tick->tk_aw_ohead.error) {
        return (0);
    }

    err = fuse_body_audit(tick, uio_resid(uio));
    if (!err) {
        err = fticket_aw_pull_uio(tick, uio);
    }

    return (err);
}

struct fuse_data *
fdata_alloc(struct fuse_softc *fdev, struct proc *p)
{
    struct fuse_data *data;

    debug_printf("fdev=%p, p=%p\n", fdev, p);

    data = (struct fuse_data *)FUSE_OSMalloc(sizeof(struct fuse_data),
                                             fuse_malloc_tag);
    if (!data) {
        panic("MacFUSE: OSMalloc failed in fdata_alloc");
    }

    bzero(data, sizeof(struct fuse_data));

    data->mpri = FM_NOMOUNTED;
    data->fdev = fdev;
    data->dataflag = 0;
    data->ms_mtx = lck_mtx_alloc_init(fuse_lock_group, fuse_lock_attr);
    STAILQ_INIT(&data->ms_head);
    data->ticket_mtx = lck_mtx_alloc_init(fuse_lock_group, fuse_lock_attr);
    debug_printf("ALLOC_INIT data=%p ticket_mtx=%p\n", data, data->ticket_mtx);
    STAILQ_INIT(&data->freetickets_head);
    TAILQ_INIT(&data->alltickets_head);
    data->aw_mtx = lck_mtx_alloc_init(fuse_lock_group, fuse_lock_attr);
    TAILQ_INIT(&data->aw_head);
    data->ticketer = 0;
    data->freeticket_counter = 0;
    data->daemoncred = proc_ucred(p);
    kauth_cred_ref(data->daemoncred);

#if 0
    data->mhierlock = lck_rw_alloc_init(fuse_lock_group, fuse_lock_attr);
    LIST_INIT(&data->slaves_head);
#endif

#if FUSE_EXCPLICIT_RENAME_LOCK
    data->rename_lock = lck_rw_alloc_init(fuse_lock_group, fuse_lock_attr);
#endif

    return (data);
}

void
fdata_destroy(struct fuse_data *data)
{
    struct fuse_ticket *tick;

    debug_printf("data=%p, destroy.mntco = %d\n", data, data->mntco);

    lck_mtx_free(data->ms_mtx, fuse_lock_group);
    data->ms_mtx = NULL;

    lck_mtx_free(data->aw_mtx, fuse_lock_group);
    data->aw_mtx = NULL;

    lck_mtx_free(data->ticket_mtx, fuse_lock_group); /* XXX */
    data->ticket_mtx = NULL; /* XXX */

#if FUSE_EXPLICIT_RENAME_LOCK
    lck_rw_free(data->rename_lock, fuse_lock_group);
    data->rename_lock = NULL;
#endif

    while ((tick = fuse_pop_allticks(data))) {
        fticket_destroy(tick);
    }

    kauth_cred_rele(data->daemoncred);

#if 0
    lck_rw_free(data->mhierlock, fuse_lock_group);
    data->mhierlock = NULL;
#endif

    FUSE_OSFree(data, sizeof(struct fuse_data), fuse_malloc_tag);
}

int
fdata_kick_get(struct fuse_data *data)
{
    debug_printf("data=%p\n", data);

    return (data->dataflag & FSESS_KICK);
}

void
fdata_kick_set(struct fuse_data *data)
{
    debug_printf("data=%p\n", data);

    fuse_lck_mtx_lock(data->ms_mtx);
    if (fdata_kick_get(data)) { 
        fuse_lck_mtx_unlock(data->ms_mtx);
        return;
    }

    data->dataflag |= FSESS_KICK;
    wakeup_one((caddr_t)data);
    fuse_lck_mtx_unlock(data->ms_mtx);

    fuse_lck_mtx_lock(data->ticket_mtx);
    wakeup(&data->ticketer);
    fuse_lck_mtx_unlock(data->ticket_mtx);

    vfs_event_signal(&vfs_statfs(data->mp)->f_fsid, VQ_NOTRESP, 0);
}

static __inline__
void
fuse_push_freeticks(struct fuse_ticket *tick)
{
    debug_printf("tick=%p\n", tick);

    STAILQ_INSERT_TAIL(&tick->tk_data->freetickets_head, tick,
                       tk_freetickets_link);
    tick->tk_data->freeticket_counter++;
}

static __inline__
struct fuse_ticket *
fuse_pop_freeticks(struct fuse_data *data)
{
    struct fuse_ticket *tick;

    debug_printf("data=%p\n", data);

    if ((tick = STAILQ_FIRST(&data->freetickets_head))) {
        STAILQ_REMOVE_HEAD(&data->freetickets_head, tk_freetickets_link);
        data->freeticket_counter--;
    }

    if (STAILQ_EMPTY(&data->freetickets_head) &&
        (data->freeticket_counter != 0)) {
        panic("MacFUSE: ticket count mismatch!");
    }

    return tick;
}

static __inline__
void
fuse_push_allticks(struct fuse_ticket *tick)
{
    debug_printf("tick=%p\n", tick);

    TAILQ_INSERT_TAIL(&tick->tk_data->alltickets_head, tick,
                      tk_alltickets_link);
}

static __inline__
void
fuse_remove_allticks(struct fuse_ticket *tick)
{
    debug_printf("tick=%p\n", tick);

    TAILQ_REMOVE(&tick->tk_data->alltickets_head, tick, tk_alltickets_link);
}

static struct fuse_ticket *
fuse_pop_allticks(struct fuse_data *data)
{
    struct fuse_ticket *tick;

    debug_printf("data=%p\n", data);

    if ((tick = TAILQ_FIRST(&data->alltickets_head))) {
        fuse_remove_allticks(tick);
    }

    return (tick);
}

struct fuse_ticket *
fuse_ticket_fetch(struct fuse_data *data)
{
    int err = 0;
    struct fuse_ticket *tick;

    debug_printf("data=%p\n", data);

    fuse_lck_mtx_lock(data->ticket_mtx);

    if (data->freeticket_counter == 0) {
        fuse_lck_mtx_unlock(data->ticket_mtx);
        tick = fticket_alloc(data);
        if (!tick) {
            panic("MacFUSE: ticket allocation failed");
        }
        fuse_lck_mtx_lock(data->ticket_mtx);
        fuse_push_allticks(tick);
    } else {
        /* locked here */
        tick = fuse_pop_freeticks(data);
        if (!tick) {
            panic("MacFUSE: no free ticket despite the counter's value");
        }
    }

    if (!(data->dataflag & FSESS_INITED) && data->ticketer > 1) {
        err = msleep(&data->ticketer, data->ticket_mtx, PCATCH | PDROP,
                     "fu_ini", 0);
    } else {
        fuse_lck_mtx_unlock(data->ticket_mtx);
    }

    if (err) {
        fdata_kick_set(data);
    }

    return (tick);
}

void
fuse_ticket_drop(struct fuse_ticket *tick)
{
    int die = 0;

    debug_printf("tick=%p\n", tick);

    fuse_lck_mtx_lock(tick->tk_data->ticket_mtx);

    if (fuse_max_freetickets >= 0 &&
        fuse_max_freetickets <= tick->tk_data->freeticket_counter) {
        die = 1;
    } else {
        fuse_lck_mtx_unlock(tick->tk_data->ticket_mtx);
        fticket_refresh(tick);
        fuse_lck_mtx_lock(tick->tk_data->ticket_mtx);
    }

    /* locked here */

    if (die) {
        fuse_remove_allticks(tick);
        fuse_lck_mtx_unlock(tick->tk_data->ticket_mtx);
        fticket_destroy(tick);
    } else {
        fuse_push_freeticks(tick);
        fuse_lck_mtx_unlock(tick->tk_data->ticket_mtx);
    }
}

void
fuse_ticket_drop_invalid(struct fuse_ticket *tick)
{
    debug_printf("tick=%p\n", tick);

    if (tick->tk_flag & FT_INVAL) {
        fuse_ticket_drop(tick);
    }
}

void
fuse_insert_callback(struct fuse_ticket *tick, fuse_handler_t *handler)
{
    debug_printf("tick=%p, handler=%p\n", tick, handler);

    if (fdata_kick_get(tick->tk_data)) {
        return;
    }

    tick->tk_aw_handler = handler;

    fuse_lck_mtx_lock(tick->tk_data->aw_mtx);
    fuse_aw_push(tick);
    fuse_lck_mtx_unlock(tick->tk_data->aw_mtx);
}

void
fuse_insert_message(struct fuse_ticket *tick)
{
    debug_printf("tick=%p\n", tick);

    if (tick->tk_flag & FT_DIRTY) {
        panic("MacFUSE: ticket reused without being refreshed");
    }

    tick->tk_flag |= FT_DIRTY;

    if (fdata_kick_get(tick->tk_data)) {
        return;
    }

    fuse_lck_mtx_lock(tick->tk_data->ms_mtx);
    fuse_ms_push(tick);
    wakeup_one((caddr_t)tick->tk_data);
    fuse_lck_mtx_unlock(tick->tk_data->ms_mtx);
}

static int
fuse_body_audit(struct fuse_ticket *tick, size_t blen)
{
    int err = 0;
    enum fuse_opcode opcode;

    debug_printf("tick=%p, blen = %lx\n", tick, blen);

    opcode = fticket_opcode(tick);

    switch (opcode) {
    case FUSE_LOOKUP:
        err = blen == sizeof(struct fuse_entry_out) ? 0 : EINVAL;
        break;

    case FUSE_FORGET:
        panic("MacFUSE: a handler has been intalled for FUSE_FORGET");
        break;

    case FUSE_GETATTR:
        err = blen == sizeof(struct fuse_attr_out) ? 0 : EINVAL;
        break;

    case FUSE_SETATTR:
        err = blen == sizeof(struct fuse_attr_out) ? 0 : EINVAL;
        break;

    case FUSE_READLINK:
        err = PAGE_SIZE >= blen ? 0 : EINVAL;
        break;

    case FUSE_SYMLINK:
        err = blen == sizeof(struct fuse_entry_out) ? 0 : EINVAL;
        break;

    case FUSE_MKNOD:
        err = blen == sizeof(struct fuse_entry_out) ? 0 : EINVAL;
        break;

    case FUSE_MKDIR:
        err = blen == sizeof(struct fuse_entry_out) ? 0 : EINVAL;
        break;

    case FUSE_UNLINK:
        err = blen == 0 ? 0 : EINVAL;
        break;

    case FUSE_RMDIR:
        err = blen == 0 ? 0 : EINVAL;
        break;

    case FUSE_RENAME:
        err = blen == 0 ? 0 : EINVAL;
        break;

    case FUSE_LINK:
        err = blen == sizeof(struct fuse_entry_out) ? 0 : EINVAL;
        break;

    case FUSE_OPEN:
        err = blen == sizeof(struct fuse_open_out) ? 0 : EINVAL;
        break;

    case FUSE_READ:
        err = ((struct fuse_read_in *)(
                (char *)tick->tk_ms_fiov.base +
                        sizeof(struct fuse_in_header)
                  ))->size >= blen ? 0 : EINVAL;
        break;

    case FUSE_WRITE:
        err = blen == sizeof(struct fuse_write_out) ? 0 : EINVAL;
        break;

    case FUSE_STATFS:
        if (fuse_libabi_geq(tick->tk_data, 7, 4)) {
            err = blen == sizeof(struct fuse_statfs_out) ? 0 : EINVAL;
        } else {
            err = blen == FUSE_COMPAT_STATFS_SIZE ? 0 : EINVAL;
        }
        break;

    case FUSE_RELEASE:
        err = blen == 0 ? 0 : EINVAL;
        break;

    case FUSE_FSYNC:
        err = blen == 0 ? 0 : EINVAL;
        break;

    case FUSE_SETXATTR:
        /* TBD */
        break;

    case FUSE_GETXATTR:
        /* TBD */
        break;

    case FUSE_LISTXATTR:
        /* TBD */
        break;

    case FUSE_REMOVEXATTR:
        /* TBD */
        break;

    case FUSE_FLUSH:
        err = blen == 0 ? 0 : EINVAL;
        break;

    case FUSE_INIT:
        if (blen == sizeof(struct fuse_init_out) || blen == 8) {
            err = 0;
        } else {
            err = EINVAL;
        }
        break;

    case FUSE_OPENDIR:
        err = blen == sizeof(struct fuse_open_out) ? 0 : EINVAL;
        break;

    case FUSE_READDIR:
        err = ((struct fuse_read_in *)(
                (char *)tick->tk_ms_fiov.base +
                        sizeof(struct fuse_in_header)
                  ))->size >= blen ? 0 : EINVAL;
        break;

    case FUSE_RELEASEDIR:
        err = blen == 0 ? 0 : EINVAL;
        break;

    case FUSE_FSYNCDIR:
        err = blen == 0 ? 0 : EINVAL;
        break;

    case FUSE_GETLK:
        panic("MacFUSE: no response body format check for FUSE_GETLK");
        break;

    case FUSE_SETLK:
        panic("MacFUSE: no response body format check for FUSE_SETLK");
        break;

    case FUSE_SETLKW:
        panic("MacFUSE: no response body format check for FUSE_SETLKW");
        break;

    case FUSE_ACCESS:
        err = blen == 0 ? 0 : EINVAL;
        break;

    case FUSE_CREATE:
        err = blen == sizeof(struct fuse_entry_out) +
                      sizeof(struct fuse_open_out) ? 0 : EINVAL;
        break;

    case FUSE_INTERRUPT:
        /* TBD */
        break;

    case FUSE_BMAP:
        /* TBD */
        break;

    case FUSE_DESTROY:
        /* TBD */
        break;

    default:
        panic("MacFUSE: opcodes out of sync");
    }

    return (err);
}

static void
fuse_setup_ihead(struct fuse_in_header *ihead,
                 struct fuse_ticket    *tick,
                 uint64_t               nid,
                 enum fuse_opcode       op,
                 size_t                 blen,
                 vfs_context_t          context)
{
    ihead->len = sizeof(*ihead) + blen;
    ihead->unique = tick->tk_unique;
    ihead->nodeid = nid;
    ihead->opcode = op;

    debug_printf("ihead=%p, tick=%p, nid=%llx, op=%d, blen=%lx, context=%p\n",
                 ihead, tick, nid, op, blen, context);

    if (context) {
        ihead->pid = vfs_context_pid(context);
        ihead->uid = vfs_context_ucred(context)->cr_uid;
        ihead->gid = vfs_context_ucred(context)->cr_gid;
    } else {
        /* XXX: The following needs more thought. */
        ihead->pid = proc_pid((proc_t)current_proc());
        ihead->uid = kauth_cred_getuid(kauth_cred_get());
        ihead->gid = kauth_cred_getgid(kauth_cred_get());
    }
}

static int
fuse_standard_handler(struct fuse_ticket *tick, uio_t uio)
{
    int err = 0;
    int dropflag = 0;

    debug_printf("tick=%p, uio=%p\n", tick, uio);

    err = fticket_pull(tick, uio);

    fuse_lck_mtx_lock(tick->tk_aw_mtx);

    if (fticket_answered(tick)) {
        dropflag = 1;
    } else {
        fticket_set_answered(tick);
        tick->tk_aw_errno = err;
        wakeup(tick);
    }

    fuse_lck_mtx_unlock(tick->tk_aw_mtx);

    if (dropflag) {
        fuse_ticket_drop(tick);
    }

    return (err);
}

void
fdisp_make(struct fuse_dispatcher *fdip,
           enum fuse_opcode        op,
           mount_t                 mp,
           uint64_t                nid,
           vfs_context_t           context)
{
    struct fuse_data *data = vfs_fsprivate(mp);

    debug_printf("fdip=%p, op=%d, mp=%p, nid=%llx, context=%p\n",
                 fdip, op, mp, nid, context);

    if (fdip->tick) {
        fticket_refresh(fdip->tick);
    } else {
        fdip->tick = fuse_ticket_fetch(data);
    }

    if (fdip->tick == 0) {
        panic("MacFUSE: fuse_ticket_fetch() failed");
    }

    FUSE_DIMALLOC(&fdip->tick->tk_ms_fiov, fdip->finh,
                  fdip->indata, fdip->iosize);

    fuse_setup_ihead(fdip->finh, fdip->tick, nid, op, fdip->iosize, context);
}

void
fdisp_make_vp(struct fuse_dispatcher *fdip,
              enum fuse_opcode        op,
              vnode_t                 vp,
              vfs_context_t           context)
{
    debug_printf("fdip=%p, op=%d, vp=%p, context=%p\n", fdip, op, vp, context);

    return (fdisp_make(fdip, op, vnode_mount(vp), VTOI(vp), context));
}

int
fdisp_wait_answ(struct fuse_dispatcher *fdip)
{
    int err = 0;

    fdip->answ_stat = 0;
    fuse_insert_callback(fdip->tick, fuse_standard_handler);
    fuse_insert_message(fdip->tick);

    if ((err = fticket_wait_answer(fdip->tick))) { // interrupted

#ifndef DONT_TRY_HARD_PREVENT_IO_IN_VAIN
        struct fuse_ticket *tick;
        unsigned            age;
#endif

        debug_printf("IPC: interrupted, err = %d\n", err);

        fuse_lck_mtx_lock(fdip->tick->tk_aw_mtx);

        if (fticket_answered(fdip->tick)) {
            debug_printf("IPC: already answered\n");
            fuse_lck_mtx_unlock(fdip->tick->tk_aw_mtx);
            goto out;
        } else {
            debug_printf("IPC: setting to answered\n");
            age = fdip->tick->tk_age;
            fticket_set_answered(fdip->tick);
            fuse_lck_mtx_unlock(fdip->tick->tk_aw_mtx);
#ifndef DONT_TRY_HARD_PREVENT_IO_IN_VAIN
            fuse_lck_mtx_lock(fdip->tick->tk_data->aw_mtx);
            TAILQ_FOREACH(tick, &fdip->tick->tk_data->aw_head, tk_aw_link) {
                if (tick == fdip->tick) {
                    if (fdip->tick->tk_age == age) {
                        debug_printf("IPC: preventing io in vain succeeded\n");
                        fdip->tick->tk_aw_handler = NULL;
                    }
                    break;
                }
            }

            fuse_lck_mtx_unlock(fdip->tick->tk_data->aw_mtx);
#endif
            return (err);
        }
    }

    debug_printf("IPC: not interrupted, err = %d\n", err);

    if (fdip->tick->tk_aw_errno) {
        debug_printf("IPC: explicit EIO-ing, tk_aw_errno = %d\n",
                      fdip->tick->tk_aw_errno);
        err = EIO;
        goto out;
    }

    if ((err = fdip->tick->tk_aw_ohead.error)) {
        debug_printf("IPC: setting status to %d\n",
                     fdip->tick->tk_aw_ohead.error);
        fdip->answ_stat = err;
        goto out;
    }

    fdip->answ = fticket_resp(fdip->tick)->base;
    fdip->iosize = fticket_resp(fdip->tick)->len;

    debug_printf("IPC: all is well\n");

    return (0);

out:
    debug_printf("IPC: dropping ticket, err = %d\n", err);
    fuse_ticket_drop(fdip->tick);

    return (err);
}
