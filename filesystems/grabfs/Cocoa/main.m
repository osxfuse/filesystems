/*
 * Copyright Amit Singh. All Rights Reserved.
 * http://osxbook.com
 */

#import <Cocoa/Cocoa.h>

int
main(int argc, char *argv[])
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    NSString *bin = [[NSBundle mainBundle] pathForResource:@"grabfs"
                                                    ofType:NULL];
    NSString *icns = [[NSBundle mainBundle] pathForResource:@"GrabFSVolume"
                                                     ofType:@"icns"];

    const char *bin_utf8 = [bin UTF8String];
    const char *icns_utf8 = [icns UTF8String];
    
    char volicon_arg[4096];
    snprintf(volicon_arg, 4096, "-ovolicon=%s", icns_utf8);

    if (fork() == 0) {
        execl(bin_utf8, bin_utf8, volicon_arg, NULL);
    }

    [pool release];

    exit(0);
}
