/*
 * Copyright (C) 2006 Google. All Rights Reserved.
 * Amit Singh <singh@>
 */

#ifndef _FUSE_SYSCTL_H_
#define _FUSE_SYSCTL_H_

extern int32_t  fuse_admin_group;
extern int32_t  fuse_dev_use_count;
extern int32_t  fuse_fh_current;
extern uint32_t fuse_fh_reuse_count;
extern uint32_t fuse_fh_upcall_count;
extern int32_t  fuse_iov_credit;
extern int32_t  fuse_iov_current;
extern uint32_t fuse_iov_permanent_bufsize;
extern uint32_t fuse_lookup_cache_hits;
extern uint32_t fuse_lookup_cache_misses;
extern uint32_t fuse_lookup_cache_overrides;
extern uint32_t fuse_max_freetickets;
extern int32_t  fuse_memory_allocated;
extern int32_t  fuse_realloc_count;
extern int32_t  fuse_tickets_current;
extern int32_t  fuse_vnodes_current;

extern void fuse_sysctl_start(void);
extern void fuse_sysctl_stop(void);

#endif /* _FUSE_SYSCTL_H_ */
