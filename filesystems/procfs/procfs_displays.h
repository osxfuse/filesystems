/*
 * MacFUSE-Based procfs
 */

#ifndef _PROCFS_DISPLAYS_H_
#define _PROCFS_DISPLAYS_H_

#include <IOKit/graphics/IOGraphicsLib.h>
#include <CoreFoundation/CoreFoundation.h>

extern "C" {

CGDisplayCount PROCFS_GetDisplayCount(void);
int PROCFS_GetInfoForDisplayAtIndex(unsigned int index, char *buf,
                                    size_t *size);
int PROCFS_GetTIFFForDisplayAtIndex(unsigned int index, CFMutableDataRef *data);
off_t PROCFS_GetTIFFSizeForDisplayAtIndex(unsigned int index);

} /* extern "C" */

#endif /* _PROCFS_DISPLAYS_H_ */
