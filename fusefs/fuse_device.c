/*
 * Copyright (C) 2006 Google. All Rights Reserved.
 * Amit Singh <singh@>
 */

#include "fuse.h"
#include "fuse_ipc.h"
#include "fuse_device.h"
#include "fuse_locking.h"

#define TAILQ_FOREACH_SAFE(var, head, field, tvar)           \
        for ((var) = TAILQ_FIRST((head));                    \
            (var) && ((tvar) = TAILQ_NEXT((var), field), 1); \
            (var) = (tvar))

static int fuse_global_usecount = 0;
static int fuse_cdev_major      = -1;

struct fuse_softc {
    int    usecount;
    pid_t  pid;
    void  *cdev;
    void  *data;
};

struct fuse_softc fuse_softc_table[FUSE_NDEVICES];

#define FUSE_SOFTC_FROM_UNIT_FAST(u) (fuse_softc_t)&(fuse_softc_table[(u)])

fuse_softc_t
fuse_softc_get(dev_t dev)
{
    int unit = minor(dev);

    if ((unit < 0) || (unit >= FUSE_NDEVICES)) {
        return (fuse_softc_t)0;
    }

    return FUSE_SOFTC_FROM_UNIT_FAST(unit);
}

__inline__
struct fuse_data *
fuse_softc_get_data(fuse_softc_t fdev)
{
    if (fdev) {
       return fdev->data;
    }

    return NULL;
}

__inline__
void
fuse_softc_set_data(fuse_softc_t fdev, struct fuse_data *data)
{
    if (fdev) {
       return;
    }

    fdev->data = data;
}

int
fuse_device_enodev(void)
{
    return ENODEV;
}

d_open_t  fuse_device_open;
d_close_t fuse_device_close;
d_read_t  fuse_device_read;
d_write_t fuse_device_write;

static struct cdevsw fuse_device_cdevsw = {
    /* open     */ fuse_device_open,
    /* close    */ fuse_device_close,
    /* read     */ fuse_device_read,
    /* write    */ fuse_device_write,
    /* ioctl    */ (d_ioctl_t *)&fuse_device_enodev,
    /* stop     */ (d_stop_t *)&fuse_device_enodev,
    /* reset    */ (d_reset_t *)&fuse_device_enodev,
    /* ttys     */ 0,
    /* select   */ (d_select_t *)&fuse_device_enodev,
    /* mmap     */ (d_mmap_t *)&fuse_device_enodev,
    /* strategy */ (d_strategy_t *)&fuse_device_enodev,
    /* getc     */ (d_getc_t *)&fuse_device_enodev,
    /* putc     */ (d_putc_t *)&fuse_device_enodev,
    /* flags    */ D_TTY,
};

int
fuse_device_open(dev_t dev, int flags, int devtype, struct proc *p)
{
    int unit;
    struct fuse_softc *fdev;
    struct fuse_data  *fdata;

    fuse_trace_printf_func();

    if (fuse_global_usecount < 0) {
        return ENOENT;
    }

    fuse_global_usecount++;

    unit = minor(dev);
    if (unit >= FUSE_NDEVICES) {
        return ENOENT;
    }

    fdev = FUSE_SOFTC_FROM_UNIT_FAST(unit);
    if (!fdev) {
        fuse_global_usecount--;
        return ENXIO;
    }

    if (fdev->usecount > 0) {
        fuse_global_usecount--;
        return EBUSY;
    }

    fdev->usecount++;

    fdata = fdata_alloc(fdev, p);

    FUSE_LOCK();

    if (fdev->data) {
        FUSE_UNLOCK();
        fdata_destroy(fdata);
        fuse_global_usecount--;
        fdev->usecount--;
        return EBUSY;
    } else {
        fdata->dataflag |= FSESS_OPENED;
        fdev->data = fdata;
        fdev->pid = proc_pid(p);
    }       

    FUSE_UNLOCK();

    return KERN_SUCCESS;
}

