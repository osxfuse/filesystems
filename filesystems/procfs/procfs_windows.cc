/*
 * MacFUSE-Based procfs
 * Windows
 *
 */

#include <ApplicationServices/ApplicationServices.h>

extern "C" {

#include "procfs_windows.h"

off_t
PROCFS_GetPNGSizeForWindowAtIndex(CGWindowID index)
{
    CGRect rect;

    CGError err = CGSGetScreenRectForWindow(_CGSDefaultConnection(), index,
                                            &rect);
    if (err) {
        return (off_t)0;
    }

    off_t size = ((off_t)rect.size.width  * (off_t)rect.size.height * (off_t)3)
                  + (off_t)8192;

    return size;
}

int
PROCFS_GetPNGForWindowAtIndex(CGWindowID index, CFMutableDataRef *data)
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
        CGImageDestinationCreateWithData((CFMutableDataRef)*data, kUTTypePNG,
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
