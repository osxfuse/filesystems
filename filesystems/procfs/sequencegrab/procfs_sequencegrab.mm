#import "procfs_sequencegrab.h"
#import "CocoaSequenceGrabber.h"

static int loopCount = 0;

@interface Snapshot : CSGCamera
{
    CFMutableDataRef tiff;
}

- (void)setDataRef:(CFMutableDataRef)dataRef;
- (void)camera:(CSGCamera *)aCamera didReceiveFrame:(CSGImage *)aFrame;

@end

@implementation Snapshot

- (void)setDataRef:(CFMutableDataRef)dataRef
{
    tiff = dataRef;
}

- (void)camera:(CSGCamera *)aCamera didReceiveFrame:(CSGImage *)aFrame;
{
    if (++loopCount == CAMERA_TRIGGER_THRESHOLD) {
        CFDataRef image = (CFDataRef)[aFrame TIFFRepresentationUsingCompression:NSTIFFCompressionNone factor:0.0];
        CFIndex len = CFDataGetLength(image);
        CFDataSetLength(tiff, len);
        CFDataReplaceBytes(tiff, CFRangeMake((CFIndex)0, len), CFDataGetBytePtr(image), len);
        //CFRelease(image);
        [self stop];
    }
}

@end

off_t
PROCFS_GetTIFFSizeFromCamera(void)
{
    return (off_t)((off_t)(640 * 480 * 4) + (off_t)8192);
}

int
PROCFS_GetTIFFFromCamera(CFMutableDataRef *data)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    loopCount = 0;

    Snapshot *camera = [[Snapshot alloc] init];
    [camera setDataRef:*data];
    [camera setDelegate:camera];
    [camera startWithSize:NSMakeSize(640, 480)];
    [[NSRunLoop currentRunLoop]
        runUntilDate:[NSDate dateWithTimeIntervalSinceNow:CAMERA_ACTIVE_DURATION]];
    [camera release];

    [pool release];

    return 0;
}
