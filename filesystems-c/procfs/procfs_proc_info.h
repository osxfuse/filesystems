/*
 * procfs as a MacFUSE file system for Mac OS X
 *
 * Copyright Amit Singh. All Rights Reserved.
 * http://osxbook.com
 *
 * http://code.google.com/p/macfuse/
 *
 * Source License: GNU GENERAL PUBLIC LICENSE (GPL)
 */

#ifndef _LOCAL_SYS_PROC_INFO_H
#define _LOCAL_SYS_PROC_INFO_H

extern "C" {

__BEGIN_DECLS

#include <sys/proc_info.h>

extern int procfs_proc_pidinfo(pid_t pid, char *buf, int *len);

__END_DECLS

}

#endif /*_LOCAL_SYS_PROC_INFO_H */
