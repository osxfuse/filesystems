//
//  GoogleShared_SystemVersion.m
//
//  Created by Dave MacLachlan on 04/18/07.
//  Copyright 2007 Google, Inc. All rights reserved.
//

#import "GoogleShared_SystemVersion.h"
#import <Carbon/Carbon.h>
#import <stdlib.h>

@implementation GoogleShared_SystemVersion

+ (BOOL)isPanther {
  long major, minor;
  [self getMajor:&major minor:&minor bugFix:nil];
  return major == 10 && minor == 3;
}

+ (BOOL)isTiger {
  long major, minor;
  [self getMajor:&major minor:&minor bugFix:nil];
  return major == 10 && minor == 4;
}

+ (BOOL)isLeopard {
  long major, minor;
  [self getMajor:&major minor:&minor bugFix:nil];
  return major == 10 && minor == 5;
}

+ (BOOL)isPantherOrGreater {
  long major, minor;
  [self getMajor:&major minor:&minor bugFix:nil];
  return (major > 10) || (major == 10 && minor >= 3);
}

+ (BOOL)isTigerOrGreater {
  long major, minor;
  [self getMajor:&major minor:&minor bugFix:nil];
  return (major > 10) || (major == 10 && minor >= 4);
}

+ (BOOL)isLeopardOrGreater {
  long major, minor;
  [self getMajor:&major minor:&minor bugFix:nil];
  return (major > 10) || (major == 10 && minor >= 5);
}

+ (void)getMajor:(long*)major minor:(long*)minor bugFix:(long*)bugFix {
  OSStatus err = noErr;
  long binaryCodedDec;
  
  if (major) {
    require_noerr(Gestalt(gestaltSystemVersionMajor, major), failedGestalt);
  }
  if (minor) {
    require_noerr(Gestalt(gestaltSystemVersionMinor, minor), failedGestalt);
  }
  if (bugFix) {
    require_noerr(Gestalt(gestaltSystemVersionBugFix, bugFix), failedGestalt);
  }
  return;

failedGestalt:
  // gestaltSystemVersionMajor et al are only on 10.4 and above, so they
  // could fail if we have this code on 10.3.
  err = Gestalt(gestaltSystemVersion, &binaryCodedDec);
  if (err) {
    // Theory being if gestalt can't handle this basic selector, something
    // serious is wrong.
    abort();
  }
  
  // Note that this code will return x.9.9 for any system rev parts that are
  // greater than 9 (ie 10.10.10 will be 10.9.9. This shouldn't ever be a
  // problem  as the code above takes care of this for any system above 10.4.
  if (major) {
    int msb = (binaryCodedDec & 0x0000F000L) >> 12;
    msb *= 10;
    int lsb = (binaryCodedDec & 0x00000F00L) >> 8;
    *major = msb + lsb;
  }
  if (minor) {
    *minor = (binaryCodedDec & 0x000000F0L) >> 4;
  }
  if (bugFix) {
    *bugFix = (binaryCodedDec & 0x0000000FL);
  }
}

@end