int
fuse_device_close(dev_t dev, int flags, int devtype, struct proc *p)
{
    int unit, skip_destroy = 0;
    struct fuse_softc *fdev;
    struct fuse_data  *data;

    fuse_trace_printf_func();

    unit = minor(dev);
    if (unit >= FUSE_NDEVICES) {
        return ENOENT;
    }

    fdev = FUSE_SOFTC_FROM_UNIT_FAST(unit);
    if (!fdev) {
        return ENXIO;
    }

    FUSE_LOCK();

    data = fdev->data;
    if (!data) {
        panic("FUSE: no softc data upon device close");
    }

    fdata_kick_set(data);
    data->dataflag &= ~FSESS_OPENED;

    lck_mtx_lock(data->aw_mtx);

    if (data->mntco > 0) {
        struct fuse_ticket *tick;

        while ((tick = fuse_aw_pop(data))) {
            lck_mtx_lock(tick->tk_aw_mtx);
            fticket_set_answered(tick);
            tick->tk_aw_errno = ENOTCONN;
            wakeup(tick);
            lck_mtx_unlock(tick->tk_aw_mtx);
        }

        lck_mtx_unlock(data->aw_mtx);

        skip_destroy = 1;
    }

    fdev->data = NULL;
    fdev->pid = -1;
    fdev->usecount--;
    FUSE_UNLOCK();

    if (!skip_destroy) {
        fdata_destroy(data);
    }

out:
    fuse_global_usecount--;

    return KERN_SUCCESS;
}

int
fuse_device_read(dev_t dev, uio_t uio, int ioflag)
{
    int i, buflen[3], err = 0;
    void *buf[] = { NULL, NULL, NULL };
    struct fuse_softc  *fdev;
    struct fuse_data   *data;
    struct fuse_ticket *tick;

    fuse_trace_printf_func();

    fdev = FUSE_SOFTC_FROM_UNIT_FAST(minor(dev));
    if (!fdev) {
        return ENXIO;
    }

    data = fdev->data;

    lck_mtx_lock(data->ms_mtx);

    // The read loop (upgoing messages to the user daemon).
again:
    if (fdata_kick_get(data)) {
        lck_mtx_unlock(data->ms_mtx);
        return ENODEV;
    }

    if (!(tick = fuse_ms_pop(data))) {
        err = msleep(data, data->ms_mtx, PCATCH, "fu_msg", 0);
        if (err != 0) {
            lck_mtx_unlock(data->ms_mtx);
            return (fdata_kick_get(data) ? ENODEV : err);
        }
        tick = fuse_ms_pop(data);
    }

    if (!tick) {
        goto again;
    }

    lck_mtx_unlock(data->ms_mtx);

    if (fdata_kick_get(data)) {
         if (tick) {
             fuse_ticket_drop_invalid(tick);
         }
         return ENODEV;
    }

    switch (tick->tk_ms_type) {

    case FT_M_FIOV:
        buf[0] = tick->tk_ms_fiov.base;
        buflen[0] =  tick->tk_ms_fiov.len;
        break;

    case FT_M_BUF:
        buf[0] = tick->tk_ms_fiov.base;
        buflen[0] =  tick->tk_ms_fiov.len;
        buf[1] = tick->tk_ms_bufdata;
        buflen[1] =  tick->tk_ms_bufsize;
        break;

    default:
        panic("FUSE: unknown message type for ticket %p", tick);
    }

    for (i = 0; buf[i]; i++) {
        if (uio_resid(uio) < buflen[i]) {
            data->dataflag |= FSESS_KICK;
            err = ENODEV;
            break;
        }
        err = uiomove(buf[i], buflen[i], uio);
        if (err) {
            break;
        }
    }

    // The 'FORGET' message is an example of a ticket that has explicitly
    // been invalidated by the sender. The sender is not expecting or wanting
    // a reply, so he sets the FT_INVALID bit in the ticket.
    fuse_ticket_drop_invalid(tick);

    return (err);
}

