#ifndef _PROCFS_SEQUENCEGRAB_H_
#define _PROCFS_SEQUENCEGRAB_H_

#include <CoreFoundation/CoreFoundation.h>

#define CAMERA_TRIGGER_THRESHOLD 4
#define CAMERA_ACTIVE_DURATION   1

int   PROCFS_GetTIFFFromCamera(CFMutableDataRef *data);
off_t PROCFS_GetTIFFSizeFromCamera(void);

#endif /* _PROCFS_SEQUENCEGRAB_H_ */
