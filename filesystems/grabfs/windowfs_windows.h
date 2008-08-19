/*
 * MacFUSE-Based windowfs
 *
 * Copyright Amit Singh. All Rights Reserved.
 * http://osxbook.com
 */

#ifndef _WINDOWFS_WINDOWS_H_
#define _WINDOWFS_WINDOWS_H_

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>

extern "C" {

#define MAX_WINDOWS 256

typedef mach_port_t   CGSConnectionID;
typedef mach_port_t   CGSWindowID;

typedef struct {
    pid_t pid;
    int windowCount;
    CGSWindowID windowIDs[MAX_WINDOWS];
} WindowListData;

extern CGSConnectionID _CGSDefaultConnection(void);
extern CGError CGSGetWindowLevel(CGSConnectionID connectionID,
                                 CGSWindowID windowID, CGWindowLevel *level);
extern CGError CGSGetConnectionIDForPSN(CGSConnectionID connectionID,
                                        ProcessSerialNumber *psn,
                                        CGSConnectionID *out);
extern CGError CGSGetOnScreenWindowList(CGSConnectionID connectionID,
                                        CGSConnectionID targetConnectionID,
                                        int maxCount,
                                        CGSWindowID *windowList,
                                        int *outCount);
extern CGError CGSGetWindowList(CGSConnectionID connectionID,
                                CGSConnectionID targetConnectionID,
                                int maxCount,
                                CGSWindowID *windowList,
                                int *outCount);
extern CGError CGSGetScreenRectForWindow(CGSConnectionID connectionID,
                                         CGSWindowID windowID, CGRect *outRect);

extern CGError CGSGetParentWindowList(CGSConnectionID connectionID,
                                        CGSConnectionID targetConnectionID,
                                        int maxCount,
                                        CGSWindowID *windowList,
                                        int *outCount);

int WINDOWFS_GetTIFFForWindowAtIndex(CGWindowID index, CFMutableDataRef *data);
off_t WINDOWFS_GetTIFFSizeForWindowAtIndex(CGWindowID index);
int WINDOWFS_GetWindowList(WindowListData *data);

struct WINDOWFSWindowData {
    CFMutableDataRef window_tiff;
    size_t           len;
    size_t           max_len;
};

} /* extern "C" */

#endif /* _WINDOWFS_WINDOWS_H_ */
