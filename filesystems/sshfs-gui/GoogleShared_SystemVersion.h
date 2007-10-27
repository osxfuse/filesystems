//
//  GoogleShared_SystemVersion.h
//  SharedTest
//
//  Created by Dave MacLachlan on 04/18/07.
//  Copyright 2007 Google, Inc. All rights reserved.
//

#import <Foundation/Foundation.h>

/// A class for getting information about what system we are running on
@interface GoogleShared_SystemVersion : NSObject

/// Returns YES if running on 10.3, NO otherwise.
+ (BOOL)isPanther;

/// Returns YES if running on 10.4, NO otherwise.
+ (BOOL)isTiger;

/// Returns YES if running on 10.5, NO otherwise.
+ (BOOL)isLeopard;

/// Returns a YES/NO if the system is 10.3 or better
+ (BOOL)isPantherOrGreater;

/// Returns a YES/NO if the system is 10.4 or better
+ (BOOL)isTigerOrGreater;

/// Returns a YES/NO if the system is 10.5 or better
+ (BOOL)isLeopardOrGreater;

/// Returns the current system version major.minor.bugFix
+ (void)getMajor:(long*)major minor:(long*)minor bugFix:(long*)bugFix;
@end
