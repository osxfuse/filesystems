/*
 * windowfs as a MacFUSE file system for Mac OS X
 *
 * Copyright Amit Singh. All Rights Reserved.
 * http://osxbook.com
 *
 */

#define MACFUSE_WINDOWFS_VERSION "1.0"
#define FUSE_USE_VERSION 26

__attribute__((used)) static const char* copyright = "(C) Amit Singh <osxbook.com>, 2007-2008. All Rights Reserved.";

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

#include "GetPID.h"
#include "windowfs_windows.h"

static int total_file_patterns = 0;
static int total_directory_patterns = 0;
static int total_link_patterns = 0;

#define WINDOWFS_NAME            "GrabFS"
#define WINDOWFS_MNTPOINT        "/Volumes/" WINDOWFS_NAME
#define WINDOWFS_ROOTDIR_PATTERN ".+ \\((\\d+)\\)"

static pcrecpp::RE *valid_process_pattern =
    new pcrecpp::RE(WINDOWFS_ROOTDIR_PATTERN);

struct windowfs_dispatcher_entry;
typedef struct windowfs_dispatcher_entry * windowfs_dispatcher_entry_t;

typedef int (*windowfs_open_handler_t)(windowfs_dispatcher_entry_t e,
                                       const char                 *argv[],
                                       const char                 *path,
                                       struct fuse_file_info      *fi);

typedef int (*windowfs_release_handler_t)(windowfs_dispatcher_entry_t e,
                                          const char                 *argv[],
                                          const char                 *path,
                                          struct fuse_file_info      *fi);

typedef int (*windowfs_opendir_handler_t)(windowfs_dispatcher_entry_t e,
                                          const char                 *argv[],
                                          const char                 *path,
                                          struct fuse_file_info      *fi);

typedef int (*windowfs_releasedir_handler_t)(windowfs_dispatcher_entry_t e,
                                             const char                 *argv[],
                                             const char                 *path,
                                             struct fuse_file_info      *fi);

typedef int (*windowfs_getattr_handler_t)(windowfs_dispatcher_entry_t e,
                                          const char                 *argv[],
                                          struct stat                *stbuf);

typedef int (*windowfs_read_handler_t)(windowfs_dispatcher_entry_t e,
                                       const char                 *argv[],
                                       char                       *buf,
                                       size_t                      size,
                                       off_t                       offset,
                                       struct fuse_file_info      *fi);

typedef int (*windowfs_readdir_handler_t)(windowfs_dispatcher_entry_t e,
                                          const char                 *argv[],
                                          void                       *buf,
                                          fuse_fill_dir_t             filler,
                                          off_t                       offset,
                                          struct fuse_file_info      *fi);

typedef int (*windowfs_readlink_handler_t)(windowfs_dispatcher_entry_t e,
                                           const char                 *argv[],
                                           char                       *buf,
                                           size_t                     size);

typedef struct windowfs_dispatcher_entry {
    int                           flag;
    char                         *pattern;
    pcrecpp::RE                  *compiled_pattern;
    int                           argc;
    windowfs_open_handler_t       open;
    windowfs_release_handler_t    release;
    windowfs_opendir_handler_t    opendir;
    windowfs_releasedir_handler_t releasedir;
    windowfs_getattr_handler_t    getattr;
    windowfs_read_handler_t       read;
    windowfs_readdir_handler_t    readdir;
    windowfs_readlink_handler_t   readlink;
    const char                   *content_files[32];
    const char                   *content_directories[32];
};

#define WINDOWFS_MAX_ARGS 3

#define OPEN_HANDLER(handler) \
int \
windowfs_open_##handler(windowfs_dispatcher_entry_t e,      \
                        const char                 *argv[], \
                        const char                 *path,   \
                        struct fuse_file_info      *fi)     \

#define RELEASE_HANDLER(handler) \
int \
windowfs_release_##handler(windowfs_dispatcher_entry_t e,      \
                           const char                 *argv[], \
                           const char                 *path,   \
                           struct fuse_file_info      *fi)     \

#define OPENDIR_HANDLER(handler) \
int \
windowfs_opendir_##handler(windowfs_dispatcher_entry_t e,      \
                           const char                 *argv[], \
                           const char                 *path,   \
                           struct fuse_file_info      *fi)     \

#define RELEASEDIR_HANDLER(handler) \
int \
windowfs_releasedir_##handler(windowfs_dispatcher_entry_t e,      \
                              const char                 *argv[], \
                              const char                 *path,   \
                              struct fuse_file_info      *fi)     \

