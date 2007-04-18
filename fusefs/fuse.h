/*
 * Copyright (C) 2006 Google. All Rights Reserved.
 * Amit Singh <singh@>
 */

#ifndef _FUSE_H_
#define _FUSE_H_

#include <sys/param.h>
#include <sys/types.h>
#include <sys/event.h>
#include <libkern/OSMalloc.h>
#include <libkern/locks.h>
#include <IOKit/IOLib.h>

#ifndef _FUSE_KERNEL_H_
#define _FUSE_KERNEL_H_
#include "fuse_kernel.h"
#endif

#include <fuse_param.h>
#include <fuse_sysctl.h>
#include <fuse_version.h>

//#define FUSE_DEBUG         1
//#define FUSE_KDEBUG        1
//#define FUSE_KTRACE_OP     1
//#define FUSE_TRACE         1
//#define FUSE_TRACE_LK      1
//#define FUSE_TRACE_OP      1
//#define FUSE_TRACE_VNCACHE 1
#define FUSE_TRACK_STATS 1

#define FUSEFS_SIGNATURE 0x55464553 // 'FUSE'

#ifdef FUSE_TRACE
#define fuse_trace_printf(fmt, ...) IOLog(fmt, ## __VA_ARGS__)
#define fuse_trace_printf_func()    IOLog("%s\n", __FUNCTION__)
#else
#define fuse_trace_printf(fmt, ...) {}
#define fuse_trace_printf_func()    {}
#endif

#ifdef FUSE_TRACE_OP
#define fuse_trace_printf_vfsop() IOLog("%s\n", __FUNCTION__)
#define fuse_trace_printf_vnop()  IOLog("%s\n", __FUNCTION__)
#else
#define fuse_trace_printf_vfsop() {}
#define fuse_trace_printf_vnop()  {}
#endif

#ifdef FUSE_KTRACE_OP
#undef  fuse_trace_printf_vfsop
#undef  fuse_trace_printf_vnop
#define fuse_trace_printf_vfsop() kprintf("%s\n", __FUNCTION__)
#define fuse_trace_printf_vnop()  kprintf("%s\n", __FUNCTION__)
#endif

#ifdef FUSE_DEBUG
#define debug_printf(fmt, ...) \
  IOLog("%s[%s:%d]: " fmt, __FUNCTION__, __FILE__, __LINE__, ## __VA_ARGS__)
#else
#define debug_printf(fmt, ...) {}
#endif

#ifdef FUSE_KDEBUG
#undef debug_printf
#define debug_printf(fmt, ...) \
  IOLog("%s[%s:%d]: " fmt, __FUNCTION__, __FILE__, __LINE__, ## __VA_ARGS__);\
  kprintf("%s[%s:%d]: " fmt, __FUNCTION__, __FILE__, __LINE__, ## __VA_ARGS__)
#define kdebug_printf(fmt, ...) debug_printf(fmt, ## __VA_ARGS__)
#else
#define kdebug_printf(fmt, ...) {}
#endif

#define FUSE_ASSERT(a)                                                    \
    {                                                                     \
        if (!(a)) {                                                       \
            IOLog("File "__FILE__", line %d: assertion ' %s ' failed.\n", \
                  __LINE__, #a);                                          \
        }                                                                 \
    }

#define E_NONE 0

#define FUSE_ZERO_SIZE 0x0000000000000000ULL
#define FUSE_ROOT_SIZE 0xFFFFFFFFFFFFFFFFULL

extern OSMallocTag fuse_malloc_tag;

#ifdef FUSE_TRACK_STATS

#define FUSE_OSAddAtomic(amount, value) OSAddAtomic((amount), (value))

extern int32_t fuse_memory_allocated;

static __inline__
void *
FUSE_OSMalloc(uint32_t size, OSMallocTag tag)
{
    void *addr = OSMalloc(size, tag);

    if (!addr) {
        panic("MacFUSE: memory allocation failed (size=%d)", size);
    }

    FUSE_OSAddAtomic(size, (SInt32 *)&fuse_memory_allocated);
    
    return addr;
}

static __inline__
void
FUSE_OSFree(void *addr, uint32_t size, OSMallocTag tag)
{
    OSFree(addr, size, tag);

    FUSE_OSAddAtomic(-(size), (SInt32 *)&fuse_memory_allocated);
}
#else

#define FUSE_OSAddAtomic(amount, value) {}
#define FUSE_OSMalloc(size, tag)        OSMalloc((size), (tag))
#define FUSE_OSFree(addr, size, tag)    OSFree((addr), (size), (tag))

#endif /* FUSE_TRACK_STATISTICS */

static __inline__
void *
FUSE_OSRealloc(void *oldptr, int oldsize, int newsize)
{   
    void *data;
    
    data = FUSE_OSMalloc(newsize, fuse_malloc_tag);
    if (!data) {
        panic("MacFUSE: OSMalloc failed in realloc");
    }
    
    bcopy(oldptr, data, oldsize);
    FUSE_OSFree(oldptr, oldsize, fuse_malloc_tag);
    
    FUSE_OSAddAtomic(1, (SInt32 *)&fuse_realloc_count);
    
    return (data);
}

#endif /* _FUSE_H_ */
