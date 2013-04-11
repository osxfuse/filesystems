/*
 * procfs as a OSXFUSE file system for Mac OS X
 *
 * Copyright Amit Singh. All Rights Reserved.
 * http://osxbook.com
 *
 * http://code.google.com/p/macfuse/
 *
 * Source License: GNU GENERAL PUBLIC LICENSE (GPL)
 */

#define OSXFUSE_PROCFS_VERSION "2.0"
#define FUSE_USE_VERSION 26

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>

#include <grp.h>
#include <pwd.h>

#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/vm_region.h>
#include <mach/vm_statistics.h>

#include <Carbon/Carbon.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>

#include <cassert>
#include <vector>
#include <pcrecpp.h>

#include <fuse.h>

#include "procfs_displays.h"
#include "procfs_proc_info.h"
#include "procfs_windows.h"
#include "sequencegrab/procfs_sequencegrab.h"

#if OSXFUSE_PROCFS_ENABLE_TPM
#include "procfs_tpm.h"
#endif /* OSXFUSE_PROCFS_ENABLE_TPM */

static int procfs_ui = 0;
#define PROCFS_DEFAULT_FILE_SIZE 65536

static int total_file_patterns = 0;
static int total_directory_patterns = 0;
static int total_link_patterns = 0;

static processor_port_array_t processor_list;
static mach_port_t  p_default_set = 0;
static mach_port_t  p_default_set_control = 0;
static host_priv_t  host_priv;
static natural_t    processor_count = 0;
static io_connect_t lightsensor_port = 0;
static io_connect_t motionsensor_port = 0;
static unsigned int sms_gIndex = 0;
static IOItemCount  sms_gStructureInputSize = 0;
static IOByteCount  sms_gStructureOutputSize = 0;

/* camera */
static pthread_mutex_t  camera_lock;
static int              camera_busy = 0;
static CFMutableDataRef camera_tiff = (CFMutableDataRef)0;

/* display */
static pthread_mutex_t  display_lock;
static int              display_busy = 0;
static CFMutableDataRef display_png = (CFMutableDataRef)0;

static pcrecpp::RE *valid_process_pattern = new pcrecpp::RE("/(\\d+)");

typedef struct {
    char x;
    char y;
    char z;
    short v;
#define FILLER_SIZE 60
    char scratch[FILLER_SIZE];
} MotionSensorData_t;

static kern_return_t
sms_getOrientation_hardware_apple(MotionSensorData_t *odata)
{
    kern_return_t      kr;
    size_t             isize = sms_gStructureInputSize;
    size_t             osize = sms_gStructureOutputSize;
    MotionSensorData_t idata;

    kr = IOConnectCallStructMethod((mach_port_t) motionsensor_port,
                                   sms_gIndex,
                                   &idata,
                                   isize,
                                   odata,
                                   &osize);
    return kr;
}

static int init_task_list(task_array_t           *task_list,
                          mach_msg_type_number_t *task_count)
{
    return processor_set_tasks(p_default_set_control, task_list, task_count);
}

static void fini_task_list(task_array_t           task_list,
                           mach_msg_type_number_t task_count)
{
    unsigned int i;
    for (i = 0; i < task_count; i++) {
        mach_port_deallocate(mach_task_self(), task_list[i]);
    }
    vm_deallocate(mach_task_self(), (vm_address_t)task_list,
                  task_count * sizeof(task_t));
}

static int init_thread_list(task_t                  the_task,
                            thread_array_t         *thread_list,
                            mach_msg_type_number_t *thread_count)
{
    return task_threads(the_task, thread_list, thread_count);
}

static void fini_thread_list(thread_array_t         thread_list,
                             mach_msg_type_number_t thread_count)
{
    unsigned int i;
    for (i = 0; i < thread_count; i++) {
        mach_port_deallocate(mach_task_self(), thread_list[i]);
    }
    vm_deallocate(mach_task_self(), (vm_address_t)thread_list,
                  thread_count * sizeof(thread_act_t));
}

static int init_port_list(task_t                  the_task,
                          mach_port_name_array_t *name_list,
                          mach_msg_type_number_t *name_count,
                          mach_port_type_array_t *type_list,
                          mach_msg_type_number_t *type_count)
{
    return mach_port_names(the_task,
                           name_list, name_count, type_list, type_count);
}

static void fini_port_list(mach_port_name_array_t name_list,
                           mach_msg_type_number_t name_count,
                           mach_port_type_array_t type_list,
                           mach_msg_type_number_t type_count)
{
    vm_deallocate(mach_task_self(), (vm_address_t)name_list,
                  name_count * sizeof(mach_port_name_t));
    vm_deallocate(mach_task_self(), (vm_address_t)type_list,
                  type_count * sizeof(mach_port_type_t));
}
                           

#define DECL_PORT_LIST() \
    mach_port_name_array_t name_list;  \
    mach_msg_type_number_t name_count; \
    mach_port_type_array_t type_list;  \
    mach_msg_type_number_t type_count;
#define INIT_PORT_LIST(the_task) \
    if (init_port_list(the_task, &name_list, &name_count, &type_list, &type_count) != 0) { \
        return -EIO; \
    }
#define FINI_PORT_LIST() \
    fini_port_list(name_list, name_count, type_list, type_count)

#define DECL_TASK_LIST() \
    task_array_t           task_list; \
    mach_msg_type_number_t task_count;
#define INIT_TASK_LIST() \
    if (init_task_list(&task_list, &task_count) != 0) { return -EIO; }
#define FINI_TASK_LIST() \
    fini_task_list(task_list, task_count)

#define DECL_THREAD_LIST() \
    thread_array_t         thread_list; \
    mach_msg_type_number_t thread_count;
#define INIT_THREAD_LIST(the_task) \
    if (init_thread_list(the_task, &thread_list, &thread_count) != 0) { \
        return -EIO; \
    }
#define FINI_THREAD_LIST() \
    fini_thread_list(thread_list, thread_count)

struct procfs_dispatcher_entry;
typedef struct procfs_dispatcher_entry * procfs_dispatcher_entry_t;

typedef int (*procfs_open_handler_t)(procfs_dispatcher_entry_t  e,
                                     const char                *argv[],
                                     const char                *path,
                                     struct fuse_file_info     *fi);

typedef int (*procfs_release_handler_t)(procfs_dispatcher_entry_t  e,
                                        const char                *argv[],
                                        const char                *path,
                                        struct fuse_file_info     *fi);
typedef int (*procfs_opendir_handler_t)(procfs_dispatcher_entry_t  e,
                                        const char                *argv[],
                                        const char                *path,
                                        struct fuse_file_info     *fi);

typedef int (*procfs_releasedir_handler_t)(procfs_dispatcher_entry_t  e,
                                           const char                *argv[],
                                           const char                *path,
                                           struct fuse_file_info     *fi);

typedef int (*procfs_getattr_handler_t)(procfs_dispatcher_entry_t  e,
                                        const char                *argv[],
                                        struct stat               *stbuf);

typedef int (*procfs_read_handler_t)(procfs_dispatcher_entry_t  e,
                                     const char                *argv[],
                                     char                      *buf,
                                     size_t                     size,
                                     off_t                      offset,
                                     struct fuse_file_info     *fi);

typedef int (*procfs_readdir_handler_t)(procfs_dispatcher_entry_t  e,
                                        const char                *argv[],
                                        void                      *buf,
                                        fuse_fill_dir_t            filler,
                                        off_t                      offset,
                                        struct fuse_file_info     *fi);

typedef int (*procfs_readlink_handler_t)(procfs_dispatcher_entry_t  e,
                                         const char                *argv[],
                                         char                      *buf,
                                         size_t                    size);

struct procfs_dispatcher_entry {
    int                         flag;
    const char                 *pattern;
    pcrecpp::RE                *compiled_pattern;
    int                         argc;
    procfs_open_handler_t       open;
    procfs_release_handler_t    release;
    procfs_opendir_handler_t    opendir;
    procfs_releasedir_handler_t releasedir;
    procfs_getattr_handler_t    getattr;
    procfs_read_handler_t       read;
    procfs_readdir_handler_t    readdir;
    procfs_readlink_handler_t   readlink;
    const char                 *content_files[32];
    const char                 *content_directories[32];
};

/* flags */
#define PROCFS_FLAG_ISDOTFILE 0x00000001

#define PROCFS_MAX_ARGS       3

#define OPEN_HANDLER(handler) \
int \
procfs_open_##handler(procfs_dispatcher_entry_t  e,      \
                      const char                *argv[], \
                      const char                *path,   \
                      struct fuse_file_info     *fi)     \

#define RELEASE_HANDLER(handler) \
int \
procfs_release_##handler(procfs_dispatcher_entry_t  e,      \
                         const char                 *argv[], \
                         const char                 *path,   \
                         struct fuse_file_info      *fi)     \

#define OPENDIR_HANDLER(handler) \
int \
procfs_opendir_##handler(procfs_dispatcher_entry_t  e,      \
                         const char                *argv[], \
                         const char                *path,   \
                         struct fuse_file_info     *fi)     \

#define RELEASEDIR_HANDLER(handler) \
int \
procfs_releasedir_##handler(procfs_dispatcher_entry_t  e,      \
                            const char                 *argv[], \
                            const char                 *path,   \
                            struct fuse_file_info      *fi)     \

#define GETATTR_HANDLER(handler) \
int \
procfs_getattr_##handler(procfs_dispatcher_entry_t  e,      \
                         const char                *argv[], \
                         struct stat               *stbuf)  \

#define READ_HANDLER(handler) \
int \
procfs_read_##handler(procfs_dispatcher_entry_t  e,      \
                      const char                *argv[], \
                      char                      *buf,    \
                      size_t                     size,   \
                      off_t                      offset, \
                      struct fuse_file_info     *fi)     \

#define READDIR_HANDLER(handler) \
int \
procfs_readdir_##handler(procfs_dispatcher_entry_t  e,      \
                         const char                *argv[], \
                         void                      *buf,    \
                         fuse_fill_dir_t            filler, \
                         off_t                      offset, \
                         struct fuse_file_info     *fi)     \

#define READLINK_HANDLER(handler) \
int \
procfs_readlink_##handler(procfs_dispatcher_entry_t  e,      \
                          const char                *argv[], \
                          char                      *buf,    \
                          size_t                     size)   \

#define PROTO_OPEN_HANDLER(handler)       OPEN_HANDLER(handler)
#define PROTO_RELEASE_HANDLER(handler)    RELEASE_HANDLER(handler)
#define PROTO_OPENDIR_HANDLER(handler)    OPENDIR_HANDLER(handler)
#define PROTO_RELEASEDIR_HANDLER(handler) RELEASEDIR_HANDLER(handler)
#define PROTO_READ_HANDLER(handler)       READ_HANDLER(handler)
#define PROTO_READDIR_HANDLER(handler)    READDIR_HANDLER(handler)
#define PROTO_READLINK_HANDLER(handler)   READLINK_HANDLER(handler)
#define PROTO_GETATTR_HANDLER(handler)    GETATTR_HANDLER(handler)

#define DECL_FILE(pattern, argc, openp, releasep, getattrp, readp) \
    {                                        \
        0,                                   \
        pattern,                             \
        new pcrecpp::RE(pattern),            \
        argc,                                \
        procfs_open_##openp,                 \
        procfs_release_##releasep,           \
        procfs_opendir_enotdir,              \
        procfs_releasedir_enotdir,           \
        procfs_getattr_##getattrp,           \
        procfs_read_##readp,                 \
        procfs_readdir_enotdir,              \
        procfs_readlink_einval,              \
        { NULL },                            \
        { NULL }                             \
    },

#define DECL_FILE_WITHFLAGS(flag, pattern, argc, openp, releasep, getattrp, readp) \
    {                                        \
        flag,                                \
        pattern,                             \
        new pcrecpp::RE(pattern),            \
        argc,                                \
        procfs_open_##openp,                 \
        procfs_release_##releasep,           \
        procfs_opendir_enotdir,              \
        procfs_releasedir_enotdir,           \
        procfs_getattr_##getattrp,           \
        procfs_read_##readp,                 \
        procfs_readdir_enotdir,              \
        procfs_readlink_einval,              \
        { NULL },                            \
        { NULL }                             \
    },

#define DECL_DIRECTORY(pattern, argc, opendirp, releasedirp, getattrp, readdirp, contents, ...) \
    {                                        \
        0,                                   \
        pattern,                             \
        new pcrecpp::RE(pattern),            \
        argc,                                \
        procfs_open_eisdir,                  \
        procfs_release_eisdir,               \
        procfs_opendir_##opendirp,           \
        procfs_releasedir_##releasedirp,     \
        procfs_getattr_##getattrp,           \
        procfs_read_eisdir,                  \
        procfs_readdir_##readdirp,           \
        procfs_readlink_einval,              \
        contents,                            \
        __VA_ARGS__                          \
    },

#define DECL_DIRECTORY_COMPACT(pattern, contents, ...) \
    {                                        \
        0,                                   \
        pattern,                             \
        new pcrecpp::RE(pattern),            \
        0,                                   \
        procfs_open_eisdir,                  \
        procfs_release_eisdir,               \
        procfs_opendir_default_directory,    \
        procfs_releasedir_default_directory, \
        procfs_getattr_default_directory,    \
        procfs_read_eisdir,                  \
        procfs_readdir_default,              \
        procfs_readlink_einval,              \
        contents,                            \
        ##__VA_ARGS__                        \
    },

#define DECL_LINK(pattern, argc, openp, releasep, getattrp, readlinkp) \
    {                                        \
        0,                                   \
        pattern,                             \
        new pcrecpp::RE(pattern),            \
        argc,                                \
        procfs_open_##openp,                 \
        procfs_release_##releasep,           \
        procfs_opendir_enotdir,              \
        procfs_releasedir_enotdir,           \
        procfs_getattr_##getattrp,           \
        procfs_read_einval,                  \
        procfs_readdir_enotdir,              \
        procfs_readlink_##readlinkp,         \
        { NULL },                            \
        { NULL }                             \
    },

#define DECL_LINK_COMPACT(pattern, argc, readlinkp) \
    {                                        \
        0,                                   \
        pattern,                             \
        new pcrecpp::RE(pattern),            \
        argc,                                \
        procfs_open_default_file,            \
        procfs_release_default_file,         \
        procfs_opendir_enotdir,              \
        procfs_releasedir_enotdir,           \
        procfs_getattr_default_link,         \
        procfs_read_einval,                  \
        procfs_readdir_enotdir,              \
        procfs_readlink_##readlinkp,         \
        { NULL },                            \
        { NULL }                             \
    },

PROTO_OPEN_HANDLER(default_file);
PROTO_OPEN_HANDLER(eisdir);
PROTO_OPEN_HANDLER(proc__windows__identify);
PROTO_OPEN_HANDLER(proc__windows__screenshots__window);
PROTO_OPEN_HANDLER(system__hardware__camera__screenshot);
PROTO_OPEN_HANDLER(system__hardware__displays__display__screenshot);

PROTO_RELEASE_HANDLER(default_file);
PROTO_RELEASE_HANDLER(eisdir);
PROTO_RELEASE_HANDLER(proc__windows__identify);
PROTO_RELEASE_HANDLER(proc__windows__screenshots__window);
PROTO_RELEASE_HANDLER(system__hardware__camera__screenshot);
PROTO_RELEASE_HANDLER(system__hardware__displays__display__screenshot);

PROTO_OPENDIR_HANDLER(default_directory);
PROTO_OPENDIR_HANDLER(enotdir);

PROTO_RELEASEDIR_HANDLER(default_directory);
PROTO_RELEASEDIR_HANDLER(enotdir);

PROTO_GETATTR_HANDLER(default_file);
PROTO_GETATTR_HANDLER(default_file_finder_info);
PROTO_GETATTR_HANDLER(default_directory);
PROTO_GETATTR_HANDLER(default_link);
PROTO_GETATTR_HANDLER(byname__name);
PROTO_GETATTR_HANDLER(system__hardware__camera__screenshot);
PROTO_GETATTR_HANDLER(system__hardware__displays__display);
PROTO_GETATTR_HANDLER(system__hardware__displays__display__screenshot);
#if OSXFUSE_PROCFS_ENABLE_TPM
PROTO_GETATTR_HANDLER(system__hardware__tpm__keyslots__slot);
PROTO_GETATTR_HANDLER(system__hardware__tpm__pcrs__pcr);
#endif /* OSXFUSE_PROCFS_ENABLE_TPM */
PROTO_GETATTR_HANDLER(proc__task__ports__port);
PROTO_GETATTR_HANDLER(proc__task__threads__thread);
PROTO_GETATTR_HANDLER(proc__windows__screenshots__window);

PROTO_READ_HANDLER(einval);
PROTO_READ_HANDLER(eisdir);
PROTO_READ_HANDLER(zero);
PROTO_READ_HANDLER(default_file_finder_info);
PROTO_READ_HANDLER(proc__carbon);
#if __i386__
PROTO_READ_HANDLER(proc__fds);
#endif /* __i386__ */
PROTO_READ_HANDLER(proc__generic);
PROTO_READ_HANDLER(proc__task__absolutetime_info);
PROTO_READ_HANDLER(proc__task__basic_info);
PROTO_READ_HANDLER(proc__task__events_info);
PROTO_READ_HANDLER(proc__task__thread_times_info);
PROTO_READ_HANDLER(proc__task__mach_name);
PROTO_READ_HANDLER(proc__task__ports__port);
PROTO_READ_HANDLER(proc__task__role);
PROTO_READ_HANDLER(proc__task__threads__thread__basic_info);
PROTO_READ_HANDLER(proc__task__threads__thread__states__debug);
PROTO_READ_HANDLER(proc__task__threads__thread__states__exception);
PROTO_READ_HANDLER(proc__task__threads__thread__states__float);
PROTO_READ_HANDLER(proc__task__threads__thread__states__thread);
PROTO_READ_HANDLER(proc__task__tokens);
PROTO_READ_HANDLER(proc__task__vmmap);
PROTO_READ_HANDLER(proc__task__vmmap_r);
PROTO_READ_HANDLER(proc__windows__generic);
PROTO_READ_HANDLER(proc__windows__screenshots__window);
PROTO_READ_HANDLER(proc__xcred);
PROTO_READ_HANDLER(system__firmware__variables);
PROTO_READ_HANDLER(system__hardware__camera__screenshot);
PROTO_READ_HANDLER(system__hardware__cpus__cpu__data);
PROTO_READ_HANDLER(system__hardware__displays__display__info);
PROTO_READ_HANDLER(system__hardware__displays__display__screenshot);
#if OSXFUSE_PROCFS_ENABLE_TPM
PROTO_READ_HANDLER(system__hardware__tpm__hwmodel);
PROTO_READ_HANDLER(system__hardware__tpm__hwvendor);
PROTO_READ_HANDLER(system__hardware__tpm__hwversion);
PROTO_READ_HANDLER(system__hardware__tpm__keyslots__slot);
PROTO_READ_HANDLER(system__hardware__tpm__pcrs__pcr);
#endif /* OSXFUSE_PROCFS_ENABLE_TPM */
PROTO_READ_HANDLER(system__hardware__xsensor);