#define GETATTR_HANDLER(handler) \
int \
windowfs_getattr_##handler(windowfs_dispatcher_entry_t e,      \
                           const char                 *argv[], \
                           struct stat                *stbuf)  \

#define READ_HANDLER(handler) \
int \
windowfs_read_##handler(windowfs_dispatcher_entry_t e,      \
                        const char                 *argv[], \
                        char                       *buf,    \
                        size_t                      size,   \
                        off_t                       offset, \
                        struct fuse_file_info      *fi)     \

#define READDIR_HANDLER(handler) \
int \
windowfs_readdir_##handler(windowfs_dispatcher_entry_t e,      \
                           const char                 *argv[], \
                           void                       *buf,    \
                           fuse_fill_dir_t             filler, \
                           off_t                       offset, \
                           struct fuse_file_info      *fi)     \

#define READLINK_HANDLER(handler) \
int \
windowfs_readlink_##handler(windowfs_dispatcher_entry_t e,      \
                            const char                 *argv[], \
                            char                       *buf,    \
                            size_t                      size)   \

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
        windowfs_open_##openp,               \
        windowfs_release_##releasep,         \
        windowfs_opendir_enotdir,            \
        windowfs_releasedir_enotdir,         \
        windowfs_getattr_##getattrp,         \
        windowfs_read_##readp,               \
        windowfs_readdir_enotdir,            \
        windowfs_readlink_einval,            \
        { NULL },                            \
        { NULL }                             \
    },

#define DECL_FILE_WITHFLAGS(flag, pattern, argc, openp, releasep, getattrp, readp) \
    {                                        \
        flag,                                \
        pattern,                             \
        new pcrecpp::RE(pattern),            \
        argc,                                \
        windowfs_open_##openp,               \
        windowfs_release_##releasep,         \
        windowfs_opendir_enotdir,            \
        windowfs_releasedir_enotdir,         \
        windowfs_getattr_##getattrp,         \
        windowfs_read_##readp,               \
        windowfs_readdir_enotdir,            \
        windowfs_readlink_einval,            \
        { NULL },                            \
        { NULL }                             \
    },

#define DECL_DIRECTORY(pattern, argc, opendirp, releasedirp, getattrp, readdirp, contents, ...) \
    {                                        \
        0,                                   \
        pattern,                             \
        new pcrecpp::RE(pattern),            \
        argc,                                \
        windowfs_open_eisdir,                \
        windowfs_release_eisdir,             \
        windowfs_opendir_##opendirp,         \
        windowfs_releasedir_##releasedirp,   \
        windowfs_getattr_##getattrp,         \
        windowfs_read_eisdir,                \
        windowfs_readdir_##readdirp,         \
        windowfs_readlink_einval,            \
        contents,                            \
        __VA_ARGS__                          \
    },

#define DECL_DIRECTORY_COMPACT(pattern, contents, ...) \
    {                                          \
        0,                                     \
        pattern,                               \
        new pcrecpp::RE(pattern),              \
        0,                                     \
        windowfs_open_eisdir,                  \
        windowfs_release_eisdir,               \
        windowfs_opendir_default_directory,    \
        windowfs_releasedir_default_directory, \
        windowfs_getattr_default_directory,    \
        windowfs_read_eisdir,                  \
        windowfs_readdir_default,              \
        windowfs_readlink_einval,              \
        contents,                              \
        ##__VA_ARGS__                          \
    },

#define DECL_LINK(pattern, argc, openp, releasep, getattrp, readlinkp) \
    {                                        \
        0,                                   \
        pattern,                             \
        new pcrecpp::RE(pattern),            \
        argc,                                \
        windowfs_open_##openp,               \
        windowfs_release_##releasep,         \
        windowfs_opendir_enotdir,            \
        windowfs_releasedir_enotdir,         \
        windowfs_getattr_##getattrp,         \
        windowfs_read_einval,                \
        windowfs_readdir_enotdir,            \
        windowfs_readlink_##readlinkp,       \
        { NULL },                            \
        { NULL }                             \
    },

