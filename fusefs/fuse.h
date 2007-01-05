/*
 * Copyright (C) 2006 Google. All Rights Reserved.
 * Amit Singh <singh@>
 */

#ifndef _FUSE_H_
#define _FUSE_H_

#include <sys/param.h>
#include <sys/types.h>
#include <libkern/OSMalloc.h>
#include <libkern/locks.h>
#include <IOKit/IOLib.h>

#ifndef _FUSE_KERNEL_H_
#define _FUSE_KERNEL_H_
#include "fuse_kernel.h"
#endif

#include <fuse_param.h>
#include <fuse_version.h>

//#define FUSE_DEBUG  1
//#define FUSE_KDEBUG 1
//#define FUSE_TRACE  1
//#define FUSE_TRACE_OP 1

#define FUSEFS_SIGNATURE 0x55464553 // 'FUSE'

#ifdef FUSE_TRACE
#define fuse_trace_printf(fmt, ...) printf(fmt, ## __VA_ARGS__)
#define fuse_trace_printf_func()    printf("%s\n", __FUNCTION__)
#else
#define fuse_trace_printf(fmt, ...) {}
#define fuse_trace_printf_func()    {}
#endif

#ifdef FUSE_TRACE_OP
#define fuse_trace_printf_vfsop() printf("%s\n", __FUNCTION__)
#define fuse_trace_printf_vnop()  printf("%s\n", __FUNCTION__)
#else
#define fuse_trace_printf_vfsop() {}
#define fuse_trace_printf_vnop()  {}
#endif

#ifdef FUSE_DEBUG
#define debug_printf(fmt, ...) \
  printf("%s[%s:%d]: " fmt, __FUNCTION__, __FILE__, __LINE__, ## __VA_ARGS__)
#else
#define debug_printf(fmt, ...) {}
#endif

#ifdef FUSE_KDEBUG
#undef debug_printf
#define debug_printf(fmt, ...) \
  printf("%s[%s:%d]: " fmt, __FUNCTION__, __FILE__, __LINE__, ## __VA_ARGS__);\
  kprintf("%s[%s:%d]: " fmt, __FUNCTION__, __FILE__, __LINE__, ## __VA_ARGS__)
#define kdebug_printf(fmt, ...) debug_printf(fmt, ## __VA_ARGS__)
#else
#define kdebug_printf(fmt, ...) {}
#endif

#define FUSE_ASSERT(a)                                                     \
    {                                                                      \
        if (!(a)) {                                                        \
            printf("File "__FILE__", line %d: assertion ' %s ' failed.\n", \
                  __LINE__, #a);                                           \
        }                                                                  \
    }

#define E_NONE 0

#endif /* _FUSE_H_ */
