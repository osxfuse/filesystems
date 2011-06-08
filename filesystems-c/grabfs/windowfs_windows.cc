/*
 * MacFUSE-Based windowfs
 *
 * Copyright Amit Singh. All Rights Reserved.
 * http://osxbook.com
 *
 */

#include <ApplicationServices/ApplicationServices.h>

extern "C" {

#include "windowfs_windows.h"

static void
WINDOWFS_WindowListApplierFunction(const void *inputDictionary, void *context)
{
    CFDictionaryRef entry = (CFDictionaryRef)inputDictionary;
    WindowListData *data = (WindowListData *)context;

    CFNumberRef sharingState; /* SInt32 */
    CFNumberRef windowNumber; /* SInt64 */
    CFNumberRef ownerPID;     /* SInt64 */

    Boolean status;
    SInt32 sint32;
    SInt64 sint64;

    status = CFDictionaryGetValueIfPresent(entry, kCGWindowSharingState,
                                           (const void **)&sharingState);
    if (status == true) {
        status = CFNumberGetValue(sharingState, kCFNumberSInt32Type,
                                  (void *)&sint32);
        CFRelease(sharingState);
        if ((status == true) && (sint32 == 0)) {
            return;
        }
    } else {
        /* proceed */
    }
    
    status = CFDictionaryGetValueIfPresent(entry, kCGWindowOwnerPID,
                                           (const void **)&ownerPID);
    if (status == true) {
        status = CFNumberGetValue(ownerPID, kCFNumberSInt64Type,
                                  (void *)&sint64); 
        CFRelease(ownerPID); 
        if (status == true) {
            if ((pid_t)sint64 != data->pid) {
                return;
            }
        } else {
            return;
        }
    } else {
        return;
    }

    status = CFDictionaryGetValueIfPresent(entry, kCGWindowNumber,
                                           (const void **)&windowNumber);
    if (status == true) {
        status = CFNumberGetValue(windowNumber, kCFNumberSInt64Type,
                                  (void *)&sint64); 
        CFRelease(windowNumber);
        if (status == true) {
            data->windowIDs[data->windowCount] = (CGSWindowID)sint64;
            data->windowCount++;
        } else {
            return;
        }
    } else {
        return;
    }
}

int
WINDOWFS_GetWindowList(WindowListData *data)
{
    CFArrayRef windowList = CGWindowListCopyWindowInfo(
                                kCGWindowListOptionOnScreenOnly,
                                kCGNullWindowID);
    if (!windowList) {
        return -1;
    }

    CFArrayApplyFunction(windowList,
                         CFRangeMake(0, CFArrayGetCount(windowList)),
                         &WINDOWFS_WindowListApplierFunction, (void *)data);

    return data->windowCount;
}

off_t
WINDOWFS_GetTIFFSizeForWindowAtIndex(CGWindowID index)
{
    CGRect rect;

    CGError err = CGSGetScreenRectForWindow(_CGSDefaultConnection(), index,
                                            &rect);
    if (err) {
        return (off_t)0;
    }

    off_t size = ((off_t)rect.size.width  * (off_t)rect.size.height * (off_t)4)
                  + (off_t)8192;

    return size;
}

int
WINDOWFS_GetTIFFForWindowAtIndex(CGWindowID index, CFMutableDataRef *data)
{
    *data = (CFMutableDataRef)0;

    CGImageRef image = CGWindowListCreateImage(
                           CGRectNull, kCGWindowListOptionIncludingWindow,
                           index, kCGWindowImageBoundsIgnoreFraming);

    if (!image) {
        return -1;
    }

    *data = CFDataCreateMutable(kCFAllocatorDefault, 0);
    if (!*data) {
        CFRelease(image);
        return -1;
    }

    CGImageDestinationRef dest = 
        CGImageDestinationCreateWithData((CFMutableDataRef)*data, kUTTypeTIFF,
                                         1, nil);
    if (!dest) {
        CFRelease(*data);
        CFRelease(image);
        return -1;
    }

    CGImageDestinationAddImage(dest, image, nil);
    CGImageDestinationFinalize(dest);

    CFRelease(dest);
    CGImageRelease(image);

    return 0;
}

} /* extern "C" */
