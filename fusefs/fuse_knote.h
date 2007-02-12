/*
 * Copyright (C) 2006 Google. All Rights Reserved.
 * Amit Singh <singh@>
 */

#ifndef _FUSE_KNOTE_H_
#define _FUSE_KNOTE_H_

#include <fuse_param.h>

#if MACFUSE_ENABLE_UNSUPPORTED

#include <sys/event.h>
SLIST_HEAD(klist, knote);

struct filterops {
    int  f_isfd; /* true if ident == filedescriptor */
    int  (*f_attach)(struct knote *kn);
    void (*f_detach)(struct knote *kn);
    int  (*f_event)(struct knote *kn, long hint);
};

TAILQ_HEAD(kqtailq, knote); /* a list of "queued" events */

struct knote {
    int                  kn_inuse;       /* inuse count */
    struct kqtailq      *kn_tq;          /* pointer to tail queue */
    TAILQ_ENTRY(knote)   kn_tqe;         /* linkage for tail queue */
    struct kqueue       *kn_kq;          /* which kqueue we are on */
    SLIST_ENTRY(knote)   kn_link;        /* linkage for search list */
    SLIST_ENTRY(knote)   kn_selnext;     /* klist element chain */
    union {
        struct fileproc *p_fp;           /* file data pointer */
        struct proc     *p_proc;         /* proc pointer */
    } kn_ptr;
    struct filterops    *kn_fop;
    int                  kn_status;      /* status bits */
    int                  kn_sfflags;     /* saved filter flags */
    struct               kevent kn_kevent;
    caddr_t              kn_hook;
    int                  kn_hookid;
    int64_t              kn_sdata;       /* saved data field */

#define kn_id     kn_kevent.ident
#define kn_filter kn_kevent.filter
#define kn_flags  kn_kevent.flags
#define kn_fflags kn_kevent.fflags
#define kn_data   kn_kevent.data
#define kn_fp     kn_ptr.p_fp

};

void filt_fusedetach(struct knote *kn);
int  filt_fuseread(struct knote *kn, long hint);
int  filt_fusewrite(struct knote *kn, long hint);
int  filt_fusevnode(struct knote *kn, long hint);

extern struct filterops fuseread_filtops;
extern struct filterops fusewrite_filtops;
extern struct filterops fusevnode_filtops;

#define KNOTE(list, hint)       knote(list, hint)
#define KNOTE_ATTACH(list, kn)  knote_attach(list, kn)
#define KNOTE_DETACH(list, kn)  knote_detach(list, kn)

#define FUSE_KNOTE(vp, hint)    KNOTE(&VTOFUD(vp)->c_knotes, (hint))

#else
#define FUSE_KNOTE(vp, hint)    {}
#endif /* MACFUSE_ENABLE_UNSUPPORTED */

#endif /* _FUSE_NODE_H_ */