PROTO_READDIR_HANDLER(default);
PROTO_READDIR_HANDLER(enotdir);
PROTO_READDIR_HANDLER(byname);
PROTO_READDIR_HANDLER(proc__task__ports);
PROTO_READDIR_HANDLER(proc__task__threads);
PROTO_READDIR_HANDLER(proc__windows__screenshots);
PROTO_READDIR_HANDLER(root);
PROTO_READDIR_HANDLER(system__hardware__cpus);
PROTO_READDIR_HANDLER(system__hardware__cpus__cpu);
PROTO_READDIR_HANDLER(system__hardware__displays);
PROTO_READDIR_HANDLER(system__hardware__displays__display);
#if OSXFUSE_PROCFS_ENABLE_TPM
PROTO_READDIR_HANDLER(system__hardware__tpm__keyslots);
PROTO_READDIR_HANDLER(system__hardware__tpm__pcrs);
#endif /* OSXFUSE_PROCFS_ENABLE_TPM */

PROTO_READLINK_HANDLER(einval);
PROTO_READLINK_HANDLER(byname__name);

static struct procfs_dispatcher_entry
procfs_link_table[] = {

    DECL_LINK(
        "/byname/(.+)",
        1,
        default_file,
        default_file,
        byname__name,
        byname__name
    )
};

static struct procfs_dispatcher_entry
procfs_file_table[] = {

    DECL_FILE_WITHFLAGS(
        PROCFS_FLAG_ISDOTFILE,
        "/system/.*\\._.*|/\\d+/.*\\._.*",
        0,
        default_file,
        default_file,
        default_file_finder_info,
        default_file_finder_info
    )

    DECL_FILE(
        "/system/firmware/variables",
        0,
        default_file,
        default_file,
        default_file,
        system__firmware__variables
    )

    DECL_FILE(
        "/system/hardware/(lightsensor|motionsensor|mouse)/data",
        1,
        default_file,
        default_file,
        default_file,
        system__hardware__xsensor
    )

    DECL_FILE(
        "/system/hardware/camera/screenshot.tiff",
        0,
        system__hardware__camera__screenshot,
        system__hardware__camera__screenshot,
        system__hardware__camera__screenshot,
        system__hardware__camera__screenshot
    )

    DECL_FILE(
        "/system/hardware/cpus/(\\d+)/data",
        1,
        default_file,
        default_file,
        default_file,
        system__hardware__cpus__cpu__data
    )

    DECL_FILE(
        "/system/hardware/displays/(\\d+)/info",
        1,
        default_file,
        default_file,
        default_file,
        system__hardware__displays__display__info
    )

    DECL_FILE(
        "/system/hardware/displays/(\\d+)/screenshot.png",
        1,
        system__hardware__displays__display__screenshot,
        system__hardware__displays__display__screenshot,
        system__hardware__displays__display__screenshot,
        system__hardware__displays__display__screenshot
    )

#if OSXFUSE_PROCFS_ENABLE_TPM
    DECL_FILE(
        "/system/hardware/tpm/hwmodel",
        0,
        default_file,
        default_file,
        default_file,
        system__hardware__tpm__hwmodel
    )

    DECL_FILE(
        "/system/hardware/tpm/hwvendor",
        0,
        default_file,
        default_file,
        default_file,
        system__hardware__tpm__hwvendor
    )

    DECL_FILE(
        "/system/hardware/tpm/hwversion",
        0,
        default_file,
        default_file,
        default_file,
        system__hardware__tpm__hwversion
    )

    DECL_FILE(
        "/system/hardware/tpm/keyslots/key(\\d+)",
        1,
        default_file,
        default_file,
        system__hardware__tpm__keyslots__slot,
        system__hardware__tpm__keyslots__slot
    )

    DECL_FILE(
        "/system/hardware/tpm/pcrs/pcr(\\d+)",
        1,
        default_file,
        default_file,
        system__hardware__tpm__pcrs__pcr,
        system__hardware__tpm__pcrs__pcr
    )
#endif /* OSXFUSE_PROCFS_ENABLE_TPM */

    DECL_FILE(
        "/(\\d+)/carbon/(name|psn)",
        2,
        default_file,
        default_file,
        default_file,
        proc__carbon
    )

#if __i386__
    DECL_FILE(
        "/(\\d+)/fds",
        1,
        default_file,
        default_file,
        default_file,
        proc__fds
    )
#endif /* __i386__ */

    DECL_FILE(
        "/(\\d+)/(cmdline|jobc|paddr|pgid|ppid|tdev|tpgid|wchan)",
        2,
        default_file,
        default_file,
        default_file,
        proc__generic
    )

    DECL_FILE(
        "/(\\d+)/task/absolutetime_info/(threads_system|threads_user|total_system|total_user)",
        2,
        default_file,
        default_file,
        default_file,
        proc__task__absolutetime_info
    )

    DECL_FILE(
        "/(\\d+)/task/basic_info/(policy|resident_size|suspend_count|system_time|user_time|virtual_size)",
        2,
        default_file,
        default_file,
        default_file,
        proc__task__basic_info
    )

    DECL_FILE(
        "/(\\d+)/task/events_info/(cow_faults|csw|faults|messages_received|messages_sent|pageins|syscalls_mach|syscalls_unix)",
        2,
        default_file,
        default_file,
        default_file,
        proc__task__events_info
    )

    DECL_FILE(
        "/(\\d+)/task/thread_times_info/(system_time|user_time)",
        2,
        default_file,
        default_file,
        default_file,
        proc__task__thread_times_info
    )

    DECL_FILE(
        "/(\\d+)/task/mach_name",
        1,
        default_file,
        default_file,
        default_file,
        proc__task__mach_name
    )

    DECL_FILE(
        "/(\\d+)/task/ports/([a-f\\d]+)/(msgcount|qlimit|seqno|sorights|task_rights)",
        3,
        default_file,
        default_file,
        default_file,
        proc__task__ports__port
    )

    DECL_FILE(
        "/(\\d+)/task/role",
        1,
        default_file,
        default_file,
        default_file,
        proc__task__role
    )

    DECL_FILE(
        "/(\\d+)/task/threads/([a-f\\d]+)/basic_info/(cpu_usage|flags|policy|run_state|sleep_time|suspend_count|system_time|user_time)",
        3,
        default_file,
        default_file,
        default_file,
        proc__task__threads__thread__basic_info
    )

    DECL_FILE(
        "/(\\d+)/task/threads/([a-f\\d]+)/states/debug/(dr[0-7])",
        3,
        default_file,
        default_file,
        default_file,
        proc__task__threads__thread__states__debug
    )

    DECL_FILE(
        "/(\\d+)/task/threads/([a-f\\d]+)/states/exception/(err|faultvaddr|trapno)",
        3,
        default_file,
        default_file,
        default_file,
        proc__task__threads__thread__states__exception
    )

    DECL_FILE(
        "/(\\d+)/task/threads/([a-f\\d]+)/states/float/(fpu_fcw|fpu_fsw|fpu_ftw|fpu_fop|fpu_ip|fpu_cs|fpu_dp|fpu_ds|fpu_mxcsr|fpu_mxcsrmask)",
        3,
        default_file,
        default_file,
        default_file,
        proc__task__threads__thread__states__float
    )

    DECL_FILE(
        "/(\\d+)/task/threads/([a-f\\d]+)/states/thread/(e[a-d]x|edi|esi|ebp|esp|ss|eflags|eip|[cdefg]s)",
        3,
        default_file,
        default_file,
        default_file,
        proc__task__threads__thread__states__thread
    )

    DECL_FILE(
        "/(\\d+)/task/tokens/(audit|security)",
        2,
        default_file,
        default_file,
        default_file,
        proc__task__tokens
    )

    DECL_FILE(
        "/(\\d+)/task/vmmap",
        1,
        default_file,
        default_file,
        default_file,
        proc__task__vmmap
    )

    DECL_FILE(
        "/(\\d+)/task/vmmap_r",
        1,
        default_file,
        default_file,
        default_file,
        proc__task__vmmap_r
    )

    DECL_FILE(
        "/(\\d+)/windows/(all|onscreen)",
        2,
        default_file,
        default_file,
        default_file,
        proc__windows__generic
    )

    DECL_FILE(
        "/(\\d+)/windows/identify",
        1,
        proc__windows__identify,
        proc__windows__identify,
        default_file,
        zero
    )

    DECL_FILE(
        "/(\\d+)/windows/screenshots/([a-f\\d]+).png",
        2,
        proc__windows__screenshots__window, 
        proc__windows__screenshots__window, 
        proc__windows__screenshots__window,
        proc__windows__screenshots__window
    )

    DECL_FILE(
        "/(\\d+)/(ucred|pcred)/(groups|rgid|ruid|svgid|svuid|uid)",
        3,
        default_file,
        default_file,
        default_file,
        proc__xcred
    )
};

static struct procfs_dispatcher_entry
procfs_directory_table[] = {
    DECL_DIRECTORY(
        "/",
        0,
        default_directory,
        default_directory,
        default_directory,
        root,
        { NULL },
        { "byname", "system", NULL }
    )

    DECL_DIRECTORY(
        "/byname",
        0,
        default_directory,
        default_directory,
        default_directory,
        byname,
        { NULL },
        { NULL }
    )

    DECL_DIRECTORY_COMPACT(
        "/system",
        { NULL },
        { "firmware", "hardware", NULL },
    )

    DECL_DIRECTORY_COMPACT(
        "/system/firmware",
        { "variables", NULL },
        { NULL }
    )

    DECL_DIRECTORY_COMPACT(
        "/system/hardware",
        { NULL },
#if OSXFUSE_PROCFS_ENABLE_TPM
        {
            "camera", "cpus", "displays", "lightsensor", "motionsensor",
             "mouse", "tpm", NULL
        }
#else
        {
            "camera", "cpus", "displays", "lightsensor", "motionsensor",
            "mouse", NULL
        }
#endif /* OSXFUSE_PROCFS_ENABLE_TPM */
    )

    DECL_DIRECTORY_COMPACT(
        "/system/hardware/camera",
        { "screenshot.tiff", NULL },
        { NULL },
    )

    DECL_DIRECTORY(
        "/system/hardware/cpus",
        0,
        default_directory,
        default_directory,
        default_directory,
        system__hardware__cpus,
        { NULL },
        { NULL },
    )

    DECL_DIRECTORY(
        "/system/hardware/cpus/(\\d+)",
        1,
        default_directory,
        default_directory,
        default_directory,
        system__hardware__cpus__cpu,
        { "data", NULL },
        { NULL },
    )

    DECL_DIRECTORY(
        "/system/hardware/displays",
        0,
        default_directory,
        default_directory,
        default_directory,
        system__hardware__displays,
        { NULL },
        { NULL },
    )

    DECL_DIRECTORY(
        "/system/hardware/displays/(\\d+)",
        1,
        default_directory,
        default_directory,
        system__hardware__displays__display,
        system__hardware__displays__display,
        { "info", "screenshot.png", NULL },
        { NULL },
    )

    DECL_DIRECTORY_COMPACT(
        "/system/hardware/lightsensor",
        { "data", NULL },
        { NULL },
    )

    DECL_DIRECTORY_COMPACT(
        "/system/hardware/motionsensor",
        { "data", NULL },
        { NULL },
    )

    DECL_DIRECTORY_COMPACT(
        "/system/hardware/mouse",
        { "data", NULL },
        { NULL },
    )

#if OSXFUSE_PROCFS_ENABLE_TPM
    DECL_DIRECTORY_COMPACT(
        "/system/hardware/tpm",
        { "hwmodel", "hwvendor", "hwversion", NULL },
        { "keyslots", "pcrs" }
    )

    DECL_DIRECTORY(
        "/system/hardware/tpm/keyslots",
        0,
        default_directory,
        default_directory,
        default_directory,
        system__hardware__tpm__keyslots,
        { NULL },
        { NULL },
    )

    DECL_DIRECTORY(
        "/system/hardware/tpm/pcrs",
        0,
        default_directory,
        default_directory,
        default_directory,
        system__hardware__tpm__pcrs,
        { NULL },
        { NULL },
    )
#endif /* OSXFUSE_PROCFS_ENABLE_TPM */

    DECL_DIRECTORY_COMPACT(
        "/\\d+",
        {
            "cmdline",
#if __i386__
            "fds",
#endif /* __i386__ */
            "jobc", "paddr", "pgid", "ppid", "tdev", "tpgid",
            "wchan", "windows", NULL
        },
        { "carbon", "pcred", "task", "ucred", NULL }
    )

    DECL_DIRECTORY_COMPACT(
        "/\\d+/carbon",
        { "name", "psn", NULL },
        { NULL }
    )

    DECL_DIRECTORY_COMPACT(
        "/\\d+/pcred",
        { "rgid", "ruid", "svgid", "svgid", NULL },
        { NULL }
    )

    DECL_DIRECTORY_COMPACT(
        "/\\d+/task",
        { "mach_name", "role", "vmmap", "vmmap_r", NULL },
        {
            "absolutetime_info", "basic_info", "events_info", "ports",
            "thread_times_info", "threads", "tokens", NULL
        }
    )

    DECL_DIRECTORY_COMPACT(
        "/\\d+/task/absolutetime_info",
        {
            "threads_system", "threads_user", "total_system",
            "total_user", NULL
        },
        { NULL }
    )

    DECL_DIRECTORY_COMPACT(
        "/\\d+/task/basic_info",
        {
            "policy", "resident_size", "suspend_count", "system_time",
            "user_time", "virtual_size", NULL
        },
        { NULL }
    )

    DECL_DIRECTORY_COMPACT(
        "/\\d+/task/events_info",
        {
            "cow_faults", "csw", "faults", "messages_received",
            "messages_sent", "pageins", "syscalls_mach", "syscalls_unix", NULL
        },
        { NULL }
    )

    DECL_DIRECTORY(
        "/(\\d+)/task/ports",
        1,
        default_directory,
        default_directory,
        default_directory,
        proc__task__ports,
        { NULL },
        { NULL }
    )

    DECL_DIRECTORY(
        "/(\\d+)/task/ports/([a-f\\d]+)",
        2,
        default_directory,
        default_directory,
        proc__task__ports__port,
        default,
        { "msgcount", "qlimit", "seqno", "sorights", "task_rights", NULL },
        { NULL }
    )

    DECL_DIRECTORY_COMPACT(
        "/\\d+/task/thread_times_info",
        { "system_time", "user_time", NULL },
        { NULL }
    )

    DECL_DIRECTORY(
        "/(\\d+)/task/threads",
        1,
        default_directory,
        default_directory,
        default_directory,
        proc__task__threads,
        { NULL },
        { NULL }
    )

    DECL_DIRECTORY(
        "/(\\d+)/task/threads/([a-f\\d])+",
        2,
        default_directory,
        default_directory,
        proc__task__threads__thread,
        default,
        { NULL },
        { "basic_info", "states", NULL }
    )

    DECL_DIRECTORY_COMPACT(
        "/\\d+/task/threads/[a-f\\d]+/basic_info",
        {
            "cpu_usage", "flags", "policy", "run_state", "sleep_time",
            "suspend_count", "system_time", "user_time", NULL
        },
        { NULL }
    )

    DECL_DIRECTORY_COMPACT(
        "/\\d+/task/threads/[a-f\\d]+/states",
        { "debug", "exception", "float", "thread", NULL },
        { NULL }
    )

    DECL_DIRECTORY_COMPACT(
        "/\\d+/task/threads/[a-f\\d]+/states/debug",
        { "dr0", "dr1", "dr2", "dr3", "dr4", "dr5", "dr6", "dr7", NULL },
        { NULL }
    )

    DECL_DIRECTORY_COMPACT(
        "/\\d+/task/threads/[a-f\\d]+/states/exception",
        { "err", "faultvaddr", "trapno", NULL },
        { NULL }
    )

    DECL_DIRECTORY_COMPACT(
        "/\\d+/task/threads/[a-f\\d]+/states/float",
        {
            "fpu_cs", "fpu_dp", "fpu_ds", "fpu_fcw", "fpu_fop", "fpu_fsw",
            "fpu_ftw", "fpu_ip", "fpu_mxcsr", "fpu_mxcsrmask", NULL
        },
        { NULL }
    )

    DECL_DIRECTORY_COMPACT(
        "/\\d+/task/threads/[a-f\\d]+/states/thread",
        {
            "eax", "ebx", "ecx", "edx", "edi", "esi", "ebp", "esp", "ss",
            "eflags", "eip", "cs", "ds", "es", "fs", "gs", NULL
        },
        { NULL }
    )

    DECL_DIRECTORY_COMPACT(
        "/\\d+/task/tokens",
        { "audit", "security", NULL },
        { NULL }
    )

    DECL_DIRECTORY_COMPACT(
        "/\\d+/ucred",
        { "groups", "uid", NULL },
        { NULL }
    )

    DECL_DIRECTORY_COMPACT(
        "/\\d+/windows",
        { "all", "onscreen", "identify", NULL },
        { "screenshots", NULL }
    )

    DECL_DIRECTORY(
        "/(\\d+)/windows/screenshots",
        1,
        default_directory,
        default_directory,
        default_directory,
        proc__windows__screenshots,
        { NULL },
        { NULL },
    )

};

// BEGIN: OPEN/OPENDIR

//
// int
// procfs_open/opendir_<handler>(procfs_dispatcher_entry_t  e,
//                               const char                *argv[],
//                               const char                *path,
//                               struct fuse_file_info     *fi)

OPEN_HANDLER(default_file)
{
    return 0;
}

OPEN_HANDLER(eisdir)
{
    return -EISDIR;
}

OPEN_HANDLER(proc__windows__identify)
{
    if (fi->fh != 0) { /* XXX: need locking */
        return 0;
    } else {
        fi->fh = 1;
    }
    char *whandler = NULL;
    if ((whandler = getenv("OSXFUSE_PROCFS_WHANDLER")) == NULL) {
        goto bail;
    }
    {
        int npid = vfork();
        if (npid == 0) {
            execl(whandler, whandler, argv[0], NULL);
            return 0;
        }
    }

bail:
    return 0;
}