static __inline__ int
fuse_ohead_audit(struct fuse_out_header *ohead, uio_t uio)
{
    if (uio_resid(uio) + sizeof(struct fuse_out_header) != ohead->len) {
        debug_printf("format error: body size differs from that in header\n");
        return (EINVAL); 
    }   
    
    if (uio_resid(uio) && ohead->error) {
        debug_printf("format error: non-zero error for message with body\n");
        return (EINVAL);
    }

    ohead->error = -(ohead->error);

    return (0);
}   

int
fuse_device_write(dev_t dev, uio_t uio, int ioflag)
{
    int err = 0, found = 0;
    struct fuse_out_header ohead;
    struct fuse_softc  *fdev;
    struct fuse_data   *data;
    struct fuse_ticket *tick, *x_tick;

    fuse_trace_printf_func();

    fdev = FUSE_SOFTC_FROM_UNIT_FAST(minor(dev));
    if (!fdev) {
        return ENXIO;
    }

    if (uio_resid(uio) < sizeof(struct fuse_out_header)) {
        debug_printf("returning EINVAL (header size issue)\n");
        return (EINVAL);
    }

    if ((err = uiomove((caddr_t)&ohead, sizeof(struct fuse_out_header), uio))
        != 0) {
        debug_printf("returning %d (uiomove issue)\n", err);
        return (err);
    }

    if ((err = fuse_ohead_audit(&ohead, uio))) {
        debug_printf("returning %d (audit failed)\n", err);
        return (err);
    }

    data = fdev->data;

    lck_mtx_lock(data->aw_mtx);

    TAILQ_FOREACH_SAFE(tick, &data->aw_head, tk_aw_link, x_tick) {
        if (tick->tk_unique == ohead.unique) {
            found = 1;
            fuse_aw_remove(tick);
            break;
        }
    }

    lck_mtx_unlock(data->aw_mtx);

    if (found) {
        if (tick->tk_aw_handler) {
            memcpy(&tick->tk_aw_ohead, &ohead, sizeof(ohead));
            err = tick->tk_aw_handler(tick, uio);
        } else {
            fuse_ticket_drop(tick);
            return (err);
        }
    } else {
        debug_printf("no handler for this response\n");
    }

    return (err);
}

int
fuse_devices_start(void)
{
    int i;

    if ((fuse_cdev_major = cdevsw_add(-1, &fuse_device_cdevsw)) == -1) {
        goto error;
    }

    for (i = 0; i < FUSE_NDEVICES; i++) {
        dev_t dev = makedev(fuse_cdev_major, i);
        fuse_softc_table[i].cdev = devfs_make_node(dev,
                                                   DEVFS_CHAR,
                                                   UID_ROOT,
                                                   GID_OPERATOR,
                                                   0666,
                                                   "fuse%d",
                                                   i);
        if (fuse_softc_table[i].cdev == NULL) {
            goto error;
        }

        fuse_softc_table[i].data = NULL;
        fuse_softc_table[i].usecount = 0;
    }

    return KERN_SUCCESS;

error:
    for (--i; i >= 0; i--) {
        devfs_remove(fuse_softc_table[i].cdev);
        fuse_softc_table[i].cdev = NULL;
    }

    (void)cdevsw_remove(fuse_cdev_major, &fuse_device_cdevsw);
    fuse_cdev_major = -1;

    return KERN_FAILURE;
}

int
fuse_devices_stop(void)
{
    int i, ret;

    if (fuse_cdev_major == -1) {
        return KERN_SUCCESS;
    }

    for (i = 0; i < FUSE_NDEVICES; i++) {
        if ((fuse_softc_table[i].data != NULL) ||
            (fuse_softc_table[i].usecount != 0)) {
            debug_printf("/dev/fuse%d seems to be still active\n", i);
            return KERN_FAILURE;
        }
        devfs_remove(fuse_softc_table[i].cdev);
        fuse_softc_table[i].cdev = NULL;
    }

    ret = cdevsw_remove(fuse_cdev_major, &fuse_device_cdevsw);
    if (ret != fuse_cdev_major) {
        debug_printf("fuse_cdev_major != return value from cdevsw_remove()\n");
    }

    fuse_cdev_major = -1;

    return KERN_SUCCESS;
}
