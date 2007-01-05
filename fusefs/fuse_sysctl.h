/*
 * Copyright (C) 2006 Google. All Rights Reserved.
 * Amit Singh <singh@>
 */

#ifndef _FUSE_SYSCTL_H_
#define _FUSE_SYSCTL_H_

extern int32_t fuse_max_freetickets;
extern int32_t fuse_iov_credit;
extern int32_t fuse_iov_permanent_bufsize;

extern uint64_t fuse_vnodes;
extern uint64_t fuse_lookup_cache_hits;
extern uint64_t fuse_lookup_cache_misses;
extern uint64_t fuse_fh_current;
extern uint64_t fuse_fh_upcall_count;
extern uint64_t fuse_fh_reuse_count;

void fuse_sysctl_start(void);
void fuse_sysctl_stop(void);

#endif /* _FUSE_SYSCTL_H_ */