OPEN_HANDLER(proc__windows__screenshots__window)
{
    if (fi->fh != 0) { /* XXX: need locking */
        return 0;
    }

    pid_t pid = strtol(argv[0], NULL, 10);
    CGWindowID target = strtol(argv[1], NULL, 16);
    ProcessSerialNumber psn;

    OSStatus status = GetProcessForPID(pid, &psn);
    if (status != noErr) {
        return -ENOENT;
    }

    CGSConnectionID conn;
    CGError err = CGSGetConnectionIDForPSN(0, &psn, &conn);
    if (err != kCGErrorSuccess) {
        return -ENOENT;
    }

#define MAX_WINDOWS 256
    CGSWindowID windowIDs[MAX_WINDOWS];
    int windowCount = 0;
    int i = 0;

    err = CGSGetWindowList(_CGSDefaultConnection(), conn, MAX_WINDOWS,
                           windowIDs, &windowCount);

    for (i = 0; i < windowCount; i++) {
        if (windowIDs[i] == target) {
            goto doread;
        }
    }

    return -ENOENT;

doread:

    CFMutableDataRef window_png = (CFMutableDataRef)0;
    int ret = PROCFS_GetPNGForWindowAtIndex(target, &window_png);

    if (ret == -1) {
        return -EIO;
    }

    struct ProcfsWindowData *pwd =
        (struct ProcfsWindowData *)malloc(sizeof(struct ProcfsWindowData));
    if (!pwd) {
        CFRelease(window_png);
        return -ENOMEM;
    }

    pwd->window_png = window_png;
    pwd->max_len = PROCFS_GetPNGSizeForWindowAtIndex(target);
    pwd->len = (size_t)CFDataGetLength(window_png);

    fi->fh = (uint64_t)pwd;

    return 0;
}

OPEN_HANDLER(system__hardware__camera__screenshot)
{
    pthread_mutex_lock(&camera_lock);
    if (camera_busy) {
        pthread_mutex_unlock(&camera_lock);
        return -EBUSY;
    } else {
        camera_busy = 1;
        pthread_mutex_unlock(&camera_lock);
    }

    int ret = PROCFS_GetTIFFFromCamera(&camera_tiff);

    return ret;
}

OPEN_HANDLER(system__hardware__displays__display__screenshot)
{
    pthread_mutex_lock(&display_lock);
    if (display_busy) {
        pthread_mutex_unlock(&display_lock);
        return -EBUSY;
    } else {
        display_busy = 1;
        pthread_mutex_unlock(&display_lock);
    }

    unsigned long index = strtol(argv[0], NULL, 10);
    CGDisplayCount display_count = PROCFS_GetDisplayCount();

    if (index >= display_count) {
        return -ENOENT;
    }

    if (display_png) {
        CFRelease(display_png);
        display_png = (CFMutableDataRef)0;
    }

    int ret = PROCFS_GetPNGForDisplayAtIndex(index, &display_png);
    if (ret) {
        if (display_png) {
            CFRelease(display_png);
            display_png = (CFMutableDataRef)0;
        }
        return -EIO;
    }

    return 0;
}

OPENDIR_HANDLER(default_directory)
{
    return 0;
}

OPENDIR_HANDLER(enotdir)
{
    return -ENOTDIR;
}

// END: OPEN/OPENDIR


// BEGIN: RELEASE/RELEASEDIR

//
// int
// procfs_release/releasedir_<handler>(procfs_dispatcher_entry_t  e,
//                                     const char                *argv[],
//                                     const char                *path,
//                                     struct fuse_file_info     *fi)

RELEASE_HANDLER(default_file)
{
    return 0;
}

RELEASE_HANDLER(eisdir)
{
    return -EISDIR;
}

RELEASE_HANDLER(proc__windows__identify)
{
    fi->fh = 0;

    return 0;
}

RELEASE_HANDLER(proc__windows__screenshots__window)
{ 
    if (fi->fh) {
        struct ProcfsWindowData *pwd = (struct ProcfsWindowData *)(fi->fh);
        CFRelease((CFMutableDataRef)(pwd->window_png));
        free((void *)pwd);
        fi->fh = 0;
    }

    return 0;
}

RELEASE_HANDLER(system__hardware__camera__screenshot)
{
    pthread_mutex_lock(&camera_lock);
    camera_busy = 0;
    pthread_mutex_unlock(&camera_lock);

    return 0;
}

RELEASE_HANDLER(system__hardware__displays__display__screenshot)
{
    pthread_mutex_lock(&display_lock);
    display_busy = 0;
    if (display_png) {
        CFRelease(display_png);
        display_png = (CFMutableDataRef)0;
    }
    pthread_mutex_unlock(&display_lock);

    return 0;
}

RELEASEDIR_HANDLER(default_directory)
{
    return 0;
}

RELEASEDIR_HANDLER(enotdir)
{
    return -ENOTDIR;
}

// END: RELEASE/RELEASEDIR


// BEGIN: GETATTR

//
//  int
//  procfs_getattr_<handler>(procfs_dispatcher_entry_t  e,
//                           const char                *argv[],
//                           struct stat               *stbuf)
                          

GETATTR_HANDLER(default_file)
{                         
    time_t current_time = time(NULL);
    stbuf->st_mode = S_IFREG | 0444;             
    stbuf->st_nlink = 1;  
    stbuf->st_size = 0;
    if (procfs_ui) {
        stbuf->st_size = PROCFS_DEFAULT_FILE_SIZE;
    }
    stbuf->st_atime = stbuf->st_ctime = stbuf->st_mtime = current_time;

    return 0;
}   

GETATTR_HANDLER(default_file_finder_info)
{
    if (!procfs_ui) {
        return -ENOENT;
    }

    time_t current_time = time(NULL);
    stbuf->st_mode = S_IFREG | 0444;             
    stbuf->st_nlink = 1;  
    stbuf->st_size = 82;
    stbuf->st_atime = stbuf->st_ctime = stbuf->st_mtime = current_time;

    return 0;
}
    
GETATTR_HANDLER(default_directory)
{
    time_t current_time = time(NULL);

    stbuf->st_mode = S_IFDIR | 0555;
    stbuf->st_nlink = 1;
    stbuf->st_size = 0;
    stbuf->st_atime = stbuf->st_ctime = stbuf->st_mtime = current_time;
    
    return 0;
}

GETATTR_HANDLER(default_link)
{
    stbuf->st_mode = S_IFLNK | 0755;
    stbuf->st_nlink = 1;
    stbuf->st_size = 0;
    
    return 0;
}

GETATTR_HANDLER(byname__name)
{
    const char *target_Pname = argv[0];
    struct stat the_stat;
    char the_name[MAXNAMLEN + 1];  
    Boolean strstatus = false;

    ProcessSerialNumber psn;
    OSErr osErr = noErr;
    OSStatus status;
    CFStringRef Pname;
    pid_t Pid;

    psn.highLongOfPSN = kNoProcess;
    psn.lowLongOfPSN  = kNoProcess;

    while ((osErr = GetNextProcess(&psn)) != procNotFound) {
        status = GetProcessPID(&psn, &Pid);
        if (status != noErr) {
            continue;
        }
        Pname = (CFStringRef)0;
        status = CopyProcessName(&psn, &Pname);
        if (status != noErr) {
            if (Pname) {
                CFRelease(Pname);
                Pname = (CFStringRef)0;
            }
            continue;
        }

        strstatus = CFStringGetCString(Pname, the_name, MAXNAMLEN,
                                       kCFStringEncodingASCII);
        if (strstatus != true) {
            Pid = 0;
        } else if (strcmp(target_Pname, the_name) != 0) {
            Pid = 0;
        }

        CFRelease(Pname);
        Pname = (CFStringRef)0;

        if (Pid) {
            break;
        }
    }

    if (!Pid) {
        return -ENOENT;
    }

    time_t current_time = time(NULL);
    stbuf->st_mode = S_IFLNK | 0755;
    stbuf->st_nlink = 1;
    stbuf->st_atime = stbuf->st_ctime = stbuf->st_mtime = current_time;
    int len = snprintf(the_name, MAXNAMLEN, "../%u", Pid);
    the_stat.st_size = len;

    return 0;
}

GETATTR_HANDLER(system__hardware__displays__display)
{
    unsigned long index = strtol(argv[0], NULL, 10);
    CGDisplayCount display_count = PROCFS_GetDisplayCount();

    if (index >= display_count) {
        return -ENOENT;
    }

    time_t current_time = time(NULL);

    stbuf->st_mode = S_IFDIR | 0555;
    stbuf->st_nlink = 1;
    stbuf->st_size = 0;
    stbuf->st_atime = stbuf->st_ctime = stbuf->st_mtime = current_time;
    
    return 0;
}

GETATTR_HANDLER(system__hardware__camera__screenshot)
{
    time_t current_time = time(NULL);

    stbuf->st_mode = S_IFREG | 0444;
    stbuf->st_nlink = 1;
    stbuf->st_atime = stbuf->st_ctime = stbuf->st_mtime = current_time;
    stbuf->st_size = PROCFS_GetTIFFSizeFromCamera();

    return 0;
}

GETATTR_HANDLER(system__hardware__displays__display__screenshot)
{
    unsigned long index = strtol(argv[0], NULL, 10);

    time_t current_time = time(NULL);

    stbuf->st_mode = S_IFREG | 0444;
    stbuf->st_nlink = 1;
    stbuf->st_atime = stbuf->st_ctime = stbuf->st_mtime = current_time;
    stbuf->st_size = PROCFS_GetPNGSizeForDisplayAtIndex(index);

    return 0;
}

#if OSXFUSE_PROCFS_ENABLE_TPM
GETATTR_HANDLER(system__hardware__tpm__keyslots__slot)
{
    uint32_t keys[256];
    unsigned long slotno = strtol(argv[0], NULL, 10);

    uint16_t slots_used = 0;
    uint32_t slots_free = 0;
    uint32_t slots_total = 0;

    if (TPM_GetCapability_Slots(&slots_free)) {
        return -ENOENT;
    }

    if (TPM_GetCapability_Key_Handle(&slots_used, keys)) {
        return -ENOENT;
    }

    slots_total = slots_used + slots_free;

    if (slotno >= slots_total) {
        return -ENOENT;
    }

    time_t current_time = time(NULL);
    stbuf->st_nlink = 1;
    stbuf->st_size = 9;
    stbuf->st_atime = stbuf->st_ctime = stbuf->st_mtime = current_time;

    if (slotno >= slots_used) {
        stbuf->st_mode = S_IFREG | 0000;
    } else {
        stbuf->st_mode = S_IFREG | 0444;
    }

    return 0;
}

GETATTR_HANDLER(system__hardware__tpm__pcrs__pcr)
{
    time_t current_time = time(NULL);
    stbuf->st_mode = S_IFREG | 0444;
    stbuf->st_nlink = 1;
    stbuf->st_size = 60;
    stbuf->st_atime = stbuf->st_ctime = stbuf->st_mtime = current_time;

    return 0;
}  
#endif /* OSXFUSE_PROCFS_ENABLE_TPM */

GETATTR_HANDLER(proc__task__ports__port)
{
    kern_return_t kr;
    task_t the_task = MACH_PORT_NULL;
    pid_t pid = strtol(argv[0], NULL, 10);

    kr = task_for_pid(mach_task_self(), pid, &the_task);
    if (kr != KERN_SUCCESS) {
        return -ENOENT;
    }

    DECL_PORT_LIST();
    INIT_PORT_LIST(the_task);

    int found = 0;
    unsigned int i;
    unsigned int the_port_name = strtoul(argv[1], NULL, 16);

    for (i = 0; i < name_count; i++) {
        if (the_port_name == name_list[i]) {
            found = 1;
            break;
        }
    }

    FINI_PORT_LIST();

    if (the_task != MACH_PORT_NULL) {
        mach_port_deallocate(mach_task_self(), the_task);
    }

    if (!found) {
        return -ENOENT;
    }

    time_t current_time = time(NULL);

    stbuf->st_mode = S_IFDIR | 0555;
    stbuf->st_nlink = 1;
    stbuf->st_size = 0;
    stbuf->st_atime = stbuf->st_ctime = stbuf->st_mtime = current_time;
    
    return 0;
}

GETATTR_HANDLER(proc__task__threads__thread)
{
    kern_return_t kr;
    task_t the_task = MACH_PORT_NULL;
    pid_t pid = strtol(argv[0], NULL, 10);

    kr = task_for_pid(mach_task_self(), pid, &the_task);
    if (kr != KERN_SUCCESS) {
        return -ENOENT;
    }

    DECL_THREAD_LIST();
    INIT_THREAD_LIST(the_task);
    FINI_THREAD_LIST();

    if (the_task != MACH_PORT_NULL) {
        mach_port_deallocate(mach_task_self(), the_task);
    }

    unsigned int the_thread_name = strtoul(argv[1], NULL, 16);
    if (the_thread_name >= thread_count) {
        return -ENOENT;
    }

    time_t current_time = time(NULL);

    stbuf->st_mode = S_IFDIR | 0555;
    stbuf->st_nlink = 1;
    stbuf->st_size = 0;
    stbuf->st_atime = stbuf->st_ctime = stbuf->st_mtime = current_time;
    
    return 0;
}

GETATTR_HANDLER(proc__windows__screenshots__window)
{
    pid_t pid = strtol(argv[0], NULL, 10);
    CGWindowID target = strtol(argv[1], NULL, 16);
    ProcessSerialNumber psn;

    OSStatus status = GetProcessForPID(pid, &psn);
    if (status != noErr) {
        return 0; /* technically not an error in this case */
    }

    CGSConnectionID conn;
    CGError err = CGSGetConnectionIDForPSN(0, &psn, &conn);
    if (err != kCGErrorSuccess) {
        return 0; /* just be nice */
    }

#define MAX_WINDOWS 256
    CGSWindowID windowIDs[MAX_WINDOWS];
    int windowCount = 0;
    int i = 0;

    err = CGSGetWindowList(_CGSDefaultConnection(), conn, MAX_WINDOWS,
                           windowIDs, &windowCount);

    for (i = 0; i < windowCount; i++) {
        if (windowIDs[i] == target) {
            time_t current_time = time(NULL);

            stbuf->st_mode = S_IFREG | 0444;
            stbuf->st_nlink = 1;
            stbuf->st_atime = stbuf->st_ctime = stbuf->st_mtime = current_time;
            stbuf->st_size = PROCFS_GetPNGSizeForWindowAtIndex(windowIDs[i]);
            return 0;
        }
    }

    return -ENOENT;
}

// END: GETATTR


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <sys/sysctl.h>

int
procinfo(pid_t pid, struct kinfo_proc *kp)
{
    int mib[4];
    size_t bufsize = 0, orig_bufsize = 0;
    struct kinfo_proc *kprocbuf;
    int retry_count = 0;
    int local_error;

    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = KERN_PROC_PID;
    mib[3] = pid;

    kprocbuf = kp;
    orig_bufsize = bufsize = sizeof(struct kinfo_proc);
    for (retry_count = 0; ; retry_count++) {
        local_error = 0;
        bufsize = orig_bufsize;
        if ((local_error = sysctl(mib, 4, kp, &bufsize, NULL, 0)) < 0) {
            if (retry_count < 1000) {
                sleep(1);
                continue;
            }
            return local_error;
        } else if (local_error == 0) {
            break;
        }
        sleep(1);
    }

    return local_error;
}

int
getproccmdline(pid_t pid, char *cmdlinebuf, int len)
{
    int i, mib[4], rlen, tlen, thislen;
    int    argmax, target_argc;
    char *target_argv, *target_argv_end;
    char  *cp;
    size_t size;

    if (pid == 0) {
        return snprintf(cmdlinebuf, len, "kernel\n");
    }

    mib[0] = CTL_KERN;
    mib[1] = KERN_ARGMAX;

    size = sizeof(argmax);
    if (sysctl(mib, 2, &argmax, &size, NULL, 0) == -1) {
        return -1;
    }

    target_argv = (char *)malloc(argmax);
    if (target_argv == NULL) {
        return -1;
    }

    target_argv_end = target_argv + argmax;

    mib[0] = CTL_KERN;
    mib[1] = KERN_PROCARGS2;
    mib[2] = pid;

    size = (size_t)argmax;
    if (sysctl(mib, 3, target_argv, &size, NULL, 0) == -1) {
        free(target_argv);
        return -1;
    }

    memcpy(&target_argc, target_argv, sizeof(target_argc));
    cp = target_argv + sizeof(target_argc);
    cp += strlen(cp) + 1; // saved exec path
    rlen = len;
    tlen = 0;
    for (i = 1; i < target_argc + 1; i++) {
        while (cp < target_argv_end && *cp == '\0')
            cp++;
        if (cp == target_argv_end) {
            /*
             * We've reached the end of target_argv without finding target_argc
             * arguments. This can happen when a process changes its argv.
             * Reported by Francis Devereux.
             */
            break;
        }
        thislen = snprintf(cmdlinebuf + tlen, rlen, "%s ", cp);
        tlen += thislen; 
        rlen -= thislen;
        if (rlen <= 0) {
            break;
        }
        cp += strlen(cp) + 1;
    }
    if (rlen > 0) {
        thislen = snprintf(cmdlinebuf + tlen, rlen, "\n");
        tlen += thislen;
        rlen -= thislen;
    }
    return tlen;
}

// BEGIN: READ

//    int
//    procfs_read_<handler>(procfs_dispatcher_entry_t  e,
//                          const char                *argv[],
//                          void                      *buf,
//                          size_t                     size,
//                          off_t                      offset,
//                          struct fuse_file_info     *fi)


READ_HANDLER(einval)
{
    return -EINVAL;
}

READ_HANDLER(eisdir)
{
    return -EISDIR;
}

READ_HANDLER(zero)
{
    return 0;
}

READ_HANDLER(default_file_finder_info)
{
    if (!procfs_ui) {
        return -ENOENT;
    }

    char tmpbuf[] = {
        0x0, 0x5, 0x16, 0x7, 0x0, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
        0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x2,
        0x0, 0x0, 0x0, 0x9, 0x0, 0x0, 0x0, 0x32, 0x0, 0x0, 0x0, 0x20, 0x0,
        0x0, 0x0, 0x2, 0x0, 0x0, 0x0, 0x52, 0x0, 0x0, 0x0, 0x0, 0x54, 0x45,
       0x58, 0x54, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
        0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
        0x0, 0x0, 0x0, 0x0,
    };
    int len = 82;

    if (offset < len) {
        if (offset + size > len)
            size = len - offset;
        memcpy(buf, tmpbuf + offset, size);
    } else             
        size = 0;

    return size;
}

