/*
 * Copyright (C) 2006 Google. All Rights Reserved.
 * Amit Singh <singh@>
 */

#include <sys/systm.h>
#include <sys/types.h>
#include <sys/sysctl.h>

#include "fuse_device.h"
#include "fuse_kernel.h"
#include "fuse_sysctl.h"
#include <fuse_param.h>
#include <fuse_version.h>

/* NB: none of these are bigger than unsigned 32-bit. */

int32_t  fuse_admin_group            = 0;                                  // rw
uint32_t fuse_api_major              = FUSE_KERNEL_VERSION;                // r
uint32_t fuse_api_minor              = FUSE_KERNEL_MINOR_VERSION;          // r
int32_t  fuse_dev_use_count          = 0;                                  // r
int32_t  fuse_fh_current             = 0;                                  // r
uint32_t fuse_fh_reuse_count         = 0;                                  // r
uint32_t fuse_fh_upcall_count        = 0;                                  // r
int32_t  fuse_iov_credit             = FUSE_DEFAULT_IOV_CREDIT;            // rw
int32_t  fuse_iov_current            = 0;                                  // r
uint32_t fuse_iov_permanent_bufsize  = FUSE_DEFAULT_IOV_PERMANENT_BUFSIZE; // rw
int32_t  fuse_kill_fs                = -1;                                 // w
uint32_t fuse_lookup_cache_hits      = 0;                                  // r
uint32_t fuse_lookup_cache_misses    = 0;                                  // r
uint32_t fuse_lookup_cache_overrides = 0;                                  // r
uint32_t fuse_max_freetickets        = FUSE_DEFAULT_MAX_FREE_TICKETS;      // rw
int32_t  fuse_memory_allocated       = 0;                                  // r
int32_t  fuse_realloc_count          = 0;                                  // r
int32_t  fuse_tickets_current        = 0;                                  // r
int32_t  fuse_vnodes_current         = 0;                                  // r

SYSCTL_DECL(_macfuse);
SYSCTL_NODE(, OID_AUTO, macfuse, CTLFLAG_RW, 0, "MacFUSE Statistics");
SYSCTL_NODE(_macfuse, OID_AUTO, control, CTLFLAG_RW, 0, "MacFUSE Controls");
SYSCTL_NODE(_macfuse, OID_AUTO, counters, CTLFLAG_RW, 0,
            "MacFUSE Statistics: Monotonic Counters");
SYSCTL_NODE(_macfuse, OID_AUTO, resourceusage, CTLFLAG_RW, 0,
            "MacFUSE Statistics: Resource Usage");
SYSCTL_NODE(_macfuse, OID_AUTO, tunables, CTLFLAG_RW, 0,
            "MacFUSE Statistics: Tunables");
SYSCTL_NODE(_macfuse, OID_AUTO, version, CTLFLAG_RW, 0,
            "MacFUSE Statistics: Version Information");

/* fuse.control */

int sysctl_macfuse_control_kill_fs_handler SYSCTL_HANDLER_ARGS;

int
sysctl_macfuse_control_kill_fs_handler SYSCTL_HANDLER_ARGS
{
    int error = 0;
    (void)oidp;

    if (arg1) {
        error = SYSCTL_OUT(req, arg1, sizeof(int));
    } else {
        error = SYSCTL_OUT(req, &arg2, sizeof(int));
    }

    if (error || !req->newptr) {
        return error;
    }

    if (!arg1) {
        error = EPERM;
    } else {
        error = SYSCTL_IN(req, arg1, sizeof(int));
        if (error == 0) {
            error = fuse_devices_kill_unit(*(int *)arg1);
        }
        fuse_kill_fs = -1; /* set it back */
    }

    return error;
}

SYSCTL_PROC(_macfuse_control,                // our parent
            OID_AUTO,                        // automatically assign object ID
            kill_fs,                         // our name
            (CTLTYPE_INT | CTLFLAG_WR),      // type flag/access flag
            &fuse_kill_fs,                   // location of our data
            0,                               // argument passed to our handler
            sysctl_macfuse_control_kill_fs_handler, // our handler function
            "I",                             // our data type (integer)
            "MacFUSE Controls: Kill the Given File System");

/* fuse.counters */
SYSCTL_INT(_macfuse_counters, OID_AUTO, device_use, CTLFLAG_RD,
           &fuse_dev_use_count, 0, "");
SYSCTL_INT(_macfuse_counters, OID_AUTO, filehandle_reuse, CTLFLAG_RD,
           &fuse_fh_reuse_count, 0, "");