#define DECL_LINK_COMPACT(pattern, argc, readlinkp) \
    {                                        \
        0,                                   \
        pattern,                             \
        new pcrecpp::RE(pattern),            \
        argc,                                \
        windowfs_open_default_file,          \
        windowfs_release_default_file,       \
        windowfs_opendir_enotdir,            \
        windowfs_releasedir_enotdir,         \
        windowfs_getattr_default_link,       \
        windowfs_read_einval,                \
        windowfs_readdir_enotdir,            \
        windowfs_readlink_##readlinkp,       \
        { NULL },                            \
        { NULL }                             \
    },

PROTO_OPEN_HANDLER(default_file);
PROTO_OPEN_HANDLER(eisdir);
PROTO_OPEN_HANDLER(proc__window);

PROTO_RELEASE_HANDLER(default_file);
PROTO_RELEASE_HANDLER(eisdir);
PROTO_RELEASE_HANDLER(proc__window);

PROTO_OPENDIR_HANDLER(default_directory);
PROTO_OPENDIR_HANDLER(enotdir);

PROTO_RELEASEDIR_HANDLER(default_directory);
PROTO_RELEASEDIR_HANDLER(enotdir);

PROTO_GETATTR_HANDLER(default_file);
PROTO_GETATTR_HANDLER(default_directory);
PROTO_GETATTR_HANDLER(default_link);
PROTO_GETATTR_HANDLER(proc);
PROTO_GETATTR_HANDLER(proc__window);

PROTO_READ_HANDLER(einval);
PROTO_READ_HANDLER(eisdir);
PROTO_READ_HANDLER(zero);

PROTO_READ_HANDLER(proc__window);

PROTO_READDIR_HANDLER(default);
PROTO_READDIR_HANDLER(enotdir);

PROTO_READDIR_HANDLER(root);
PROTO_READDIR_HANDLER(proc);

PROTO_READLINK_HANDLER(einval);

static struct windowfs_dispatcher_entry
windowfs_link_table[] = {
};

static struct windowfs_dispatcher_entry
windowfs_file_table[] = {

    DECL_FILE(
        WINDOWFS_ROOTDIR_PATTERN "/([a-f\\d]+).tiff",
        2,
        proc__window, 
        proc__window, 
        proc__window,
        proc__window
    )

    DECL_FILE(
        "/\\.metadata_never_index",
        0,
        default_file,
        default_file,
        default_file,
        zero
    )

};

static struct windowfs_dispatcher_entry
windowfs_directory_table[] = {

    DECL_DIRECTORY(
        "/",
        0,
        default_directory,
        default_directory,
        default_directory,
        root,
        { ".metadata_never_index", NULL },
        { NULL },
    )

    DECL_DIRECTORY(
        //"/(\\d+) \\(.+\\)",
        WINDOWFS_ROOTDIR_PATTERN,
        1,
        default_directory,
        default_directory,
        proc,
        proc,
        { NULL },
        { NULL },
    )

};

// BEGIN: OPEN/OPENDIR

//
// int
// windowfs_open/opendir_<handler>(windowfs_dispatcher_entry_t  e,
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