READ_HANDLER(system__firmware__variables)
{
    io_registry_entry_t    options;
    CFMutableDictionaryRef optionsDict;
    kern_return_t          kr = KERN_FAILURE;

    options = IORegistryEntryFromPath(kIOMasterPortDefault,
                                      kIODeviceTreePlane ":/options");
    if (options) {
        kr = IORegistryEntryCreateCFProperties(options, &optionsDict, 0, 0);
        if (kr == KERN_SUCCESS) {
            CFDataRef xml = CFPropertyListCreateXMLData(kCFAllocatorDefault,
                                (CFPropertyListRef)optionsDict);

            int len = CFDataGetLength(xml);
            if (len < 0) {
                kr = KERN_FAILURE;
                goto done;
            }

            {
                const UInt8 *tmpbuf = CFDataGetBytePtr(xml);
   
                if (offset < len) {
                    if (offset + size > len)
                        size = len - offset;
                    memcpy(buf, tmpbuf + offset, size);
                } else
                    size = 0;
            }
done:
            CFRelease(xml);
            CFRelease(optionsDict);
        }
        IOObjectRelease(options);
    }
   
    if (kr != KERN_SUCCESS) {
        return -EIO;
    }

    return size;
}

READ_HANDLER(system__hardware__cpus__cpu__data)
{
    int len;
    kern_return_t kr;
    char tmpbuf[4096];
    const char *whichfile = argv[0];
    unsigned int whichcpu = atoi(whichfile);

    if (whichcpu >= processor_count) {
        return -ENOENT;
    }

    processor_basic_info_data_t    basic_info;
    processor_cpu_load_info_data_t cpu_load_info;
    natural_t                      info_count;
    host_name_port_t               myhost = mach_host_self();

    info_count = PROCESSOR_BASIC_INFO_COUNT;
    kr = processor_info(processor_list[whichcpu], PROCESSOR_BASIC_INFO,
                        &myhost, (processor_info_t)&basic_info, &info_count);
    if (kr != KERN_SUCCESS) {
        return -EIO;
    }
    info_count = PROCESSOR_CPU_LOAD_INFO_COUNT;
    kr = processor_info(processor_list[whichcpu], PROCESSOR_CPU_LOAD_INFO,
                        &myhost, (processor_info_t)&cpu_load_info, &info_count);
    if (kr != KERN_SUCCESS) {
        return -EIO;
    }

    len = 0;
    unsigned long ticks;

    len += snprintf(tmpbuf + len, 4096 - len, "slot %d%s%s",
                    basic_info.slot_num,
                    (basic_info.is_master) ? " (master)," : ",",
                    (basic_info.running) ? " running\n" : " not running\n");
    len += snprintf(tmpbuf + len, 4096 - len, "type %d, subtype %d\n",
                    basic_info.cpu_type, basic_info.cpu_subtype);

    ticks = cpu_load_info.cpu_ticks[CPU_STATE_USER] +
            cpu_load_info.cpu_ticks[CPU_STATE_SYSTEM] +
            cpu_load_info.cpu_ticks[CPU_STATE_IDLE] +
            cpu_load_info.cpu_ticks[CPU_STATE_NICE];
    len += snprintf(tmpbuf + len, 4096 - len,
                    "%ld ticks (user %u system %u idle %u nice %u)\n",
                    ticks,
                    cpu_load_info.cpu_ticks[CPU_STATE_USER],
                    cpu_load_info.cpu_ticks[CPU_STATE_SYSTEM],
                    cpu_load_info.cpu_ticks[CPU_STATE_IDLE],
                    cpu_load_info.cpu_ticks[CPU_STATE_NICE]);
    len += snprintf(tmpbuf + len, 4096 - len, "cpu uptime %ldh %ldm %lds\n",
                    (ticks / 100) / 3600, ((ticks / 100) % 3600) / 60,
                    (ticks / 100) % 60);

    if (len < 0) {
        return -EIO;
    }
    
    if (offset < len) {
        if (offset + size > len)
            size = len - offset;
        memcpy(buf, tmpbuf + offset, size);
    } else             
        size = 0;
    
    return size;
}

READ_HANDLER(system__hardware__displays__display__info)
{
    char tmpbuf[4096];
    unsigned long index = strtol(argv[0], NULL, 10);
    CGDisplayCount display_count = PROCFS_GetDisplayCount();

    if (index >= display_count) {
        return -ENOENT;
    }

    size_t len = 4096;
    int ret = PROCFS_GetInfoForDisplayAtIndex(index, tmpbuf, &len);
    if (ret) {
        return -EIO;
    }

    if (offset < len) {
        if (offset + size > len)
            size = len - offset;
        memcpy(buf, tmpbuf + offset, size);
    } else           
        size = 0;

    return size;
}

READ_HANDLER(system__hardware__camera__screenshot)
{
    size_t max_len = PROCFS_GetTIFFSizeFromCamera();
    CFIndex len = CFDataGetLength(camera_tiff);

    if (len > max_len) {
        return -EIO;
    }

    CFDataSetLength(camera_tiff, max_len);

    const UInt8 *tmpbuf = CFDataGetBytePtr(camera_tiff);
        
    if (len < 0) {
        return -EIO; 
    }

    if (offset < len) {
        if (offset + size > len)
            size = len - offset;
        memcpy(buf, tmpbuf + offset, size);
    } else
        size = 0;

    return size;
}

READ_HANDLER(system__hardware__displays__display__screenshot)
{
    unsigned long index = strtol(argv[0], NULL, 10);
    CGDisplayCount display_count = PROCFS_GetDisplayCount();

    if (index >= display_count) {
        return -ENOENT;

    }
    pthread_mutex_lock(&display_lock);
    if (!display_png) {
        pthread_mutex_unlock(&display_lock);
        return -EIO;
    }
    CFRetain(display_png);
    pthread_mutex_unlock(&display_lock);

    size_t max_len = PROCFS_GetPNGSizeForDisplayAtIndex(index);
    CFIndex len = (size_t)CFDataGetLength(display_png);

    if (len > max_len) {
        pthread_mutex_lock(&display_lock);
        CFRelease(display_png);
        pthread_mutex_unlock(&display_lock);
        return -EIO;
    }

    CFDataSetLength(display_png, max_len);
    len = max_len;
    const UInt8 *tmpbuf = CFDataGetBytePtr(display_png);

    if (len < 0) {
        pthread_mutex_lock(&display_lock);
        CFRelease(display_png);
        pthread_mutex_unlock(&display_lock);
        return -EIO;
    }

    if (offset < len) {
        if (offset + size > len)
            size = len - offset;
        memcpy(buf, tmpbuf + offset, size);
    } else
        size = 0;

    pthread_mutex_lock(&display_lock);
    CFRelease(display_png);
    pthread_mutex_unlock(&display_lock);

    return size;
}

typedef struct {
    const char  *classname;
    unsigned int index;
    IOItemCount  structureInputSize;
    IOByteCount  structureOutputSize;
} sms_configuration_t;

sms_configuration_t
SMS_CONFIGURATIONS[] = {
    { "SMCMotionSensor",    5, 40, 40 }, // 0
    { "PMUMotionSensor",   21, 60, 60 }, // 1
    { "IOI2CMotionSensor", 21, 60, 60 }, // 2
    { NULL,                -1,  0,  0 },
};

enum { sms_maxConfigurationID = 2 };

static int sms_configurationID = -1;

READ_HANDLER(system__hardware__xsensor)
{
    int len = -1;
    kern_return_t kr;
    char tmpbuf[4096];
    const char *whichfile = argv[0];

    if (strcmp(whichfile, "lightsensor") == 0) {
        unsigned int  gIndex = 0;
        uint64_t      scalarOutputValues[2];
        uint32_t      scalarOutputCount = 2;
        if (lightsensor_port == 0) {
            len = snprintf(tmpbuf, 4096, "not available\n");
            goto gotdata;
        }
        kr = IOConnectCallScalarMethod(lightsensor_port, gIndex, NULL, 0, 
                                       scalarOutputValues, &scalarOutputCount);
        if (kr == KERN_SUCCESS) {
            len = snprintf(tmpbuf, 4096, "%llu %llu\n", 
                           scalarOutputValues[0], scalarOutputValues[1]);
        } else if (kr == kIOReturnBusy) {
            len = snprintf(tmpbuf, 4096, "busy\n");
        } else {
            len = snprintf(tmpbuf, 4096, "error %d\n", kr);
        }
        goto gotdata;
    }

    if (strcmp(whichfile, "motionsensor") == 0) {
        MotionSensorData_t sms_data;
        if (motionsensor_port == 0) {
            len = snprintf(tmpbuf, 4096, "not available\n");
            goto gotdata;
        }
        kr = sms_getOrientation_hardware_apple(&sms_data);
        if (kr != KERN_SUCCESS) {
            len = snprintf(tmpbuf, 4096, "error %d\n", kr);
        } else {
            len = snprintf(tmpbuf, 4096, "%hhd %hhd %hhd\n",
                           sms_data.x, sms_data.y, sms_data.z);
        }
        goto gotdata;
    }

    if (strcmp(whichfile, "mouse") == 0) {
        HIPoint mouselocation = { 0.0, 0.0 };
        (void)HIGetMousePosition(kHICoordSpaceScreenPixel,
                                 NULL, &mouselocation);
        len = snprintf(tmpbuf, 4096, "%.0f %.0f\n",
                       mouselocation.x, mouselocation.y);
        goto gotdata;
    }

gotdata:

    if (len < 0) {
        return -EIO;
    }
    
    if (offset < len) {
        if (offset + size > len)
            size = len - offset;
        memcpy(buf, tmpbuf + offset, size);
    } else             
        size = 0;
    
    return size;
}

/*
 * To make the tpm stuff work, you need to:
 *
 * 1. Get the Mac OS X TPM device driver from
 *    http://osxbook.com/book/bonus/chapter10/tpm/
 *
 * 2. Get IBM's libtpm user-space library.
 *
 * 3. Define OSXFUSE_PROCFS_ENABLE_TPM to 1, compile procfs.cc, and link with
 *    libtpm.
 */

#if OSXFUSE_PROCFS_ENABLE_TPM
READ_HANDLER(system__hardware__tpm__hwmodel)
{
    int len;
    char tmpbuf[4096];

    len = snprintf(tmpbuf, 4096, "%s\n", "SLB 9635 TT 1.2");

    if (len <= 0) {
        return -EIO;
    }

    if (offset < len) {
        if (offset + size > len)
            size = len - offset;
        memcpy(buf, tmpbuf + offset, size);
    } else
        size = 0;

    return size;
}

READ_HANDLER(system__hardware__tpm__hwvendor)
{
    int len;
    char tmpbuf[4096];

    len = snprintf(tmpbuf, 4096, "%s\n", "Infineon");

    if (len <= 0) {
        return -EIO;
    }

    if (offset < len) {
        if (offset + size > len)
            size = len - offset;
        memcpy(buf, tmpbuf + offset, size);
    } else
        size = 0;

    return size;
}

READ_HANDLER(system__hardware__tpm__hwversion)
{
    int major, minor, version, rev, len;
    char tmpbuf[4096];

    if (TPM_GetCapability_Version(&major, &minor, &version, &rev)) {
        return -EIO;
    }

    len = snprintf(tmpbuf, 4096, "%d.%d.%d.%d\n", major, minor, version, rev);

    if (len <= 0) {
        return -EIO;
    }

    if (offset < len) {
        if (offset + size > len)
            size = len - offset;
        memcpy(buf, tmpbuf + offset, size);
    } else
        size = 0;

    return size;
}

READ_HANDLER(system__hardware__tpm__keyslots__slot)
{
    char tmpbuf[32] = { 0 };
    int len;
    uint32_t keys[256];
    unsigned long slotno = strtol(argv[0], NULL, 10);

    uint16_t slots_used = 0;
    uint32_t slots_free = 0;
    uint32_t slots_total = 0;

    if (TPM_GetCapability_Slots(&slots_free)) {
        return -ENOENT;
    }

    if (TPM_GetCapability_Key_Handle(&slots_used, keys)) {
        return -ENOENT;
    }

    slots_total = slots_used + slots_free;

    if (slotno >= slots_used) {
        return -EIO;
    }

    len = snprintf(tmpbuf, 32, "%08x\n", keys[slotno]);
    if (offset < len) {
        if (offset + size > len)
            size = len - offset;
        memcpy(buf, tmpbuf + offset, size);
    } else {
        size = 0;
    }

    return size;
}

READ_HANDLER(system__hardware__tpm__pcrs__pcr)
{
    uint32_t pcrs, the_pcr;
    unsigned char pcr_data[20];
    char tmpbuf[4096] = { 0 };
    int len, i;

    if (TPM_GetCapability_Pcrs(&pcrs)) {
        return -EIO;
    }

    the_pcr = strtol(argv[0], NULL, 10);
    if ((the_pcr < 0) || (the_pcr >= pcrs)) {
        return -ENOENT;
    }

    if (TPM_PcrRead(the_pcr, pcr_data)) {
        return -EIO;
    }

    len = 0;
    for (i = 0; i < 20; i++) {
        len += snprintf(tmpbuf + len, 4096 - len, "%02x ", pcr_data[i]);
    }
    tmpbuf[len - 1] = '\n';

    if (offset < len) {
        if (offset + size > len)
            size = len - offset;
        memcpy(buf, tmpbuf + offset, size);
    } else {
        size = 0;
    }

    return size;
}
#endif /* OSXFUSE_PROCFS_ENABLE_TPM */

READ_HANDLER(proc__carbon)
{
    OSStatus status = procNotFound;
    ProcessSerialNumber psn;
    CFStringRef nameRef;
    char tmpbuf[4096];
    int len = -1;

    pid_t pid = atoi(argv[0]);
    const char *whichfile = argv[1];

    if (pid <= 0) {
        return 0;
    }

    status = GetProcessForPID(pid, &psn);

    if (status != noErr) {
        return 0;
    }

    if (strcmp(whichfile, "psn") == 0) {
        len = snprintf(tmpbuf, 4096, "%lu:%lu\n",
                       psn.highLongOfPSN, psn.lowLongOfPSN);
        goto gotdata;
    }

    if (strcmp(whichfile, "name") == 0) {
        status = CopyProcessName(&psn, &nameRef);
        CFStringGetCString(nameRef, tmpbuf, 4096, kCFStringEncodingASCII);
        len = snprintf(tmpbuf, 4096, "%s\n", tmpbuf);
        CFRelease(nameRef);
        goto gotdata;
    }

    return -ENOENT;

gotdata:

    if (len < 0) {
        return -EIO;
    }   
        
    if (offset < len) {
        if (offset + size > len)
            size = len - offset;
        memcpy(buf, tmpbuf + offset, size);
    } else
        size = 0;

    return size;
}

#if __i386__
READ_HANDLER(proc__fds)
{
    pid_t pid = atoi(argv[0]);
    char tmpbuf[65536];
    int len = 65536;

    int ret = procfs_proc_pidinfo(pid, tmpbuf, &len);

    if (ret) {
        return -EIO;
    }

    if (offset < len) {
        if (offset + size > len)
            size = len - offset;
        memcpy(buf, tmpbuf + offset, size);
    } else
        size = 0;

    return size;
}
#endif /* __i386__ */

#define HANDLE_GENERIC_ITEM(item, fmt, datasrc)     \
    if (strcmp(whichfile, item) == 0) {             \
        len = snprintf(tmpbuf, 4096, fmt, datasrc); \
        goto gotdata;                               \
    }   

READ_HANDLER(proc__generic)
{
    pid_t pid = atoi(argv[0]);
    const char *whichfile = argv[1];
    struct kinfo_proc kp;
    int len;
    char tmpbuf[4096];

    len = procinfo(pid, &kp);
    if (len != 0) {
        return -EIO;
    }

    len = -1;

    if (strcmp(whichfile, "cmdline") == 0) {
        len = getproccmdline(pid, tmpbuf, 4096);
        goto gotdata;
    }

    if (strcmp(whichfile, "tdev") == 0) {
        char *dr = devname_r(kp.kp_eproc.e_tdev, S_IFCHR, tmpbuf, 4096);
        if (!dr) {
            len = snprintf(tmpbuf, 4096, "none\n");
        } else {
            len = snprintf(tmpbuf, 4096, "%s\n", dr);
        }
        goto gotdata;
    }

    HANDLE_GENERIC_ITEM("jobc",    "%hd\n", kp.kp_eproc.e_jobc);
    HANDLE_GENERIC_ITEM("paddr",   "%p\n",  kp.kp_eproc.e_paddr);
    HANDLE_GENERIC_ITEM("pgid",    "%d\n",  kp.kp_eproc.e_pgid);
    HANDLE_GENERIC_ITEM("ppid",    "%d\n",  kp.kp_eproc.e_ppid);
    HANDLE_GENERIC_ITEM("wchan",   "%s\n",
                        (kp.kp_eproc.e_wmesg[0]) ? kp.kp_eproc.e_wmesg : "-");
    HANDLE_GENERIC_ITEM("tpgid",   "%d\n",  kp.kp_eproc.e_tpgid);

gotdata:

    if (len < 0) {
        return -EIO;
    }

    if (offset < len) {
        if (offset + size > len)
            size = len - offset;
        memcpy(buf, tmpbuf + offset, size);
    } else
        size = 0;

    return size;
}

#define READ_PROC_TASK_PROLOGUE()                          \
    int len = -1;                                          \
    kern_return_t kr;                                      \
    char tmpbuf[4096];                                     \
    task_t the_task = MACH_PORT_NULL;                      \
    pid_t pid = strtol(argv[0], NULL, 10);                 \
                                                           \
    kr = task_for_pid(mach_task_self(), pid, &the_task);   \
    if (kr != KERN_SUCCESS) {                              \
        return -EIO;                                       \
    }

#define READ_PROC_TASK_EPILOGUE()                          \
                                                           \
    if (the_task != MACH_PORT_NULL) {                      \
        mach_port_deallocate(mach_task_self(), the_task);  \
    }                                                      \
                                                           \
    if (len < 0) {                                         \
        return -EIO;                                       \
    }                                                      \
                                                           \
    if (offset < len) {                                    \
        if (offset + size > len)                           \
            size = len - offset;                           \
        memcpy(buf, tmpbuf + offset, size);                \
    } else                                                 \
        size = 0;                                          \
                                                           \
    return size;

static const char *thread_policies[] = {
    "UNKNOWN?",
    "STANDARD|EXTENDED",
    "TIME_CONSTRAINT",
    "PRECEDENCE",
};
#define THREAD_POLICIES_MAX (int)(sizeof(thread_policies)/sizeof(char *))

READ_HANDLER(proc__task__absolutetime_info)
{
    READ_PROC_TASK_PROLOGUE();

    task_info_data_t tinfo;
    mach_msg_type_number_t task_info_count;
    task_absolutetime_info_t absolutetime_info;

    task_info_count = TASK_INFO_MAX;
    kr = task_info(the_task, TASK_ABSOLUTETIME_INFO, (task_info_t)tinfo,
                   &task_info_count);
    if (kr != KERN_SUCCESS) {
        return -EIO;
    }

    const char *whichfile = argv[1];
    absolutetime_info = (task_absolutetime_info_t)tinfo;

    if (strcmp(whichfile, "threads_system") == 0) {
        len  = snprintf(tmpbuf, 4096, "%lld\n",
                        absolutetime_info->threads_system);
        goto gotdata;
    }

    if (strcmp(whichfile, "threads_user") == 0) {
        len  = snprintf(tmpbuf, 4096, "%lld\n",
                        absolutetime_info->threads_user);
        goto gotdata;
    }

    if (strcmp(whichfile, "total_system") == 0) {
        len  = snprintf(tmpbuf, 4096, "%lld\n",
                        absolutetime_info->total_system);
        goto gotdata;
    }

    if (strcmp(whichfile, "total_user") == 0) {
        len  = snprintf(tmpbuf, 4096, "%lld\n",
                        absolutetime_info->total_user);
        goto gotdata;
    }

gotdata:

    READ_PROC_TASK_EPILOGUE();
}