SYSCTL_INT(_macfuse_counters, OID_AUTO, filehandle_upcalls, CTLFLAG_RD,
           &fuse_fh_upcall_count, 0, "");
SYSCTL_INT(_macfuse_counters, OID_AUTO, lookup_cache_hits, CTLFLAG_RD,
           &fuse_lookup_cache_hits, 0, "");
SYSCTL_INT(_macfuse_counters, OID_AUTO, lookup_cache_misses, CTLFLAG_RD,
           &fuse_lookup_cache_misses, 0, "");
SYSCTL_INT(_macfuse_counters, OID_AUTO, lookup_cache_overrides,
           CTLFLAG_RD, &fuse_lookup_cache_overrides, 0, "");
SYSCTL_INT(_macfuse_counters, OID_AUTO, memory_reallocs, CTLFLAG_RD,
           &fuse_realloc_count, 0, "");

/* fuse.resourceusage */
SYSCTL_INT(_macfuse_resourceusage, OID_AUTO, filehandles, CTLFLAG_RD,
           &fuse_fh_current, 0, "");
SYSCTL_INT(_macfuse_resourceusage, OID_AUTO, ipc_iovs, CTLFLAG_RD,
           &fuse_iov_current, 0, "");
SYSCTL_INT(_macfuse_resourceusage, OID_AUTO, ipc_tickets, CTLFLAG_RD,
           &fuse_tickets_current, 0, "");
SYSCTL_INT(_macfuse_resourceusage, OID_AUTO, memory_bytes, CTLFLAG_RD,
           &fuse_memory_allocated, 0, "");
SYSCTL_INT(_macfuse_resourceusage, OID_AUTO, vnodes, CTLFLAG_RD,
           &fuse_vnodes_current, 0, "");

/* fuse.tunables */
SYSCTL_INT(_macfuse_tunables, OID_AUTO, admin_group, CTLFLAG_RW,
           &fuse_admin_group, 0, "");
SYSCTL_INT(_macfuse_tunables, OID_AUTO, iov_credit, CTLFLAG_RW,
           &fuse_iov_credit, 0, "");
SYSCTL_INT(_macfuse_tunables, OID_AUTO, iov_permanent_bufsize, CTLFLAG_RW,
           &fuse_iov_permanent_bufsize, 0, "");
SYSCTL_INT(_macfuse_tunables, OID_AUTO, max_freetickets, CTLFLAG_RW,
           &fuse_max_freetickets, 0, "");

/* fuse.version */
SYSCTL_INT(_macfuse_version, OID_AUTO, api_major, CTLFLAG_RD,
           &fuse_api_major, 0, "");
SYSCTL_INT(_macfuse_version, OID_AUTO, api_minor, CTLFLAG_RD,
           &fuse_api_minor, 0, "");
SYSCTL_STRING(_macfuse_version, OID_AUTO, string, CTLFLAG_RD,
              MACFUSE_VERSION ", " MACFUSE_TIMESTAMP, 0, "");

static struct sysctl_oid *fuse_sysctl_list[] =
{
    &sysctl__macfuse_control,
    &sysctl__macfuse_counters,
    &sysctl__macfuse_resourceusage,
    &sysctl__macfuse_tunables,
    &sysctl__macfuse_version,
    &sysctl__macfuse_control_kill_fs,
    &sysctl__macfuse_counters_device_use,
    &sysctl__macfuse_counters_filehandle_reuse,
    &sysctl__macfuse_counters_filehandle_upcalls,
    &sysctl__macfuse_counters_lookup_cache_hits,
    &sysctl__macfuse_counters_lookup_cache_misses,
    &sysctl__macfuse_counters_lookup_cache_overrides,
    &sysctl__macfuse_counters_memory_reallocs,
    &sysctl__macfuse_resourceusage_filehandles,
    &sysctl__macfuse_resourceusage_ipc_iovs,
    &sysctl__macfuse_resourceusage_ipc_tickets,
    &sysctl__macfuse_resourceusage_memory_bytes,
    &sysctl__macfuse_resourceusage_vnodes,
    &sysctl__macfuse_tunables_admin_group,
    &sysctl__macfuse_tunables_iov_credit,
    &sysctl__macfuse_tunables_iov_permanent_bufsize,
    &sysctl__macfuse_tunables_max_freetickets,
    &sysctl__macfuse_version_api_major,
    &sysctl__macfuse_version_api_minor,
    &sysctl__macfuse_version_string,
    (struct sysctl_oid *)0
};

void
fuse_sysctl_start(void)
{
    int i;

    sysctl_register_oid(&sysctl__macfuse);
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
    sysctl_unregister_oid(&sysctl__macfuse);
}
