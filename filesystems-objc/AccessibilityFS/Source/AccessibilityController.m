// ================================================================
// Copyright (C) 2008 Google Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//      http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// ================================================================
//
//  AccessibilityController.m
//  AccessibilityFS
//

#import "AccessibilityController.h"
#import "AccessibilityFS.h"
#import <MacFUSE/GMUserFileSystem.h>
#import "GTMAXUIElement.h"

NSString *const kMountPath = @"/Volumes/Accessibility";

@implementation AccessibilityController

- (void)applicationDidBecomeActive:(NSNotification *)notification {
  NSString* parentPath = [kMountPath stringByDeletingLastPathComponent];
  [[NSWorkspace sharedWorkspace] selectFile:kMountPath
                   inFileViewerRootedAtPath:parentPath];
}

- (void)didMount:(NSNotification *)notification {
  [self applicationDidBecomeActive:notification];
}

- (void)didUnmount:(NSNotification*)notification {  
  [[NSApplication sharedApplication] terminate:nil];
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
  if (![GTMAXUIElement isAccessibilityEnabled]) {
    NSAlert *alert = [[[NSAlert alloc] init] autorelease];
    [alert setMessageText:NSLocalizedString(@"Can't start AccessibilityFS", 
                                            @"Can't start error")];
    [alert setInformativeText:NSLocalizedString(@"Please 'Enable access for assistive devices' in the 'Universal Access' System preference panel.", 
                                                @"Can't start help")];
    [alert runModal];
    [NSApp terminate:self];
  }
  NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
  [center addObserver:self selector:@selector(didMount:)
                 name:kGMUserFileSystemDidMount object:nil];
  [center addObserver:self selector:@selector(didUnmount:)
                 name:kGMUserFileSystemDidUnmount object:nil];
  
  fs_ = [[GMUserFileSystem alloc] initWithDelegate:[[AccessibilityFS alloc] init]
                                      isThreadSafe:NO];
  
  NSMutableArray* options = [NSMutableArray array];
  NSString* volArg = 
  [NSString stringWithFormat:@"volicon=%@", 
   [[NSBundle mainBundle] pathForResource:@"AccessibilityFSMount" ofType:@"icns"]];
  [options addObject:volArg];
  [options addObject:@"volname=Accessibility"];
  // Turn on for tons of fun debugging spew
  // [options addObject:@"debug"];
  [fs_ mountAtPath:kMountPath 
       withOptions:options];
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [fs_ unmount];
  id delegate = [fs_ delegate];
  [fs_ release];
  [delegate release];
  return NSTerminateNow;
}

@end