READ_HANDLER(proc__task__basic_info)
{
    READ_PROC_TASK_PROLOGUE();

    task_info_data_t tinfo;
    mach_msg_type_number_t task_info_count;
    task_basic_info_t basic_info;

    task_info_count = TASK_INFO_MAX;
    kr = task_info(the_task, TASK_BASIC_INFO, (task_info_t)tinfo,
                   &task_info_count);
    if (kr != KERN_SUCCESS) {
        return -EIO;
    }

    basic_info = (task_basic_info_t)tinfo;
    const char *whichfile = argv[1];

    if (strcmp(whichfile, "policy") == 0) {
        if ((basic_info->policy < 0) &&
            (basic_info->policy > THREAD_POLICIES_MAX)) {
            basic_info->policy = 0;
        }
        len = snprintf(tmpbuf, 4096, "%s\n",
                       thread_policies[basic_info->policy]);
        goto gotdata;
    }

    if (strcmp(whichfile, "resident_size") == 0) {
        len = snprintf(tmpbuf, 4096, "%u KB\n",
                       basic_info->resident_size >> 10);
        goto gotdata;
    }

    if (strcmp(whichfile, "suspend_count") == 0) {
        len = snprintf(tmpbuf, 4096, "%u\n", basic_info->suspend_count);
        goto gotdata;
    }

    if (strcmp(whichfile, "system_time") == 0) {
        len = snprintf(tmpbuf, 4096, "%us %uus\n",
                       basic_info->system_time.seconds,
                       basic_info->system_time.microseconds);
        goto gotdata;
    }

    if (strcmp(whichfile, "user_time") == 0) {
        len = snprintf(tmpbuf, 4096, "%us %uus\n",
                       basic_info->user_time.seconds,
                       basic_info->user_time.microseconds);
        goto gotdata;
    }

    if (strcmp(whichfile, "virtual_size") == 0) {
        len = snprintf(tmpbuf, 4096, "%u KB\n", basic_info->virtual_size >> 10);
        goto gotdata;
    }

gotdata:

    READ_PROC_TASK_EPILOGUE();
}

READ_HANDLER(proc__task__events_info)
{
    READ_PROC_TASK_PROLOGUE();

    task_info_data_t tinfo;
    mach_msg_type_number_t task_info_count;
    task_events_info_t events_info;

    task_info_count = TASK_INFO_MAX;
    kr = task_info(the_task, TASK_EVENTS_INFO, (task_info_t)tinfo,
                   &task_info_count);
    if (kr != KERN_SUCCESS) {
        return -EIO;
    }

    const char *whichfile = argv[1];
    events_info = (task_events_info_t)tinfo;

#define HANDLE_TASK_EVENT_ITEM(item, datasrc)          \
    if (strcmp(whichfile, item) == 0) {                \
        len = snprintf(tmpbuf, 4096, "%u\n", datasrc); \
        goto gotdata;                                  \
    }

    HANDLE_TASK_EVENT_ITEM("cow_faults", events_info->cow_faults);
    HANDLE_TASK_EVENT_ITEM("csw", events_info->csw);
    HANDLE_TASK_EVENT_ITEM("faults", events_info->faults);
    HANDLE_TASK_EVENT_ITEM("messages_received", events_info->messages_received);
    HANDLE_TASK_EVENT_ITEM("messages_sent", events_info->messages_sent);
    HANDLE_TASK_EVENT_ITEM("pageins", events_info->pageins);
    HANDLE_TASK_EVENT_ITEM("syscalls_mach", events_info->syscalls_mach);
    HANDLE_TASK_EVENT_ITEM("syscalls_unix", events_info->syscalls_unix);

    return -EIO;

gotdata:

    READ_PROC_TASK_EPILOGUE();
}

READ_HANDLER(proc__task__thread_times_info)
{
    READ_PROC_TASK_PROLOGUE();

    task_info_data_t tinfo;
    mach_msg_type_number_t task_info_count;
    task_thread_times_info_t thread_times_info;

    task_info_count = TASK_INFO_MAX;
    kr = task_info(the_task, TASK_THREAD_TIMES_INFO, (task_info_t)tinfo,
                   &task_info_count);
    if (kr != KERN_SUCCESS) {
        return -EIO;
    }

    const char *whichfile = argv[1];
    thread_times_info = (task_thread_times_info_t)tinfo;
    
    if (strcmp(whichfile, "system_time") == 0) {
        len = snprintf(tmpbuf, 4096, "%us %uus\n",
                       thread_times_info->system_time.seconds,
                       thread_times_info->system_time.microseconds);
        goto gotdata;
    }

    if (strcmp(whichfile, "user_time") == 0) {
        len = snprintf(tmpbuf, 4096, "%us %uus\n",
                       thread_times_info->user_time.seconds,
                       thread_times_info->user_time.microseconds);
        goto gotdata;
    }

    return -ENOENT;

gotdata:

    READ_PROC_TASK_EPILOGUE();
}

READ_HANDLER(proc__task__mach_name)
{
    READ_PROC_TASK_PROLOGUE();

    len = snprintf(tmpbuf, 4096, "%x\n", the_task);
   
    READ_PROC_TASK_EPILOGUE();
}

READ_HANDLER(proc__task__ports__port)
{
    READ_PROC_TASK_PROLOGUE();
    DECL_PORT_LIST();
    INIT_PORT_LIST(the_task);
    unsigned int i;
    char the_name[MAXNAMLEN + 1];
    mach_port_t the_port = MACH_PORT_NULL;
    mach_port_type_t the_type = 0;
    for (i = 0; i < name_count; i++) {
        snprintf(the_name, MAXNAMLEN, "%x", name_list[i]);
        if (strcmp(the_name, argv[1]) == 0) {
            the_port = name_list[i];
            the_type = type_list[i];
            break;
        }
    }

    if (the_port == MACH_PORT_NULL) {
        FINI_PORT_LIST();
        return -ENOENT;
    }

    const char *whichfile = argv[2];
    mach_msg_type_number_t port_info_count;

    if (strcmp(whichfile, "task_rights") == 0) {
        len = 0;
        if (the_type & MACH_PORT_TYPE_SEND) {
             len += snprintf(tmpbuf + len, 4096 - len, "%s ", "SEND");
        } 
        if (the_type & MACH_PORT_TYPE_RECEIVE) {
             len += snprintf(tmpbuf + len, 4096 - len, "%s ", "RECEIVE");
        } 
        if (the_type & MACH_PORT_TYPE_SEND_ONCE) {
             len += snprintf(tmpbuf + len, 4096 - len, "%s ", "SEND_ONCE");
        } 
        if (the_type & MACH_PORT_TYPE_PORT_SET) {
             len += snprintf(tmpbuf + len, 4096 - len, "%s ", "PORT_SET");
        } 
        if (the_type & MACH_PORT_TYPE_DEAD_NAME) {
             len += snprintf(tmpbuf + len, 4096 - len, "%s ", "DEAD_NAME");
        } 
        if (the_type & MACH_PORT_TYPE_DNREQUEST) {
             len += snprintf(tmpbuf + len, 4096 - len, "%s ", "DNREQUEST");
        } 
        len += snprintf(tmpbuf + len, 4096 - len, "\n");
        goto gotdata;
    }

    mach_port_limits_t port_limits;
    mach_port_status_t port_status;

    port_info_count = MACH_PORT_LIMITS_INFO_COUNT;
    kr = mach_port_get_attributes(the_task, the_port, MACH_PORT_LIMITS_INFO,
                                  (mach_port_info_t)&port_limits,
                                  &port_info_count);

    if ((kr != KERN_SUCCESS) && (kr != KERN_INVALID_RIGHT)) {
        FINI_PORT_LIST();
        return -EIO;
    }

    if (strcmp(whichfile, "qlimit") == 0) {
        if (kr == KERN_SUCCESS) {
            len = snprintf(tmpbuf, 4096, "%d\n", port_limits.mpl_qlimit);
        } else {
            len = snprintf(tmpbuf, 4096, "-\n");
        }
        goto gotdata;
    }

    port_info_count = MACH_PORT_RECEIVE_STATUS_COUNT;
    kr = mach_port_get_attributes(the_task, the_port, MACH_PORT_RECEIVE_STATUS,
                                  (mach_port_info_t)&port_status,
                                  &port_info_count);
    if ((kr != KERN_SUCCESS) && (kr != KERN_INVALID_RIGHT)) {
        FINI_PORT_LIST();
        return -EIO;
    }

    if (strcmp(whichfile, "msgcount") == 0) {
        if (kr == KERN_SUCCESS) {
            len = snprintf(tmpbuf, 4096, "%d\n", port_status.mps_msgcount);
        } else {
            len = snprintf(tmpbuf, 4096, "-\n");
        }
        goto gotdata;
    }

    if (strcmp(whichfile, "seqno") == 0) {
        if (kr == KERN_SUCCESS) {
            len = snprintf(tmpbuf, 4096, "%d\n", port_status.mps_seqno);
        } else {
            len = snprintf(tmpbuf, 4096, "-\n");
        }
        goto gotdata;
    }

    if (strcmp(whichfile, "sorights") == 0) {
        if (kr == KERN_SUCCESS) {
            len = snprintf(tmpbuf, 4096, "%d\n", port_status.mps_sorights);
        } else {
            len = snprintf(tmpbuf, 4096, "-\n");
        }
        goto gotdata;
    }

gotdata:

    FINI_PORT_LIST();

    READ_PROC_TASK_EPILOGUE();
}

static const char *task_roles[] = {
    "RENICED",
    "UNSPECIFIED",
    "FOREGROUND_APPLICATION",
    "BACKGROUND_APPLICATION",
    "CONTROL_APPLICATION",
    "GRAPHICS_SERVER",
};
#define TASK_ROLES_MAX (int)(sizeof(task_roles)/sizeof(char *))

READ_HANDLER(proc__task__role)
{
    READ_PROC_TASK_PROLOGUE();

    task_category_policy_data_t category_policy;
    mach_msg_type_number_t task_info_count;
    boolean_t get_default;

    len = snprintf(tmpbuf, 4096, "NONE\n");

    task_info_count = TASK_CATEGORY_POLICY_COUNT;
    get_default = FALSE;
    kr = task_policy_get(the_task, TASK_CATEGORY_POLICY,
                         (task_policy_t)&category_policy,
                         &task_info_count, &get_default);
    if (kr == KERN_SUCCESS) {
        if (get_default == FALSE) {
            if ((category_policy.role >= -1) &&
                (category_policy.role < (TASK_ROLES_MAX - 1))) {
                len = snprintf(tmpbuf, 4096, "%s\n",
                               task_roles[category_policy.role + 1]);
            }
        }
    }

    READ_PROC_TASK_EPILOGUE();
}

static const char *thread_states[] = {
    "NONE",
    "RUNNING",
    "STOPPED",
    "WAITING",
    "UNINTERRUPTIBLE",
    "HALTED",
};
#define THREAD_STATES_MAX (int)(sizeof(thread_states)/sizeof(char *))

READ_HANDLER(proc__task__threads__thread__basic_info)
{
    READ_PROC_TASK_PROLOGUE();
    DECL_THREAD_LIST();
    INIT_THREAD_LIST(the_task);

    thread_t the_thread = MACH_PORT_NULL;
    unsigned int i = strtoul(argv[1], NULL, 16);

    if (i < thread_count) {
        the_thread = thread_list[i];
    }
    
    if (the_thread == MACH_PORT_NULL) {
        FINI_THREAD_LIST();
        return -ENOENT;
    }   
        
    const char *whichfile = argv[2];

    thread_info_data_t thinfo;
    mach_msg_type_number_t thread_info_count;
    thread_basic_info_t basic_info_th;

    thread_info_count = THREAD_INFO_MAX;
    kr = thread_info(the_thread, THREAD_BASIC_INFO, (thread_info_t)thinfo,
                     &thread_info_count);

    if (kr != KERN_SUCCESS) {
        FINI_THREAD_LIST();
        return -EIO;
    }

    basic_info_th = (thread_basic_info_t)thinfo;

    if (strcmp(whichfile, "cpu_usage") == 0) {
        len = snprintf(tmpbuf, 4096, "%u\n", basic_info_th->cpu_usage);
        goto gotdata;
    }

    if (strcmp(whichfile, "flags") == 0) {
        len = 0;
        len += snprintf(tmpbuf + len, 4096 - len, "%x", basic_info_th->flags);
        len += snprintf(tmpbuf + len, 4096 - len, "%s",
                 (basic_info_th->flags & TH_FLAGS_IDLE) ? " (IDLE)" : "");
        len += snprintf(tmpbuf + len, 4096 - len, "%s",
                 (basic_info_th->flags & TH_FLAGS_SWAPPED) ? " (SWAPPED)" : "");
        len += snprintf(tmpbuf + len, 4096 - len, "\n");
        goto gotdata;
    }

    if (strcmp(whichfile, "policy") == 0) {

        len = 0;
        boolean_t get_default = FALSE;
        thread_extended_policy_data_t        extended_policy;
        thread_time_constraint_policy_data_t time_constraint_policy;
        thread_precedence_policy_data_t      precedence_policy;

        switch (basic_info_th->policy) {

        case THREAD_EXTENDED_POLICY:
            thread_info_count = THREAD_EXTENDED_POLICY_COUNT;
            kr = thread_policy_get(the_thread, THREAD_EXTENDED_POLICY,
                                   (thread_policy_t)&extended_policy,
                                   &thread_info_count, &get_default);
            if (kr != KERN_SUCCESS) {
                len += snprintf(tmpbuf + len, 4096 - len,
                                "STANDARD/EXTENDED\n");
                break;
            }
            len += snprintf(tmpbuf + len, 4096 - len, "%s\n",
                            (extended_policy.timeshare == TRUE) ? \
                            "STANDARD" : "EXTENDED");
            break;

        case THREAD_TIME_CONSTRAINT_POLICY:
            len += snprintf(tmpbuf + len, 4096 - len, "TIME_CONSTRAINT");
            thread_info_count = THREAD_TIME_CONSTRAINT_POLICY_COUNT;
            kr = thread_policy_get(the_thread, THREAD_TIME_CONSTRAINT_POLICY,
                                   (thread_policy_t)&time_constraint_policy,
                                   &thread_info_count, &get_default);
            if (kr != KERN_SUCCESS) {
                len += snprintf(tmpbuf + len, 4096 - len, "\n");
                break;
            }
            len += snprintf(tmpbuf + len, 4096 - len,
                            " (period=%u computation=%u constraint=%u "
                            "preemptible=%s)\n",
                            time_constraint_policy.period,
                            time_constraint_policy.computation,
                            time_constraint_policy.constraint,
                            (time_constraint_policy.preemptible == TRUE) ? \
                            "TRUE" : "FALSE");
            break;

        case THREAD_PRECEDENCE_POLICY:
            len += snprintf(tmpbuf + len, 4096 - len, "PRECEDENCE");
            thread_info_count = THREAD_PRECEDENCE_POLICY;
            kr = thread_policy_get(the_thread, THREAD_PRECEDENCE_POLICY,
                                   (thread_policy_t)&precedence_policy,
                                   &thread_info_count, &get_default);
            if (kr != KERN_SUCCESS) {
                len += snprintf(tmpbuf + len, 4096 - len, "\n");
                break;
            }
            len += snprintf(tmpbuf + len, 4096 - len, " (importance=%u)\n",
                            precedence_policy.importance);
            break;

        default:
            len = snprintf(tmpbuf, 4096, "UNKNOWN?\n");
            break;
        }
        goto gotdata;
    }

    if (strcmp(whichfile, "run_state") == 0) {
        len = 0;
        len += snprintf(tmpbuf + len, 4096 - len, "%u",
                        basic_info_th->run_state);
        len += snprintf(tmpbuf + len, 4096 - len, " (%s)\n",
                        (basic_info_th->run_state >= THREAD_STATE_MAX) ? \
                        "?" : thread_states[basic_info_th->run_state]);
        goto gotdata;
    }

    if (strcmp(whichfile, "sleep_time") == 0) {
        len = snprintf(tmpbuf, 4096, "%us\n", basic_info_th->sleep_time);
        goto gotdata;
    }

    if (strcmp(whichfile, "suspend_count") == 0) {
        len = snprintf(tmpbuf, 4096, "%u\n", basic_info_th->suspend_count);
        goto gotdata;
    }

    if (strcmp(whichfile, "system_time") == 0) {
        len = snprintf(tmpbuf, 4096, "%us %uus\n",
                       basic_info_th->system_time.seconds,
                       basic_info_th->system_time.microseconds);
        goto gotdata;
    }

    if (strcmp(whichfile, "user_time") == 0) {
        len = snprintf(tmpbuf, 4096, "%us %uus\n",
                       basic_info_th->user_time.seconds,
                       basic_info_th->user_time.microseconds);
        goto gotdata;
    }

gotdata:

    FINI_THREAD_LIST();

    READ_PROC_TASK_EPILOGUE();
}

READ_HANDLER(proc__task__threads__thread__states__debug)
{
    READ_PROC_TASK_PROLOGUE();
    len = snprintf(tmpbuf, 4096, "not yet implemented\n");
    READ_PROC_TASK_EPILOGUE();
}

READ_HANDLER(proc__task__threads__thread__states__exception)
{
    READ_PROC_TASK_PROLOGUE();
    len = snprintf(tmpbuf, 4096, "not yet implemented\n");
    READ_PROC_TASK_EPILOGUE();
}