OPEN_HANDLER(proc__window)
{
    if (fi->fh != 0) { /* XXX: need locking */
        return 0;
    }

    pid_t pid = strtol(argv[0], NULL, 10);
    CGWindowID target = strtol(argv[1], NULL, 16);

    WindowListData data = { 0, 0, { 0 } };
    data.pid = pid;
    int ret = WINDOWFS_GetWindowList(&data);

    if (ret <= 0) {
        return -ENOENT;
    }

    int i = 0;

    for (i = 0; i < data.windowCount; i++) {
        if (data.windowIDs[i] == target) {
            goto doread;
        }
    }

    return -ENOENT;

doread:

    CFMutableDataRef window_tiff = (CFMutableDataRef)0;
    ret = WINDOWFS_GetTIFFForWindowAtIndex(target, &window_tiff);

    if (ret == -1) {
        return -EIO;
    }

    struct WINDOWFSWindowData *pwd =
        (struct WINDOWFSWindowData *)malloc(sizeof(struct WINDOWFSWindowData));
    if (!pwd) {
        CFRelease(window_tiff);
    }

    pwd->window_tiff = window_tiff;
    pwd->max_len = WINDOWFS_GetTIFFSizeForWindowAtIndex(target);
    pwd->len = (size_t)CFDataGetLength(window_tiff);

    fi->fh = (uint64_t)pwd;

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
// windowfs_release/releasedir_<handler>(windowfs_dispatcher_entry_t  e,
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

RELEASE_HANDLER(proc__window)
{ 
    if (fi->fh) {
        struct WINDOWFSWindowData *pwd = (struct WINDOWFSWindowData *)(fi->fh);
        CFRelease((CFMutableDataRef)(pwd->window_tiff));
        free((void *)pwd);
        fi->fh = 0;
    }

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
//  windowfs_getattr_<handler>(windowfs_dispatcher_entry_t  e,
//                           const char                *argv[],
//                           struct stat               *stbuf)
                          

GETATTR_HANDLER(default_file)
{                         
    time_t current_time = time(NULL);
    stbuf->st_mode = S_IFREG | 0444;             
    stbuf->st_nlink = 1;  
    stbuf->st_size = 0;
    stbuf->st_atime = stbuf->st_ctime = stbuf->st_mtime = current_time;

    return 0;
}   

GETATTR_HANDLER(default_directory)
{
    time_t current_time = time(NULL);

    stbuf->st_mode = S_IFDIR | 0755;
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


GETATTR_HANDLER(proc)
{
    pid_t pid = strtol(argv[0], NULL, 10);

    if (pid == getpid()) {
        return -ENOENT;
    }

    time_t current_time = time(NULL);

    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 1;
    stbuf->st_size = 0;
    stbuf->st_atime = stbuf->st_ctime = stbuf->st_mtime = current_time;
    
    return 0;
}


GETATTR_HANDLER(proc__window)
{
    pid_t pid = strtol(argv[0], NULL, 10);
    CGWindowID target = strtol(argv[1], NULL, 16);

    WindowListData data = { 0, 0, { 0 } };
    data.pid = pid; 
    int ret = WINDOWFS_GetWindowList(&data);

    if (ret <= 0) {
        return -ENOENT;
    }

    int i = 0;

    for (i = 0; i < data.windowCount; i++) {
        if (data.windowIDs[i] == target) {
            time_t current_time = time(NULL);

            stbuf->st_mode = S_IFREG | 0644;
            stbuf->st_nlink = 1;
            stbuf->st_atime = stbuf->st_ctime = stbuf->st_mtime = current_time;
            stbuf->st_size = WINDOWFS_GetTIFFSizeForWindowAtIndex(data.windowIDs[i]);
            return 0;
        }
    }

    return -ENOENT;
}

// END: GETATTR


// BEGIN: READ

//    int
//    windowfs_read_<handler>(windowfs_dispatcher_entry_t  e,
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

READ_HANDLER(proc__window) 
{
    if (fi->fh == 0) {
        return 0;
    }

    struct WINDOWFSWindowData *pwd = (struct WINDOWFSWindowData *)fi->fh;

    CFMutableDataRef window_tiff = pwd->window_tiff;
    size_t max_len = pwd->max_len;
    size_t len = pwd->len;

    if (len > max_len) {
        return -EIO;
    }

    CFDataSetLength(window_tiff, max_len);
    len = max_len;

    const UInt8 *tmpbuf = CFDataGetBytePtr(window_tiff);
        
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
//    windowfs_readdir_<handler>(windowfs_dispatcher_entry_t *e,
//                             const char                *argv[],
//                             void                      *buf,
//                             fuse_fill_dir_t            filler,
//                             off_t                      offset,
//                             struct fuse_file_info     *fi)


int
windowfs_populate_directory(const char           **content_files,
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
    dir_stat.st_mode = S_IFDIR | 0755;
    dir_stat.st_size = 0;

    memset(&file_stat, 0, sizeof(file_stat));
    dir_stat.st_mode = S_IFREG | 0644;
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

out:
    return bufferfull;
}


READDIR_HANDLER(default)
{
    return 0;
}


READDIR_HANDLER(enotdir)
{
    return -ENOTDIR;
}


READDIR_HANDLER(proc)
{
    int i;
    pid_t pid = strtol(argv[0], NULL, 10);
    struct stat dir_stat;
    char the_name[MAXNAMLEN + 1];

    memset(&dir_stat, 0, sizeof(dir_stat));
    dir_stat.st_mode = S_IFDIR | 0755;
    dir_stat.st_size = 0;

    WindowListData data = { 0, 0, { 0 } };
    data.pid = pid;
    int ret = WINDOWFS_GetWindowList(&data);

    if (ret < 0) {
        return -EIO;
    }

    if (data.windowCount == 0) {
        return 0;
    }

    for (i = 0; i < data.windowCount; i++) {
        snprintf(the_name, MAXNAMLEN, "%x.tiff", data.windowIDs[i]);
        dir_stat.st_mode = S_IFREG | 0644;
        dir_stat.st_size = WINDOWFS_GetTIFFSizeForWindowAtIndex(data.windowIDs[i]);
        if (filler(buf, the_name, &dir_stat, 0)) {
            break;
        }
    }

    return 0;
}


READDIR_HANDLER(root)
{
    char the_name_app[MAXNAMLEN + 1];  
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
        if (Pid == getpid()) {
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
        the_stat.st_mode = S_IFDIR | 0755;
        the_stat.st_nlink = 1;
        the_stat.st_size = 0;

        strstatus = CFStringGetCString(Pname, the_name_app, MAXNAMLEN,
                                       kCFStringEncodingASCII);
        if (strstatus == false) {
            CFRelease(Pname);
            Pname = (CFStringRef)0;
            continue;
        }

        snprintf(the_name, MAXNAMLEN, "%s (%d)", the_name_app, Pid);

        if (filler(buf, the_name, &the_stat, 0)) {
            CFRelease(Pname);
            break;
        }
        CFRelease(Pname);
    }

    Pid = GetPIDForProcessName("WindowServer");
    if (Pid != -1) {
        the_stat.st_mode = S_IFDIR | 0755;
        the_stat.st_nlink = 1;
        the_stat.st_size = 0;
        snprintf(the_name, MAXNAMLEN, "WindowServer (%d)", Pid);
        (void)filler(buf, the_name, &the_stat, 0);
    }

    return 0;
}

// END: READDIR


// BEGIN: READLINK

//    int
//    windowfs_readlink_<handler>(windowfs_dispatcher_entry_t  e,
//                              const char                *argv[],
//                              char                      *buf,
//                              size_t                     size)

READLINK_HANDLER(einval)
{
    return -EINVAL;
}

// END: READLINK


static void *
windowfs_init(struct fuse_conn_info *conn)
{
    total_file_patterns =
      sizeof(windowfs_file_table)/sizeof(struct windowfs_dispatcher_entry);

    total_directory_patterns =
      sizeof(windowfs_directory_table)/sizeof(struct windowfs_dispatcher_entry);

    total_link_patterns =
      sizeof(windowfs_link_table)/sizeof(struct windowfs_dispatcher_entry);

    return NULL;
}

static void
windowfs_destroy(void *arg)
{
}

#define WINDOWFS_OPEN_RELEASE_COMMON()                                        \
    int i;                                                                    \
    windowfs_dispatcher_entry_t e;                                            \
    string arg1, arg2, arg3;                                                  \
    const char *real_argv[WINDOWFS_MAX_ARGS];                                 \
                                                                              \
    if (valid_process_pattern->PartialMatch(path, &arg1)) {                   \
        pid_t check_pid = atoi(arg1.c_str());                                 \
        if (getpgid(check_pid) == -1) {                                       \
            return -ENOENT;                                                   \
        }                                                                     \
    }                                                                         \
                                                                              \
    for (i = 0; i < WINDOWFS_MAX_ARGS; i++) {                                 \
        real_argv[i] = (char *)0;                                             \
    }                                                                         \
                                                                              \
    for (i = 0; i < total_file_patterns; i++) {                               \
        e = &windowfs_file_table[i];                                          \
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
        e = &windowfs_link_table[i];                                          \
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
windowfs_open(const char *path, struct fuse_file_info *fi)
{
    WINDOWFS_OPEN_RELEASE_COMMON()

    return e->open(e, real_argv, path, fi);
}

static int
windowfs_release(const char *path, struct fuse_file_info *fi)
{
    WINDOWFS_OPEN_RELEASE_COMMON()

    return e->release(e, real_argv, path, fi);
}

static int
windowfs_opendir(const char *path, struct fuse_file_info *fi)
{
    return 0;
}

static int
windowfs_releasedir(const char *path, struct fuse_file_info *fi)
{
    return 0;
}

static int
windowfs_getattr(const char *path, struct stat *stbuf)
{
    int i;
    windowfs_dispatcher_entry_t e;
    string arg1, arg2, arg3;
    const char *real_argv[WINDOWFS_MAX_ARGS];

    if (valid_process_pattern->PartialMatch(path, &arg1)) {
        pid_t check_pid = atoi(arg1.c_str());
        if (getpgid(check_pid) == -1) {
            return -ENOENT;
        }
    }

    for (i = 0; i < WINDOWFS_MAX_ARGS; i++) {
        real_argv[i] = (char *)0;
    }

    for (i = 0; i < total_directory_patterns; i++) {
        e = &windowfs_directory_table[i];
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
        e = &windowfs_file_table[i];
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
        e = &windowfs_link_table[i];
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
windowfs_readdir(const char           *path,
               void                   *buf,
               fuse_fill_dir_t        filler,
               off_t                  offset,
               struct fuse_file_info *fi)
{
    int i;
    windowfs_dispatcher_entry_t e;
    string arg1, arg2, arg3;
    const char *real_argv[WINDOWFS_MAX_ARGS];

    if (valid_process_pattern->PartialMatch(path, &arg1)) {
        pid_t check_pid = atoi(arg1.c_str());
        if (getpgid(check_pid) == -1) {
            return -ENOENT;
        }
    }

    for (i = 0; i < WINDOWFS_MAX_ARGS; i++) {
        real_argv[i] = (char *)0;
    }

    for (i = 0; i < total_directory_patterns; i++) {

        e = &windowfs_directory_table[i];

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

    (void)windowfs_populate_directory(e->content_files, e->content_directories,
                                    buf, filler, offset, fi);

    return 0;
}

static int
windowfs_readlink(const char *path, char *buf, size_t size)
{
    int i;
    windowfs_dispatcher_entry_t e;
    string arg1, arg2, arg3;
    const char *real_argv[WINDOWFS_MAX_ARGS];

    for (i = 0; i < WINDOWFS_MAX_ARGS; i++) {
        real_argv[i] = (char *)0;
    }

    for (i = 0; i < total_link_patterns; i++) {

        e = &windowfs_link_table[i];

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
windowfs_read(const char *path, char *buf, size_t size, off_t offset,
            struct fuse_file_info *fi)
{
    int i;
    windowfs_dispatcher_entry_t e;
    string arg1, arg2, arg3;
    const char *real_argv[WINDOWFS_MAX_ARGS];

    for (i = 0; i < WINDOWFS_MAX_ARGS; i++) {
        real_argv[i] = (char *)0;
    }

    for (i = 0; i < total_file_patterns; i++) {

        e = &windowfs_file_table[i];

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
windowfs_statfs(const char *path, struct statvfs *buf)
{
    (void)path;

    buf->f_namemax = 255;
    buf->f_bsize = 2097152;
    buf->f_frsize = 2097152;
    buf->f_blocks = 1000ULL * 1024 * 1024 * 1024 / buf->f_frsize;
    buf->f_bfree = buf->f_bavail = 0;
    buf->f_files = 1000000000;
    buf->f_ffree = 0;
    return 0;
}

static struct fuse_operations windowfs_oper;

static void
windowfs_oper_populate(struct fuse_operations *oper)
{
    oper->init       = windowfs_init;
    oper->destroy    = windowfs_destroy;
    oper->statfs     = windowfs_statfs;
    oper->open       = windowfs_open;
    oper->release    = windowfs_release;
    oper->opendir    = windowfs_opendir;
    oper->releasedir = windowfs_releasedir;
    oper->getattr    = windowfs_getattr;
    oper->read       = windowfs_read;
    oper->readdir    = windowfs_readdir;
    oper->readlink   = windowfs_readlink;
}

static char *def_opts = "-odirect_io,iosize=2097152,local,noappledouble,nolocalcaches,ro,fsname=GrabFS,volname=GrabFS Volume";

int
main(int argc, char *argv[])
{
    int i;
    char **new_argv;
    char *extra_opts = def_opts;

    argc += 3;
    new_argv = (char **)malloc(sizeof(char *) * argc);

    new_argv[0] = argv[0];
    new_argv[1] = WINDOWFS_MNTPOINT;
    new_argv[2] = extra_opts;
    new_argv[3] = "-f";
    for (i = 1; i < (argc - 3); i++) {
        new_argv[i + 3] = argv[i];
    }

    errno = 0;

    int ret = mkdir(WINDOWFS_MNTPOINT, 0755);
    if (ret && (errno != EEXIST)) {
        perror("mkdir");
        exit(ret);
    }

    windowfs_oper_populate(&windowfs_oper);

    {
        /* Make Carbon & Co. Happy */

        pid_t Pid;
        ProcessSerialNumber psn = { kNoProcess, kNoProcess };

        (void)GetNextProcess(&psn);
        (void)GetProcessPID(&psn, &Pid);
    }

    return fuse_main(argc, new_argv, &windowfs_oper, NULL);
}
