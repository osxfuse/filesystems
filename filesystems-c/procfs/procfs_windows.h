/*
 * MacFUSE-Based procfs
 */

#ifndef _PROCFS_WINDOWS_H_
#define _PROCFS_WINDOWS_H_

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>

extern "C" {

typedef mach_port_t   CGSConnectionID;
typedef mach_port_t   CGSWindowID;

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

int PROCFS_GetPNGForWindowAtIndex(CGWindowID index, CFMutableDataRef *data);
off_t PROCFS_GetPNGSizeForWindowAtIndex(CGWindowID index);

struct ProcfsWindowData {
    CFMutableDataRef window_png;
    size_t           len;
    size_t           max_len;
};

} /* extern "C" */

#endif /* _PROCFS_WINDOWS_H_ */
