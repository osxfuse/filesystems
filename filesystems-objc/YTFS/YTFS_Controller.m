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
//  YTFS_Controller.m
//  YTFS
//
//  Created by ted on 12/7/08.
//
#import "YTFS_Controller.h"
#import "YTFS_Filesystem.h"
#import "YTVideo.h"
#import <MacFUSE/MacFUSE.h>

@implementation YTFS_Controller

- (void)mountFailed:(NSNotification *)notification {
  NSDictionary* userInfo = [notification userInfo];
  NSError* error = [userInfo objectForKey:kGMUserFileSystemErrorKey];
  NSLog(@"kGMUserFileSystem Error: %@, userInfo=%@", error, [error userInfo]);  
  NSRunAlertPanel(@"Mount Failed", [error localizedDescription], nil, nil, nil);
  [[NSApplication sharedApplication] terminate:nil];
}

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
  // Pump up our url cache.
  NSURLCache* cache = [NSURLCache sharedURLCache];
  [cache setDiskCapacity:(1024 * 1024 * 500)];
  [cache setMemoryCapacity:(1024 * 1024 * 40)];
  
  NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
  [center addObserver:self selector:@selector(mountFailed:)
                 name:kGMUserFileSystemMountFailed object:nil];
  [center addObserver:self selector:@selector(didMount:)
                 name:kGMUserFileSystemDidMount object:nil];
  [center addObserver:self selector:@selector(didUnmount:)
                 name:kGMUserFileSystemDidUnmount object:nil];
  
  NSString* mountPath = @"/Volumes/YTFS";
  fs_delegate_ = 
    [[YTFS_Filesystem alloc] initWithVideos:[YTVideo fetchTopRatedVideos]];
  fs_ = [[GMUserFileSystem alloc] initWithDelegate:fs_delegate_ isThreadSafe:YES];

  NSMutableArray* options = [NSMutableArray array];
  NSString* volArg = 
    [NSString stringWithFormat:@"volicon=%@", 
     [[NSBundle mainBundle] pathForResource:@"YTFS" ofType:@"icns"]];
  [options addObject:volArg];
  [options addObject:@"volname=YTFS"];
  [options addObject:@"rdonly"];
  [fs_ mountAtPath:mountPath withOptions:options];
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [fs_ unmount];
  [fs_ release];
  [fs_delegate_ release];
  return NSTerminateNow;
}

@end
