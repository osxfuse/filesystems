/*
 * Copyright (C) 2006 Google. All Rights Reserved.
 * Amit Singh <singh@>
 */

#include <sys/systm.h>
#include <sys/types.h>
#include <sys/sysctl.h>

#include "fuse_kernel.h"
#include <fuse_param.h>
#include <fuse_version.h>

int32_t  fuse_api_major             = FUSE_KERNEL_VERSION;                // r
int32_t  fuse_api_minor             = FUSE_KERNEL_MINOR_VERSION;          // r
int32_t  fuse_max_freetickets       = FUSE_DEFAULT_MAX_FREE_TICKETS;      // rw
int32_t  fuse_iov_credit            = FUSE_DEFAULT_IOV_CREDIT;            // rw
int32_t  fuse_iov_permanent_bufsize = FUSE_DEFAULT_IOV_PERMANENT_BUFSIZE; // rw
uint64_t fuse_vnodes               = 0;                                   // r
uint64_t fuse_lookup_cache_hits    = 0;                                   // r
uint64_t fuse_lookup_cache_misses  = 0;                                   // r
uint64_t fuse_fh_current           = 0;                                   // r
uint64_t fuse_fh_upcall_count      = 0;                                   // r
uint64_t fuse_fh_reuse_count       = 0;                                   // r

SYSCTL_DECL(_fuse);
SYSCTL_NODE(, OID_AUTO, fuse, CTLFLAG_RW, 0, "");
SYSCTL_STRING(_fuse, OID_AUTO, version, CTLFLAG_RD, MACFUSE_VERSION, 0, "");
SYSCTL_INT(_fuse, OID_AUTO, api_major, CTLFLAG_RD, &fuse_api_major, 0, "");
SYSCTL_INT(_fuse, OID_AUTO, api_minor, CTLFLAG_RD, &fuse_api_minor, 0, "");
SYSCTL_INT(_fuse, OID_AUTO, max_freetickets, CTLFLAG_RW,
           &fuse_max_freetickets, 0, "");
SYSCTL_INT(_fuse, OID_AUTO, iov_credit, CTLFLAG_RW,
           &fuse_iov_credit, 0, "");
SYSCTL_INT(_fuse, OID_AUTO, iov_permanent_bufsize, CTLFLAG_RW,
           &fuse_iov_permanent_bufsize, 0, "");
SYSCTL_QUAD(_fuse, OID_AUTO, vnodes, CTLFLAG_RD, &fuse_vnodes, "");
SYSCTL_QUAD(_fuse, OID_AUTO, lookup_cache_hits, CTLFLAG_RD,
            &fuse_lookup_cache_hits, "");
SYSCTL_QUAD(_fuse, OID_AUTO, lookup_cache_misses, CTLFLAG_RD,
            &fuse_lookup_cache_misses, "");
SYSCTL_QUAD(_fuse, OID_AUTO, fh_current, CTLFLAG_RD, &fuse_fh_current, "");
SYSCTL_QUAD(_fuse, OID_AUTO, fh_upcall_count, CTLFLAG_RD,
            &fuse_fh_upcall_count, "");
SYSCTL_QUAD(_fuse, OID_AUTO, fh_reuse_count, CTLFLAG_RD,
            &fuse_fh_reuse_count, "");

static struct sysctl_oid *fuse_sysctl_list[] =
{
    &sysctl__fuse_version,
    &sysctl__fuse_api_major,
    &sysctl__fuse_api_minor,
    &sysctl__fuse_max_freetickets,
    &sysctl__fuse_iov_credit,
    &sysctl__fuse_iov_permanent_bufsize,
    &sysctl__fuse_vnodes,
    &sysctl__fuse_lookup_cache_hits,
    &sysctl__fuse_lookup_cache_misses,
    &sysctl__fuse_fh_current,
    &sysctl__fuse_fh_upcall_count,
    &sysctl__fuse_fh_reuse_count,
    (struct sysctl_oid *) 0
};

void
fuse_sysctl_start(void)
{
    int i;

    sysctl_register_oid(&sysctl__fuse);
    for (i = 0; fuse_sysctl_list[i]; i++) {
       sysctl_register_oid(fuse_sysctl_list[i]);
    }
}

void
fuse_sysctl_stop(void)
{
    int i;

    for (i = 0; fuse_sysctl_list[i]; i++) {
       sysctl_unregister_oid(fuse_sysctl_list[i]);
    }
    sysctl_unregister_oid(&sysctl__fuse);
}