READ_HANDLER(proc__task__threads__thread__states__float)
{
    READ_PROC_TASK_PROLOGUE();
    DECL_THREAD_LIST();
    INIT_THREAD_LIST(the_task);

    thread_t the_thread = MACH_PORT_NULL;
    unsigned int i = strtoul(argv[1], NULL, 16);

    if (i < thread_count) {
        the_thread = thread_list[i];
    }

    if (the_thread == MACH_PORT_NULL) {
        FINI_THREAD_LIST();
        return -ENOENT;
    }

#if defined(__i386__)
    const char *whichfile = argv[2];

    x86_float_state_t state = {{0}};
    unsigned int count = x86_FLOAT_STATE_COUNT;
    kr = thread_get_state(the_thread, x86_FLOAT_STATE, (thread_state_t)&state,
                          &count);
    if (kr != KERN_SUCCESS) {
        FINI_THREAD_LIST();
        return -EIO;
    }

#define HANDLE_x86_FLOAT_STATE_ITEM(item, fmt)                      \
    if (strcmp(whichfile, #item) == 0) {                            \
        len = snprintf(tmpbuf, 4096, fmt, state.ufs.fs32.__##item); \
        goto gotdata;                                               \
    }

    HANDLE_x86_FLOAT_STATE_ITEM(fpu_cs, "%hx\n");
    HANDLE_x86_FLOAT_STATE_ITEM(fpu_dp, "%x\n");
    HANDLE_x86_FLOAT_STATE_ITEM(fpu_ds, "%hx\n");
    HANDLE_x86_FLOAT_STATE_ITEM(fpu_fop, "%hx\n");
    HANDLE_x86_FLOAT_STATE_ITEM(fpu_ftw, "%hhx\n");
    HANDLE_x86_FLOAT_STATE_ITEM(fpu_ip, "%x\n");
    HANDLE_x86_FLOAT_STATE_ITEM(fpu_mxcsr, "%x\n");
    HANDLE_x86_FLOAT_STATE_ITEM(fpu_mxcsrmask, "%x\n");

#define HANDLE_x86_FLOAT_STATE_ITEM_CONTROL_BIT(bit)            \
    if (state.ufs.fs32.__fpu_fcw.__##bit) {                     \
        len += snprintf(tmpbuf + len, 4096 - len, "%s ", #bit); \
    }

    if (strcmp(whichfile, "fpu_fcw") == 0) { /* control */
        len = 0;
        HANDLE_x86_FLOAT_STATE_ITEM_CONTROL_BIT(invalid);
        HANDLE_x86_FLOAT_STATE_ITEM_CONTROL_BIT(denorm);
        HANDLE_x86_FLOAT_STATE_ITEM_CONTROL_BIT(zdiv);
        HANDLE_x86_FLOAT_STATE_ITEM_CONTROL_BIT(ovrfl);
        HANDLE_x86_FLOAT_STATE_ITEM_CONTROL_BIT(undfl);
        HANDLE_x86_FLOAT_STATE_ITEM_CONTROL_BIT(precis);
        HANDLE_x86_FLOAT_STATE_ITEM_CONTROL_BIT(pc);
        switch (state.ufs.fs32.__fpu_fcw.__pc) {    
        case 0:
            len += snprintf(tmpbuf + len, 4096 - len, "(24B) ");
            break;
        case 2:
            len += snprintf(tmpbuf + len, 4096 - len, "(53B) ");
             break;
        case 3:
            len += snprintf(tmpbuf + len, 4096 - len, "(64B) ");
            break;
        }
        HANDLE_x86_FLOAT_STATE_ITEM_CONTROL_BIT(rc);
        switch (state.ufs.fs32.__fpu_fcw.__rc) {
        case 0:
            len += snprintf(tmpbuf + len, 4096 - len, "(round near) ");
            break;
        case 1:
            len += snprintf(tmpbuf + len, 4096 - len, "(round down) ");
            break;
        case 2:
            len += snprintf(tmpbuf + len, 4096 - len, "(round up) ");
            break;
        case 3:
            len += snprintf(tmpbuf + len, 4096 - len, "(chop) ");
            break;
        }
        len += snprintf(tmpbuf + len, 4096 - len, "\n");
        goto gotdata;
    }

#define HANDLE_x86_FLOAT_STATE_ITEM_STATUS_BIT(bit)             \
    if (state.ufs.fs32.__fpu_fsw.__##bit) {                     \
        len += snprintf(tmpbuf + len, 4096 - len, "%s ", #bit); \
    }

    if (strcmp(whichfile, "fpu_fsw") == 0) { /* status */
        len = 0;
        HANDLE_x86_FLOAT_STATE_ITEM_STATUS_BIT(invalid);
        HANDLE_x86_FLOAT_STATE_ITEM_STATUS_BIT(denorm);
        HANDLE_x86_FLOAT_STATE_ITEM_STATUS_BIT(zdiv);
        HANDLE_x86_FLOAT_STATE_ITEM_STATUS_BIT(ovrfl);
        HANDLE_x86_FLOAT_STATE_ITEM_STATUS_BIT(undfl);
        HANDLE_x86_FLOAT_STATE_ITEM_STATUS_BIT(precis);
        HANDLE_x86_FLOAT_STATE_ITEM_STATUS_BIT(stkflt);
        HANDLE_x86_FLOAT_STATE_ITEM_STATUS_BIT(errsumm);
        HANDLE_x86_FLOAT_STATE_ITEM_STATUS_BIT(c0);
        HANDLE_x86_FLOAT_STATE_ITEM_STATUS_BIT(c1);
        HANDLE_x86_FLOAT_STATE_ITEM_STATUS_BIT(c2);
        HANDLE_x86_FLOAT_STATE_ITEM_STATUS_BIT(c3);
        HANDLE_x86_FLOAT_STATE_ITEM_STATUS_BIT(busy);
        len += snprintf(tmpbuf + len, 4096 - len, "tos=%hx\n",
                        state.ufs.fs32.__fpu_fsw.__tos);
        goto gotdata;
    }

#else
    len = -1;
    goto gotdata;
#endif

gotdata:

    FINI_THREAD_LIST();

    READ_PROC_TASK_EPILOGUE();
}

READ_HANDLER(proc__task__threads__thread__states__thread)
{
    READ_PROC_TASK_PROLOGUE();
    DECL_THREAD_LIST();
    INIT_THREAD_LIST(the_task);

    thread_t the_thread = MACH_PORT_NULL;
    unsigned int i = strtoul(argv[1], NULL, 16);

    if (i < thread_count) {
        the_thread = thread_list[i];
    }

    if (the_thread == MACH_PORT_NULL) {
        FINI_THREAD_LIST();
        return -ENOENT;
    }

#if defined(__i386__)

    const char *whichfile = argv[2];

    x86_thread_state_t state = {{0}};
    unsigned int count = x86_THREAD_STATE_COUNT;
    kr = thread_get_state(the_thread, x86_THREAD_STATE, (thread_state_t)&state,
                          &count);
    if (kr != KERN_SUCCESS) {
        FINI_THREAD_LIST();
        return -EIO;
    }

#define HANDLE_x86_THREAD_STATE_ITEM(item)                             \
    if (strcmp(whichfile, #item) == 0) {                               \
        len = snprintf(tmpbuf, 4096, "%x\n", state.uts.ts32.__##item); \
        goto gotdata;                                                  \
    }

    HANDLE_x86_THREAD_STATE_ITEM(eax);
    HANDLE_x86_THREAD_STATE_ITEM(ebx);
    HANDLE_x86_THREAD_STATE_ITEM(ecx);
    HANDLE_x86_THREAD_STATE_ITEM(edx);
    HANDLE_x86_THREAD_STATE_ITEM(edi);
    HANDLE_x86_THREAD_STATE_ITEM(esi);
    HANDLE_x86_THREAD_STATE_ITEM(ebp);
    HANDLE_x86_THREAD_STATE_ITEM(esp);
    HANDLE_x86_THREAD_STATE_ITEM(ss);
    HANDLE_x86_THREAD_STATE_ITEM(eflags);
    HANDLE_x86_THREAD_STATE_ITEM(eip);
    HANDLE_x86_THREAD_STATE_ITEM(cs);
    HANDLE_x86_THREAD_STATE_ITEM(ds);
    HANDLE_x86_THREAD_STATE_ITEM(es);
    HANDLE_x86_THREAD_STATE_ITEM(fs);
    HANDLE_x86_THREAD_STATE_ITEM(gs);

#else
    len = -1;
    goto gotdata;
#endif

gotdata:

    FINI_THREAD_LIST();

    READ_PROC_TASK_EPILOGUE();
}

READ_HANDLER(proc__task__tokens)
{
    READ_PROC_TASK_PROLOGUE();

    unsigned int n;
    audit_token_t audit_token;
    security_token_t security_token;
    mach_msg_type_number_t task_info_count;
    const char *whichfile = argv[1];

    if (strcmp(whichfile, "audit") == 0) {
        task_info_count = TASK_AUDIT_TOKEN_COUNT;
        kr = task_info(the_task, TASK_AUDIT_TOKEN,
                       (task_info_t)&audit_token, &task_info_count);
        len = -1;
        if (kr == KERN_SUCCESS) {
            len = 0;
            for (n = 0; n < sizeof(audit_token)/sizeof(uint32_t); n++) {
                len += snprintf(tmpbuf + len, 4096 - len, "%x ",
                                audit_token.val[n]);
            }
            len += snprintf(tmpbuf + len, 4096 - len, "\n");
        }
        goto gotdata;
    }

    if (strcmp(whichfile, "security") == 0) {
        task_info_count = TASK_SECURITY_TOKEN_COUNT;
        kr = task_info(the_task, TASK_SECURITY_TOKEN,
                       (task_info_t)&security_token, &task_info_count);
        len = -1;
        if (kr == KERN_SUCCESS) {
            len = 0;
            for (n = 0; n < sizeof(security_token)/sizeof(uint32_t); n++) {
                len += snprintf(tmpbuf + len, 4096 - len, "%x ",
                                security_token.val[n]);
            }
            len += snprintf(tmpbuf + len, 4096 - len, "\n");
        }
        goto gotdata;
    }

    return -ENOENT;

gotdata:

    READ_PROC_TASK_EPILOGUE();
}

const char *
inheritance_strings[] = {
    "SHARE", "COPY", "NONE", "DONATE_COPY",
};

const char *
behavior_strings[] = {
    "DEFAULT", "RANDOM", "SEQUENTIAL", "RESQNTL", "WILLNEED", "DONTNEED",
};

READ_HANDLER(proc__task__vmmap)
{
    int len = -1;
    kern_return_t kr;
#define MAX_VMMAP_SIZE 65536 /* XXX */
    char tmpbuf[MAX_VMMAP_SIZE];
    task_t the_task;
    pid_t pid = strtol(argv[0], NULL, 10);

    kr = task_for_pid(mach_task_self(), pid, &the_task);
    if (kr != KERN_SUCCESS) {
        return -EIO;
    }

    vm_size_t vmsize;
    vm_address_t address;
    vm_region_basic_info_data_t info;
    mach_msg_type_number_t info_count;
    vm_region_flavor_t flavor; 
    memory_object_name_t object;

    kr = KERN_SUCCESS;
    address = 0;
    len = 0;

    do {
        flavor = VM_REGION_BASIC_INFO;
        info_count = VM_REGION_BASIC_INFO_COUNT;
        kr = vm_region(the_task, &address, &vmsize, flavor,
                       (vm_region_info_t)&info, &info_count, &object);
        if (kr == KERN_SUCCESS) {
            if (len >= MAX_VMMAP_SIZE) {
                goto gotdata;
            }
            len += snprintf(tmpbuf + len, MAX_VMMAP_SIZE - len,
            "%08x-%08x %8uK %c%c%c/%c%c%c %11s %6s %10s uwir=%hu sub=%u\n",
                            address, (address + vmsize), (vmsize >> 10),
                            (info.protection & VM_PROT_READ)        ? 'r' : '-',
                            (info.protection & VM_PROT_WRITE)       ? 'w' : '-',
                            (info.protection & VM_PROT_EXECUTE)     ? 'x' : '-',
                            (info.max_protection & VM_PROT_READ)    ? 'r' : '-',
                            (info.max_protection & VM_PROT_WRITE)   ? 'w' : '-',
                            (info.max_protection & VM_PROT_EXECUTE) ? 'x' : '-',
                            inheritance_strings[info.inheritance],
                            (info.shared) ? "shared" : "-",
                            behavior_strings[info.behavior],
                            info.user_wired_count,
                            info.reserved);
            address += vmsize;
        } else if (kr != KERN_INVALID_ADDRESS) {

            if (the_task != MACH_PORT_NULL) {
                mach_port_deallocate(mach_task_self(), the_task);
            }

            return -EIO;
        }
    } while (kr != KERN_INVALID_ADDRESS);

gotdata:

    if (the_task != MACH_PORT_NULL) {
        mach_port_deallocate(mach_task_self(), the_task);
    }

    READ_PROC_TASK_EPILOGUE();
}

static int
M_get_vmmap_entries(task_t task)
{
    kern_return_t kr      = KERN_SUCCESS;
    vm_address_t  address = 0;
    vm_size_t     size    = 0;
    int           n       = 1;

    while (1) {
        mach_msg_type_number_t count;
        struct vm_region_submap_info_64 info;
        uint32_t nesting_depth;
  
        count = VM_REGION_SUBMAP_INFO_COUNT_64;
        kr = vm_region_recurse_64(task, &address, &size, &nesting_depth,
                                  (vm_region_info_64_t)&info, &count);
        if (kr == KERN_INVALID_ADDRESS) {
            break;
        } else if (kr) {
            mach_error("vm_region:", kr);
            break; /* last region done */
        }

        if (info.is_submap) {
            nesting_depth++;
        } else {
            address += size;
            n++;
        }
    }

    return n;
}

#define CAST_DOWN(type, addr) (((type)((uintptr_t)(addr))))

static const char *
get_user_tag_description(unsigned int user_tag)
{
    const char *description = "unknown";

    switch (user_tag) {
    
    case VM_MEMORY_MALLOC:
        description = "MALLOC";
        break;
    case VM_MEMORY_MALLOC_SMALL:
        description = "MALLOC_SMALL";
        break;
    case VM_MEMORY_MALLOC_LARGE:
        description = "MALLOC_LARGE";
        break;
    case VM_MEMORY_MALLOC_HUGE:
        description = "MALLOC_HUGE";
        break;
    case VM_MEMORY_SBRK:
        description = "SBRK";
        break;
    case VM_MEMORY_REALLOC:
        description = "REALLOC";
        break;
    case VM_MEMORY_MALLOC_TINY:
        description = "MALLOC_TINY";
        break;
    case VM_MEMORY_ANALYSIS_TOOL:
        description = "ANALYSIS_TOOL";
        break;
    case VM_MEMORY_MACH_MSG:
        description = "MACH_MSG";
        break;
    case VM_MEMORY_IOKIT:
        description = "IOKIT";
        break;
    case VM_MEMORY_STACK:
        description = "STACK";
        break;
    case VM_MEMORY_GUARD:
        description = "MEMORY_GUARD";
        break;
    case VM_MEMORY_SHARED_PMAP:
        description = "SHARED_PMAP";
        break;
    case VM_MEMORY_DYLIB:
        description = "DYLIB";
        break;
    case VM_MEMORY_APPKIT:
        description = "AppKit";
        break;
    case VM_MEMORY_FOUNDATION:
        description = "Foundation";
        break;
    case VM_MEMORY_COREGRAPHICS:
        description = "CoreGraphics";
        break;
    case VM_MEMORY_CARBON:
        description = "Carbon";
        break;
    case VM_MEMORY_JAVA:
        description = "Java";
        break;
    case VM_MEMORY_ATS:
        description = "ATS";
        break;
    case VM_MEMORY_DYLD:
        description = "DYLD";
        break;
    case VM_MEMORY_DYLD_MALLOC:
        description = "DYLD_MALLOC";
        break;
    case VM_MEMORY_APPLICATION_SPECIFIC_1:
        description = "APPLICATION_SPECIFIC_1";
        break;
    case VM_MEMORY_APPLICATION_SPECIFIC_16:
        description = "APPLICATION_SPECIFIC_16";
        break;
    default:
        break;
    }

    return description;
}

READ_HANDLER(proc__task__vmmap_r)
{ 
    int len = -1;
    kern_return_t kr;
    uint32_t nesting_depth = 0;
    struct vm_region_submap_info_64 vbr;
    mach_msg_type_number_t vbrcount = 0;
#define MAX_VMMAP_R_SIZE 262144 /* XXX */
    char tmpbuf[MAX_VMMAP_R_SIZE];
    task_t the_task;
    int segment_count;
    pid_t pid = strtol(argv[0], NULL, 10);

    kr = task_for_pid(mach_task_self(), pid, &the_task);
    if (kr != KERN_SUCCESS) {
        return -EIO;
    }

    mach_vm_size_t vmsize;
    mach_vm_address_t address;

    kr = KERN_SUCCESS;
    address = 0;
    len = 0;

    segment_count = M_get_vmmap_entries(the_task);

    while (segment_count > 0) {
        while (1) { /* next region */
            vbrcount = VM_REGION_SUBMAP_INFO_COUNT_64;
            if ((kr = mach_vm_region_recurse(the_task, &address, &vmsize,
                                             &nesting_depth,
                                             (vm_region_recurse_info_t)&vbr,
                                             &vbrcount)) != KERN_SUCCESS) {
                break;
            }
            if (address + vmsize > VM_MAX_ADDRESS) {
                kr = KERN_INVALID_ADDRESS;
                break;
            }
            if (vbr.is_submap) {
                nesting_depth++;
                continue;
            } else {
                break;
            }
        } /* while (1) */

        if (kr != KERN_SUCCESS) {
            if (kr != KERN_INVALID_ADDRESS) {
                if (the_task != MACH_PORT_NULL) {
                    mach_port_deallocate(mach_task_self(), the_task);
                }
                return -EIO;
            }
            break;
        }

        if (len >= MAX_VMMAP_R_SIZE) {
            goto gotdata;
        }

        /* XXX: 32-bit only */

        len += snprintf(tmpbuf + len, MAX_VMMAP_R_SIZE - len,
            "%08x-%08x %8uK %c%c%c/%c%c%c ",
                        CAST_DOWN(uint32_t,address),
                        CAST_DOWN(uint32_t,(address + vmsize)),
                        CAST_DOWN(uint32_t,(vmsize >> 10)),
                        (vbr.protection & VM_PROT_READ)        ? 'r' : '-',
                        (vbr.protection & VM_PROT_WRITE)       ? 'w' : '-',
                        (vbr.protection & VM_PROT_EXECUTE)     ? 'x' : '-',
                        (vbr.max_protection & VM_PROT_READ)    ? 'r' : '-',
                        (vbr.max_protection & VM_PROT_WRITE)   ? 'w' : '-',
                        (vbr.max_protection & VM_PROT_EXECUTE) ? 'x' : '-');

        if (vbr.is_submap) {
            len += snprintf(tmpbuf + len, MAX_VMMAP_R_SIZE - len,
                            "%20s %s\n", "(submap)",
                            get_user_tag_description(vbr.user_tag));
        } else {
            len += snprintf(tmpbuf + len, MAX_VMMAP_R_SIZE - len,
                            "%6d %6d %6d %s\n",
                            vbr.pages_resident,
                            vbr.pages_swapped_out,
                            vbr.pages_dirtied,
                            get_user_tag_description(vbr.user_tag));
        }

        address += vmsize;
        segment_count--;

    } /* while (segment_count > 0) */

gotdata:

    if (the_task != MACH_PORT_NULL) {
        mach_port_deallocate(mach_task_self(), the_task);
    }

    READ_PROC_TASK_EPILOGUE();
}

READ_HANDLER(proc__windows__generic)
{
    pid_t pid = atoi(argv[0]);
    const char *whichfile = argv[1];
    ProcessSerialNumber psn;

    OSStatus status = GetProcessForPID(pid, &psn);
    if (status != noErr) {
        return 0; /* technically not an error in this case */
    }

    CGSConnectionID conn;
    CGError err = CGSGetConnectionIDForPSN(0, &psn, &conn);
    if (err != kCGErrorSuccess) {
        return 0; /* just be nice */
    }

#define MAX_WINDOWS 256
    CGSWindowID windowIDs[MAX_WINDOWS];
    int windowCount = 0;

    if (strcmp(whichfile, "all") == 0) {
        err = CGSGetWindowList(_CGSDefaultConnection(), conn, MAX_WINDOWS,
                               windowIDs, &windowCount);
    } else if (strcmp(whichfile, "onscreen") == 0) {
        err = CGSGetOnScreenWindowList(_CGSDefaultConnection(), conn,
                                       MAX_WINDOWS, windowIDs, &windowCount);
    }

    if (err != kCGErrorSuccess) {
        return -EIO;
    }

    if (windowCount == 0) {
        return 0;
    }

#define MAX_WINDOWDATA 16384
    char tmpbuf[MAX_WINDOWDATA];
    int i, len = 0;

    for (i = 0; i < windowCount; i++) { 

        if (len > MAX_WINDOWDATA) {
           goto gotdata;
        }

        CGRect rect;
        err = CGSGetScreenRectForWindow(_CGSDefaultConnection(), windowIDs[i],
                                        &rect);
        CGWindowLevel level;
        CGError err2 = CGSGetWindowLevel(_CGSDefaultConnection(), windowIDs[i],
                                         &level);
        len += snprintf(tmpbuf + len, MAX_WINDOWDATA - len,
                        "%-4d %-6x %.0f x %.0f @ (%.0f, %.0f, %d)\n",
                        i + 1, windowIDs[i],
                        (err == kCGErrorSuccess)  ? rect.size.width  : -1,
                        (err == kCGErrorSuccess)  ? rect.size.height : -1,
                        (err == kCGErrorSuccess)  ? rect.origin.x    : -1,
                        (err == kCGErrorSuccess)  ? rect.origin.y    : -1,
                        (err2 == kCGErrorSuccess) ? level            : -1);
    }

gotdata:

    if (offset < len) {
        if (offset + size > len)
            size = len - offset;
        memcpy(buf, tmpbuf + offset, size);
    } else
        size = 0;

    return size;
}

READ_HANDLER(proc__windows__screenshots__window) 
{
    if (fi->fh == 0) {
        return 0;
    }

    struct ProcfsWindowData *pwd = (struct ProcfsWindowData *)fi->fh;

    CFMutableDataRef window_png = pwd->window_png;
    size_t max_len = pwd->max_len;
    size_t len = pwd->len;

    if (len > max_len) {
        return -EIO;
    }

    CFDataSetLength(window_png, max_len);
    len = max_len;

    const UInt8 *tmpbuf = CFDataGetBytePtr(window_png);

    if (offset < len) {
        if (offset + size > len)
            size = len - offset;
        memcpy(buf, tmpbuf + offset, size);
    } else
        size = 0;

    return size;
}

READ_HANDLER(proc__xcred)
{
    pid_t pid = atoi(argv[0]);
    const char *whichfile = argv[2];
    struct kinfo_proc kp;
    int len;
    char tmpbuf[4096];
    struct passwd *p;
    struct group *g;

    len = procinfo(pid, &kp);
    if (len != 0) {
        return -EIO;
    }

    len = -1;

    if (strcmp(whichfile, "groups") == 0) {
        short n;
        len = 0;
        for (n = 0; n < kp.kp_eproc.e_ucred.cr_ngroups; n++) {
            g = getgrgid(kp.kp_eproc.e_ucred.cr_groups[n]);
            len += snprintf(tmpbuf + len, 4096 - len, "%d(%s) ",
                            kp.kp_eproc.e_ucred.cr_groups[n],
                            (g) ? g->gr_name : "?");
        }
        len += snprintf(tmpbuf + len, 4096 - len, "\n");
        goto gotdata;
    }   

    if (strcmp(whichfile, "rgid") == 0) {
        g = getgrgid(kp.kp_eproc.e_pcred.p_rgid);
        len = snprintf(tmpbuf, 4096, "%d(%s)\n", kp.kp_eproc.e_pcred.p_rgid,
                       (g) ? g->gr_name : "?");
        goto gotdata;
    }

    if (strcmp(whichfile, "svgid") == 0) {
        g = getgrgid(kp.kp_eproc.e_pcred.p_svgid);
        len = snprintf(tmpbuf, 4096, "%d(%s)\n", kp.kp_eproc.e_pcred.p_svgid,
                       (g) ? g->gr_name : "?");
        goto gotdata;
    }

    if (strcmp(whichfile, "ruid") == 0) {
        p = getpwuid(kp.kp_eproc.e_pcred.p_ruid);
        len = snprintf(tmpbuf, 4096, "%d(%s)\n", kp.kp_eproc.e_pcred.p_ruid,
                       (p) ? p->pw_name : "?");
        goto gotdata;
    }

    if (strcmp(whichfile, "svuid") == 0) {
        p = getpwuid(kp.kp_eproc.e_pcred.p_svuid);
        len = snprintf(tmpbuf, 4096, "%d(%s)\n", kp.kp_eproc.e_pcred.p_svuid,
                       (p) ? p->pw_name : "?");
        goto gotdata;
    }

    if (strcmp(whichfile, "uid") == 0) {
        p = getpwuid(kp.kp_eproc.e_ucred.cr_uid);
        len = snprintf(tmpbuf, 4096, "%d(%s)\n", kp.kp_eproc.e_ucred.cr_uid,
                       (p) ? p->pw_name : "?");
        goto gotdata;
    }

gotdata:
    
    if (len < 0) {
        return -EIO;
    }

    if (offset < len) {
        if (offset + size > len)
            size = len - offset;
        memcpy(buf, tmpbuf + offset, size);
    } else
        size = 0;

    return size;
}

// END: READ


// BEGIN: READDIR

//    int
//    procfs_readdir_<handler>(procfs_dispatcher_entry_t *e,
//                             const char                *argv[],
//                             void                      *buf,
//                             fuse_fill_dir_t            filler,
//                             off_t                      offset,
//                             struct fuse_file_info     *fi)


int
procfs_populate_directory(const char           **content_files,
                          const char           **content_directories,
                          void                  *buf,
                          fuse_fill_dir_t        filler,
                          off_t                  offset,
                          struct fuse_file_info *fi)
{
    int    bufferfull = 0;
    struct stat dir_stat;
    struct stat file_stat;
    const char **name;

    memset(&dir_stat, 0, sizeof(dir_stat));
    dir_stat.st_mode = S_IFDIR | 0555;
    dir_stat.st_size = 0;

    memset(&file_stat, 0, sizeof(file_stat));
    dir_stat.st_mode = S_IFREG | 0444;
    dir_stat.st_size = 0;

    if (filler(buf, ".", NULL, 0)) {
        bufferfull = 1;
        goto out;
    }

    if (filler(buf, "..", NULL, 0)) {
        bufferfull = 1;
        goto out;
    }

    if (!content_files && !content_directories) {
        goto out;
    }

    name = content_directories;
    if (name) {
        for (; *name; name++) {
            if (filler(buf, *name, &dir_stat, 0)) {
                bufferfull = 1;
                goto out;
            }
        }
    }

    name = content_files;
    if (name) {
        for (; *name; name++) {
            if (filler(buf, *name, &file_stat, 0)) {
                bufferfull = 1;
                goto out;
            }
        }
    }

    if (procfs_ui) {
        name = content_files;
        if (name) {
            for (; *name; name++) {
                char dot_name[MAXPATHLEN + 1];
                snprintf(dot_name, MAXPATHLEN, "._%s", *name);
                if (filler(buf, dot_name, &file_stat, 0)) {
                    bufferfull = 1;
                    goto out;
                }
            }
        }
    }

out:
    return bufferfull;
}


READDIR_HANDLER(enotdir)
{
    return -ENOTDIR;
}

READDIR_HANDLER(default)
{
    return 0;
}

READDIR_HANDLER(root)
{
    unsigned int i;
    kern_return_t kr;
    char the_name[MAXNAMLEN + 1];  
    struct stat dir_stat;
    pid_t pid;
    DECL_TASK_LIST();

    INIT_TASK_LIST();
    for (i = 0; i < task_count; i++) {
        memset(&dir_stat, 0, sizeof(dir_stat));
        dir_stat.st_mode = S_IFDIR | 0755;
        dir_stat.st_size = 0;
        kr = pid_for_task(task_list[i], &pid);
        if (kr != KERN_SUCCESS) {
            continue;
        }
        snprintf(the_name, MAXNAMLEN, "%d", pid);
        if (filler(buf, the_name, &dir_stat, 0)) {
            break;
        }
    }
    FINI_TASK_LIST();

    return 0;
}

READDIR_HANDLER(byname)
{
    int len;
    char the_name[MAXNAMLEN + 1];  
    Boolean strstatus = false;
    struct stat the_stat;

    ProcessSerialNumber psn;
    OSErr osErr = noErr;
    OSStatus status;
    CFStringRef Pname;
    pid_t Pid;

    psn.highLongOfPSN = kNoProcess;
    psn.lowLongOfPSN  = kNoProcess;
    memset(&the_stat, 0, sizeof(the_stat));

    while ((osErr = GetNextProcess(&psn)) != procNotFound) {
        status = GetProcessPID(&psn, &Pid);
        if (status != noErr) {
            continue;
        }
        Pname = (CFStringRef)0;
        status = CopyProcessName(&psn, &Pname);
        if (status != noErr) {
            if (Pname) {
                CFRelease(Pname);
                Pname = (CFStringRef)0;
            }
            continue;
        }
        the_stat.st_mode = S_IFLNK | 0755;
        the_stat.st_nlink = 1;
        len = snprintf(the_name, MAXNAMLEN, "../%u", Pid);
        the_stat.st_size = len;

        strstatus = CFStringGetCString(Pname, the_name, MAXNAMLEN,
                                       kCFStringEncodingASCII);
        if (strstatus == false) {
            CFRelease(Pname);
            Pname = (CFStringRef)0;
            continue;
        }

        if (filler(buf, the_name, &the_stat, 0)) {
            CFRelease(Pname);
            break;
        }
        CFRelease(Pname);
    }

    return 0;
}

READDIR_HANDLER(system__hardware__cpus)
{
    int len;
    unsigned int i;
    char the_name[MAXNAMLEN + 1];  
    struct stat the_stat;

    memset(&the_stat, 0, sizeof(the_stat));

    for (i = 0; i < processor_count; i++) {
        the_stat.st_mode = S_IFDIR | 0555;
        the_stat.st_nlink = 1;
        len = snprintf(the_name, MAXNAMLEN, "%d", i);
        if (filler(buf, the_name, &the_stat, 0)) {
            break;
        }
    }

    return 0;
}

READDIR_HANDLER(system__hardware__cpus__cpu)
{
    return 0;
}

READDIR_HANDLER(system__hardware__displays)
{
    int len;
    unsigned int i;
    char the_name[MAXNAMLEN + 1];
    struct stat the_stat;
    CGDisplayCount display_count = PROCFS_GetDisplayCount();

    memset(&the_stat, 0, sizeof(the_stat));

    for (i = 0; i < display_count; i++) {
        the_stat.st_mode = S_IFDIR | 0555;
        the_stat.st_nlink = 1;
        len = snprintf(the_name, MAXNAMLEN, "%d", i);
        if (filler(buf, the_name, &the_stat, 0)) {
            break;
        }
    }

    return 0;

}

READDIR_HANDLER(system__hardware__displays__display)
{
    unsigned long index = strtol(argv[0], NULL, 10);
    CGDisplayCount display_count = PROCFS_GetDisplayCount();

    if (index >= display_count) {
        return -ENOENT;
    }

    return 0;
}

READDIR_HANDLER(system__hardware__tpm__keyslots)
{
#if OSXFUSE_PROCFS_ENABLE_TPM
    unsigned int i, len;
    char the_name[MAXNAMLEN + 1];  
    struct stat the_stat;
    uint32_t keys[256];

    uint16_t slots_used = 0;
    uint32_t slots_free = 0;
    uint32_t slots_total = 0;

    if (TPM_GetCapability_Slots(&slots_free)) {
        return -ENOENT;
    }

    if (TPM_GetCapability_Key_Handle(&slots_used, keys)) {
        return -ENOENT;
    }

    slots_total = slots_used + slots_free;

    memset(&the_stat, 0, sizeof(the_stat));

    for (i = 0; i < slots_total; i++) {
        len = snprintf(the_name, MAXNAMLEN, "key%02d", i);
        if (i >= slots_used) {
            the_stat.st_size = 0;
            the_stat.st_mode = S_IFREG | 0000;
        } else {
            the_stat.st_size = 4096;
            the_stat.st_mode = S_IFREG | 0444;
        }
        if (filler(buf, the_name, &the_stat, 0)) {
            break;
        }
    }
#endif

    return 0;
}

READDIR_HANDLER(system__hardware__tpm__pcrs)
{
#if OSXFUSE_PROCFS_ENABLE_TPM
    unsigned int i, len;
    uint32_t pcrs;
    char the_name[MAXNAMLEN + 1];  
    struct stat the_stat;

    if (TPM_GetCapability_Pcrs(&pcrs)) {
        return -ENOENT;
    }

    memset(&the_stat, 0, sizeof(the_stat));

    for (i = 0; i < pcrs; i++) {
        len = snprintf(the_name, MAXNAMLEN, "pcr%02d", i);
        the_stat.st_size = 4096;
        the_stat.st_mode = S_IFREG | 0444;
        if (filler(buf, the_name, &the_stat, 0)) {
            break;
        }
    }
#endif

    return 0;
}

READDIR_HANDLER(proc__task__ports)
{
    unsigned int i;
    kern_return_t kr;
    DECL_PORT_LIST();
    pid_t pid = strtol(argv[0], NULL, 10);
    struct stat dir_stat;
    char the_name[MAXNAMLEN + 1];
    task_t the_task = MACH_PORT_NULL;

    kr = task_for_pid(mach_task_self(), pid, &the_task);
    if (kr != KERN_SUCCESS) {
        return -ENOENT;
    }

    memset(&dir_stat, 0, sizeof(dir_stat));
    dir_stat.st_mode = S_IFDIR | 0755;
    dir_stat.st_size = 0;

    INIT_PORT_LIST(the_task);
    for (i = 0; i < name_count; i++) {
        snprintf(the_name, MAXNAMLEN, "%x", name_list[i]);
        if (filler(buf, the_name, &dir_stat, 0)) {
            break;
        }
    }
    FINI_PORT_LIST();

    if (the_task != MACH_PORT_NULL) {
        mach_port_deallocate(mach_task_self(), the_task);
    }

    return 0;
}

READDIR_HANDLER(proc__task__threads)
{
    unsigned int i;
    kern_return_t kr;
    DECL_THREAD_LIST();
    pid_t pid = strtol(argv[0], NULL, 10);
    struct stat dir_stat;
    char the_name[MAXNAMLEN + 1];
    task_t the_task = MACH_PORT_NULL;

    kr = task_for_pid(mach_task_self(), pid, &the_task);
    if (kr != KERN_SUCCESS) {
        return -ENOENT;
    }

    memset(&dir_stat, 0, sizeof(dir_stat));
    dir_stat.st_mode = S_IFDIR | 0755;
    dir_stat.st_size = 0;

    INIT_THREAD_LIST(the_task);
    FINI_THREAD_LIST();

    for (i = 0; i < thread_count; i++) {
        snprintf(the_name, MAXNAMLEN, "%x", i);
        if (filler(buf, the_name, &dir_stat, 0)) {
            break;
        }
    }

    if (the_task != MACH_PORT_NULL) {
        mach_port_deallocate(mach_task_self(), the_task);
    }

    return 0;
}

READDIR_HANDLER(proc__windows__screenshots)
{
    int i;
    pid_t pid = strtol(argv[0], NULL, 10);
    struct stat dir_stat;
    char the_name[MAXNAMLEN + 1];

    ProcessSerialNumber psn;

    OSStatus status = GetProcessForPID(pid, &psn);
    if (status != noErr) {
        return 0; /* technically not an error in this case */
    }

    memset(&dir_stat, 0, sizeof(dir_stat));
    dir_stat.st_mode = S_IFDIR | 0755;
    dir_stat.st_size = 0;

    CGSConnectionID conn;
    CGError err = CGSGetConnectionIDForPSN(0, &psn, &conn);
    if (err != kCGErrorSuccess) {
        return 0; /* just be nice */
    }

#define MAX_WINDOWS 256
    CGSWindowID windowIDs[MAX_WINDOWS];
    int windowCount = 0;

    err = CGSGetOnScreenWindowList(_CGSDefaultConnection(), conn,
                                   MAX_WINDOWS, windowIDs, &windowCount);

    if (err != kCGErrorSuccess) {
        return -EIO;
    }

    if (windowCount == 0) {
        return 0;
    }

    for (i = 0; i < windowCount; i++) {
        snprintf(the_name, MAXNAMLEN, "%x.png", windowIDs[i]);
        dir_stat.st_mode = S_IFREG | 0444;
        dir_stat.st_size = PROCFS_GetPNGSizeForWindowAtIndex(windowIDs[i]);
        if (filler(buf, the_name, &dir_stat, 0)) {
            break;
        }
    }

    return 0;
}

// END: READDIR


// BEGIN: READLINK

//    int
//    procfs_readlink_<handler>(procfs_dispatcher_entry_t  e,
//                              const char                *argv[],
//                              char                      *buf,
//                              size_t                     size)

READLINK_HANDLER(einval)
{
    return -EINVAL;
}

READLINK_HANDLER(byname__name)
{
    const char *target_Pname = argv[0];
    char the_name[MAXNAMLEN + 1];  
    Boolean strstatus = false;

    ProcessSerialNumber psn;
    OSErr osErr = noErr;
    OSStatus status;
    CFStringRef Pname;
    pid_t Pid;

    psn.highLongOfPSN = kNoProcess;
    psn.lowLongOfPSN  = kNoProcess;

    while ((osErr = GetNextProcess(&psn)) != procNotFound) {
        status = GetProcessPID(&psn, &Pid);
        if (status != noErr) {
            continue;
        }
        Pname = (CFStringRef)0;
        status = CopyProcessName(&psn, &Pname);
        if (status != noErr) {
            if (Pname) {
                CFRelease(Pname);
                Pname = (CFStringRef)0;
            }
            continue;
        }

        strstatus = CFStringGetCString(Pname, the_name, MAXNAMLEN,
                                       kCFStringEncodingASCII);

        if (strcmp(target_Pname, the_name) != 0) {
            Pid = 0;
        }

        CFRelease(Pname);
        Pname = (CFStringRef)0;

        if (Pid) {
            break;
        }
    }

    if (!Pid) {
        return -ENOENT;
    }

    (void)snprintf(the_name, MAXNAMLEN, "../%u", Pid);

    strncpy(buf, the_name, size - 1);

    return 0;
}

// END: READLINK


#define DEBUG 1
#ifdef DEBUG
#define TRACEME() { fprintf(stderr, "%s: path=%s\n", __FUNCTION__, path); }
#else
#define TRACEME() { }
#endif

#define EXIT_ON_MACH_ERROR(msg, retval) \
    if (kr != KERN_SUCCESS) { mach_error(msg ":" , kr); exit((retval)); }

static void *
procfs_init(struct fuse_conn_info *conn)
{
    int i;
    kern_return_t kr;

    kr = processor_set_default(mach_host_self(), &p_default_set);
    EXIT_ON_MACH_ERROR("processor_default", 1);
   
    kr = host_processor_set_priv(mach_host_self(), p_default_set,
                                 &p_default_set_control);
    EXIT_ON_MACH_ERROR("host_processor_set_priv", 1);

    kr = host_get_host_priv_port(mach_host_self(), &host_priv);
    EXIT_ON_MACH_ERROR("host_get_host_priv_port", 1);
   
    processor_list = (processor_port_array_t)0;
    kr = host_processors(host_priv, &processor_list, &processor_count);
    EXIT_ON_MACH_ERROR("host_processors", 1);

    io_service_t serviceObject;

    serviceObject = IOServiceGetMatchingService(kIOMasterPortDefault,
                        IOServiceMatching("AppleLMUController"));
    if (serviceObject) {
        kr = IOServiceOpen(serviceObject, mach_task_self(), 0,
                           &lightsensor_port);
        IOObjectRelease(serviceObject);
        if (kr != KERN_SUCCESS) {
            lightsensor_port = 0;
        }
    }

    kr = KERN_FAILURE;
    CFDictionaryRef classToMatch;
    MotionSensorData_t sms_data;

    for (i = 0; i <= sms_maxConfigurationID; i++) {

        sms_gIndex = SMS_CONFIGURATIONS[i].index;
        classToMatch = IOServiceMatching(SMS_CONFIGURATIONS[i].classname);
        sms_gStructureInputSize = SMS_CONFIGURATIONS[i].structureInputSize;
        sms_gStructureOutputSize = SMS_CONFIGURATIONS[i].structureOutputSize;
        sms_configurationID = i;

        serviceObject = IOServiceGetMatchingService(kIOMasterPortDefault,
                                                    classToMatch);
        if (!serviceObject) {
            continue;
        }

        kr = IOServiceOpen(serviceObject, mach_task_self(), 0,
                           &motionsensor_port);
        IOObjectRelease(serviceObject);
        if (kr != KERN_SUCCESS) {
            continue;
        }

        kr = sms_getOrientation_hardware_apple(&sms_data);
        if (kr != KERN_SUCCESS) {
            IOServiceClose(motionsensor_port);
            motionsensor_port = 0;
            continue;
        } else {
            break;
        }
    }

    total_file_patterns =
        sizeof(procfs_file_table)/sizeof(struct procfs_dispatcher_entry);
    total_directory_patterns =
        sizeof(procfs_directory_table)/sizeof(struct procfs_dispatcher_entry);
    total_link_patterns =
        sizeof(procfs_link_table)/sizeof(struct procfs_dispatcher_entry);
   
    pthread_mutex_init(&camera_lock, NULL);
    pthread_mutex_init(&display_lock, NULL);

    camera_tiff = CFDataCreateMutable(kCFAllocatorDefault, (CFIndex)0);

    return NULL;
}

static void
procfs_destroy(void *arg)
{
    (void)mach_port_deallocate(mach_task_self(), p_default_set);
    (void)mach_port_deallocate(mach_task_self(), p_default_set_control);

    pthread_mutex_destroy(&camera_lock);
    pthread_mutex_destroy(&display_lock);

    CFRelease(camera_tiff);
}

#define PROCFS_OPEN_RELEASE_COMMON()                                          \
    int i;                                                                    \
    procfs_dispatcher_entry_t e;                                              \
    string arg1, arg2, arg3;                                                  \
    const char *real_argv[PROCFS_MAX_ARGS];                                   \
                                                                              \
    if (valid_process_pattern->PartialMatch(path, &arg1)) {                   \
        pid_t check_pid = atoi(arg1.c_str());                                 \
        if (getpgid(check_pid) == -1) {                                       \
            return -ENOENT;                                                   \
        }                                                                     \
    }                                                                         \
                                                                              \
    for (i = 0; i < PROCFS_MAX_ARGS; i++) {                                   \
        real_argv[i] = (char *)0;                                             \
    }                                                                         \
                                                                              \
    for (i = 0; i < total_file_patterns; i++) {                               \
        e = &procfs_file_table[i];                                            \
        if ((e->flag & PROCFS_FLAG_ISDOTFILE) & !procfs_ui) {                 \
            continue;                                                         \
        }                                                                     \
        switch (e->argc) {                                                    \
        case 0:                                                               \
            if (e->compiled_pattern->FullMatch(path)) {                       \
                goto out;                                                     \
            }                                                                 \
            break;                                                            \
                                                                              \
        case 1:                                                               \
            if (e->compiled_pattern->FullMatch(path, &arg1)) {                \
                real_argv[0] = arg1.c_str();                                  \
                goto out;                                                     \
            }                                                                 \
            break;                                                            \
                                                                              \
        case 2:                                                               \
            if (e->compiled_pattern->FullMatch(path, &arg1, &arg2)) {         \
                real_argv[0] = arg1.c_str();                                  \
                real_argv[1] = arg2.c_str();                                  \
                goto out;                                                     \
            }                                                                 \
            break;                                                            \
                                                                              \
        case 3:                                                               \
            if (e->compiled_pattern->FullMatch(path, &arg1, &arg2, &arg3)) {  \
                real_argv[0] = arg1.c_str();                                  \
                real_argv[1] = arg2.c_str();                                  \
                real_argv[2] = arg3.c_str();                                  \
                goto out;                                                     \
            }                                                                 \
            break;                                                            \
                                                                              \
        default:                                                              \
            break;                                                            \
        }                                                                     \
    }                                                                         \
                                                                              \
    for (i = 0; i < total_link_patterns; i++) {                               \
        e = &procfs_link_table[i];                                            \
        switch (e->argc) {                                                    \
        case 0:                                                               \
            if (e->compiled_pattern->FullMatch(path)) {                       \
                goto out;                                                     \
            }                                                                 \
            break;                                                            \
                                                                              \
        case 1:                                                               \
            if (e->compiled_pattern->FullMatch(path, &arg1)) {                \
                real_argv[0] = arg1.c_str();                                  \
                goto out;                                                     \
            }                                                                 \
            break;                                                            \
                                                                              \
        case 2:                                                               \
            if (e->compiled_pattern->FullMatch(path, &arg1, &arg2)) {         \
                real_argv[0] = arg1.c_str();                                  \
                real_argv[1] = arg2.c_str();                                  \
                goto out;                                                     \
            }                                                                 \
            break;                                                            \
                                                                              \
        case 3:                                                               \
            if (e->compiled_pattern->FullMatch(path, &arg1, &arg2, &arg3)) {  \
                real_argv[0] = arg1.c_str();                                  \
                real_argv[1] = arg2.c_str();                                  \
                real_argv[2] = arg3.c_str();                                  \
                goto out;                                                     \
            }                                                                 \
            break;                                                            \
                                                                              \
        default:                                                              \
            break;                                                            \
        }                                                                     \
    }                                                                         \
                                                                              \
    return -ENOENT;                                                           \
                                                                              \
out:                                                                          \

static int
procfs_open(const char *path, struct fuse_file_info *fi)
{
    PROCFS_OPEN_RELEASE_COMMON()

    return e->open(e, real_argv, path, fi);
}

static int
procfs_release(const char *path, struct fuse_file_info *fi)
{
    PROCFS_OPEN_RELEASE_COMMON()

    return e->release(e, real_argv, path, fi);
}

static int
procfs_opendir(const char *path, struct fuse_file_info *fi)
{
    return 0;
}

static int
procfs_releasedir(const char *path, struct fuse_file_info *fi)
{
    return 0;
}

static int
procfs_getattr(const char *path, struct stat *stbuf)
{
    int i;
    procfs_dispatcher_entry_t e;
    string arg1, arg2, arg3;
    const char *real_argv[PROCFS_MAX_ARGS];

    if (valid_process_pattern->PartialMatch(path, &arg1)) {
        pid_t check_pid = atoi(arg1.c_str());
        if (getpgid(check_pid) == -1) {
            return -ENOENT;
        }
    }

    for (i = 0; i < PROCFS_MAX_ARGS; i++) {
        real_argv[i] = (char *)0;
    }

    for (i = 0; i < total_directory_patterns; i++) {
        e = &procfs_directory_table[i];
        switch (e->argc) {
        case 0:
            if (e->compiled_pattern->FullMatch(path)) {
                goto out;
            }
            break;

        case 1:
            if (e->compiled_pattern->FullMatch(path, &arg1)) {
                real_argv[0] = arg1.c_str();
                goto out;
            }
            break;

        case 2:
            if (e->compiled_pattern->FullMatch(path, &arg1, &arg2)) {
                real_argv[0] = arg1.c_str();
                real_argv[1] = arg2.c_str();
                goto out;
            }
            break;

        case 3:
            if (e->compiled_pattern->FullMatch(path, &arg1, &arg2, &arg3)) {
                real_argv[0] = arg1.c_str();
                real_argv[1] = arg2.c_str();
                real_argv[2] = arg3.c_str();
                goto out;
            }
            break;

        default:
            break;
        }
    }

    for (i = 0; i < total_file_patterns; i++) {
        e = &procfs_file_table[i];
        if ((e->flag & PROCFS_FLAG_ISDOTFILE) & !procfs_ui) {
            continue;
        }
        switch (e->argc) {
        case 0:
            if (e->compiled_pattern->FullMatch(path)) {
                goto out;
            }
            break;

        case 1:
            if (e->compiled_pattern->FullMatch(path, &arg1)) {
                real_argv[0] = arg1.c_str();
                goto out;
            }
            break;

        case 2:
            if (e->compiled_pattern->FullMatch(path, &arg1, &arg2)) {
                real_argv[0] = arg1.c_str();
                real_argv[1] = arg2.c_str();
                goto out;
            }
            break;

        case 3:
            if (e->compiled_pattern->FullMatch(path, &arg1, &arg2, &arg3)) {
                real_argv[0] = arg1.c_str();
                real_argv[1] = arg2.c_str();
                real_argv[2] = arg3.c_str();
                goto out;
            }
            break;

        default:
            break;
        }
    }

    for (i = 0; i < total_link_patterns; i++) {
        e = &procfs_link_table[i];
        if ((e->flag & PROCFS_FLAG_ISDOTFILE) & !procfs_ui) {
            continue;
        }
        switch (e->argc) {
        case 0:
            if (e->compiled_pattern->FullMatch(path)) {
                goto out;
            }
            break;

        case 1:
            if (e->compiled_pattern->FullMatch(path, &arg1)) {
                real_argv[0] = arg1.c_str();
                goto out;
            }
            break;

        case 2:
            if (e->compiled_pattern->FullMatch(path, &arg1, &arg2)) {
                real_argv[0] = arg1.c_str();
                real_argv[1] = arg2.c_str();
                goto out;
            }
            break;

        case 3:
            if (e->compiled_pattern->FullMatch(path, &arg1, &arg2, &arg3)) {
                real_argv[0] = arg1.c_str();
                real_argv[1] = arg2.c_str();
                real_argv[2] = arg3.c_str();
                goto out;
            }
            break;

        default:
            break;
        }
    }

    return -ENOENT;

out:
    return e->getattr(e, real_argv, stbuf);
}


static int
procfs_readdir(const char             *path,
               void                   *buf,
               fuse_fill_dir_t        filler,
               off_t                  offset,
               struct fuse_file_info *fi)
{
    int i;
    procfs_dispatcher_entry_t e;
    string arg1, arg2, arg3;
    const char *real_argv[PROCFS_MAX_ARGS];

    if (valid_process_pattern->PartialMatch(path, &arg1)) {
        pid_t check_pid = atoi(arg1.c_str());
        if (getpgid(check_pid) == -1) {
            return -ENOENT;
        }
    }

    for (i = 0; i < PROCFS_MAX_ARGS; i++) {
        real_argv[i] = (char *)0;
    }

    for (i = 0; i < total_directory_patterns; i++) {

        e = &procfs_directory_table[i];

        switch (e->argc) {
        case 0:
            if (e->compiled_pattern->FullMatch(path)) {
                goto out;
            }
            break;

        case 1:
            if (e->compiled_pattern->FullMatch(path, &arg1)) {
                real_argv[0] = arg1.c_str();
                goto out;
            }
            break;

        case 2:
            if (e->compiled_pattern->FullMatch(path, &arg1, &arg2)) {
                real_argv[0] = arg1.c_str();
                real_argv[1] = arg2.c_str();
                goto out;
            }
            break;

        case 3:
            if (e->compiled_pattern->FullMatch(path, &arg1, &arg2, &arg3)) {
                real_argv[0] = arg1.c_str();
                real_argv[1] = arg2.c_str();
                real_argv[2] = arg3.c_str();
                goto out;
            }
            break;

        default:
            return -ENOENT;
        }
    }

    return -ENOENT;

out:
    (void)e->readdir(e, real_argv, buf, filler, offset, fi);

    (void)procfs_populate_directory(e->content_files, e->content_directories,
                                    buf, filler, offset, fi);

    return 0;
}

static int
procfs_readlink(const char *path, char *buf, size_t size)
{
    int i;
    procfs_dispatcher_entry_t e;
    string arg1, arg2, arg3;
    const char *real_argv[PROCFS_MAX_ARGS];

    for (i = 0; i < PROCFS_MAX_ARGS; i++) {
        real_argv[i] = (char *)0;
    }

    for (i = 0; i < total_link_patterns; i++) {

        e = &procfs_link_table[i];

        if ((e->flag & PROCFS_FLAG_ISDOTFILE) & !procfs_ui) {
            continue;
        }

        switch (e->argc) {
        case 0:
            if (e->compiled_pattern->FullMatch(path)) {
                goto out;
            }
            break;
        case 1:
            if (e->compiled_pattern->FullMatch(path, &arg1)) {
                real_argv[0] = arg1.c_str();
                goto out;
            }
            break;
        case 2:
            if (e->compiled_pattern->FullMatch(path, &arg1, &arg2)) {
                real_argv[0] = arg1.c_str();
                real_argv[1] = arg2.c_str();
                goto out;
            }
            break;
        }
    }

    return -ENOENT;

out:
    return e->readlink(e, real_argv, buf, size);
}

static int
procfs_read(const char *path, char *buf, size_t size, off_t offset,
            struct fuse_file_info *fi)
{
    int i;
    procfs_dispatcher_entry_t e;
    string arg1, arg2, arg3;
    const char *real_argv[PROCFS_MAX_ARGS];

    for (i = 0; i < PROCFS_MAX_ARGS; i++) {
        real_argv[i] = (char *)0;
    }

    for (i = 0; i < total_file_patterns; i++) {

        e = &procfs_file_table[i];

        if ((e->flag & PROCFS_FLAG_ISDOTFILE) & !procfs_ui) {
            continue;
        }

        switch (e->argc) {
        case 0:
            if (e->compiled_pattern->FullMatch(path)) {
                goto out;
            }
            break;

        case 1:
            if (e->compiled_pattern->FullMatch(path, &arg1)) {
                real_argv[0] = arg1.c_str();
                goto out;
            }
            break;

        case 2:
            if (e->compiled_pattern->FullMatch(path, &arg1, &arg2)) {
                real_argv[0] = arg1.c_str();
                real_argv[1] = arg2.c_str();
                goto out;
            }
            break;

        case 3:
            if (e->compiled_pattern->FullMatch(path, &arg1, &arg2, &arg3)) {
                real_argv[0] = arg1.c_str();
                real_argv[1] = arg2.c_str();
                real_argv[2] = arg3.c_str();
                goto out;
            }
            break;
    
        default:
            return -EIO;
        }
    }   

    return -EIO;
    
out:    
    return e->read(e, real_argv, buf, size, offset, fi);
}

static int
procfs_statfs(const char *path, struct statvfs *buf)
{
    (void)path;

    buf->f_namemax = 255;
    buf->f_bsize = 1048576;
    buf->f_frsize = 1048576;
    buf->f_blocks = buf->f_bfree = buf->f_bavail =
        1000ULL * 1024 * 1024 * 1024 / buf->f_frsize;
    buf->f_files = buf->f_ffree = 1000000000;
    return 0;
}

static struct fuse_operations procfs_oper;

static void
procfs_oper_populate(struct fuse_operations *oper)
{
    oper->init       = procfs_init;
    oper->destroy    = procfs_destroy;
    oper->statfs     = procfs_statfs;
    oper->open       = procfs_open;
    oper->release    = procfs_release;
    oper->opendir    = procfs_opendir;
    oper->releasedir = procfs_releasedir;
    oper->getattr    = procfs_getattr;
    oper->read       = procfs_read;
    oper->readdir    = procfs_readdir;
    oper->readlink   = procfs_readlink;
}

static char def_opts[] = "-oallow_other,direct_io,nobrowse,nolocalcaches,ro,iosize=1048576,volname=ProcFS";
static char def_opts_ui[] = "-oallow_other,local,nolocalcaches,ro,iosize=1048576,volname=ProcFS";

int
main(int argc, char *argv[])
{
    int i;
    char **new_argv;
    char *extra_opts = def_opts;

    if (getenv("OSXFUSE_PROCFS_UI")) {
        procfs_ui = 1;
        extra_opts = def_opts_ui;
    }

    argc++;
    new_argv = (char **)malloc(sizeof(char *) * argc);
    if (!new_argv)
        return -1;
    for (i = 0; i < (argc - 1); i++) {
        new_argv[i] = argv[i];
    }
    new_argv[i] = extra_opts;

    procfs_oper_populate(&procfs_oper);

    return fuse_main(argc, new_argv, &procfs_oper, NULL);
}
