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
//  HelloController
//  HelloFS
//
//  Created by ted on 1/3/08.
//
#import "HelloController.h"
#import "HelloFuseFileSystem.h"
#import <MacFUSE/GMUserFileSystem.h>

@implementation HelloController

- (void)didMount:(NSNotification *)notification {
  NSDictionary* userInfo = [notification userInfo];
  NSString* mountPath = [userInfo objectForKey:kGMUserFileSystemMountPathKey];
  NSString* parentPath = [mountPath stringByDeletingLastPathComponent];
  [[NSWorkspace sharedWorkspace] selectFile:mountPath
                   inFileViewerRootedAtPath:parentPath];
}

- (void)didUnmount:(NSNotification*)notification {
  [[NSApplication sharedApplication] terminate:nil];
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
  NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
  [center addObserver:self selector:@selector(didMount:)
                 name:kGMUserFileSystemDidMount object:nil];
  [center addObserver:self selector:@selector(didUnmount:)
                 name:kGMUserFileSystemDidUnmount object:nil];
  
  NSString* mountPath = @"/Volumes/Hello";
  HelloFuseFileSystem* hello = [[HelloFuseFileSystem alloc] init];
  fs_ = [[GMUserFileSystem alloc] initWithDelegate:hello isThreadSafe:YES];
  NSMutableArray* options = [NSMutableArray array];
  [options addObject:@"rdonly"];
  [options addObject:@"volname=HelloFS"];
  [options addObject:[NSString stringWithFormat:@"volicon=%@", 
    [[NSBundle mainBundle] pathForResource:@"Fuse" ofType:@"icns"]]];
  [fs_ mountAtPath:mountPath withOptions:options];
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [fs_ unmount];  // Just in case we need to unmount;
  [[fs_ delegate] release];  // Clean up HelloFS
  [fs_ release];
  return NSTerminateNow;
}

@end
