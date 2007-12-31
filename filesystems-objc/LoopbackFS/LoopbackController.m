//
//  LoopbackController.m
//  LoopbackFS
//
//  Created by ted on 12/27/07.
//  Copyright 2007 Google, Inc. All rights reserved.
//

#import "LoopbackController.h"
#import "LoopbackFS.h"
#import <MacFUSE/UserFileSystem.h>

@implementation LoopbackController

- (void)didMount:(NSNotification *)notification {
  NSLog(@"Got didMount notification.");

  NSDictionary* userInfo = [notification userInfo];
  NSString* mountPath = [userInfo objectForKey:@"mountPath"];
  NSString* parentPath = [mountPath stringByDeletingLastPathComponent];
  [[NSWorkspace sharedWorkspace] selectFile:mountPath
                   inFileViewerRootedAtPath:parentPath];
}

- (void)didUnmount:(NSNotification*)notification {
  NSLog(@"Got didUnmount notification.");
  
  [[NSApplication sharedApplication] terminate:nil];
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
  NSOpenPanel* panel = [NSOpenPanel openPanel];
  [panel setCanChooseFiles:NO];
  [panel setCanChooseDirectories:YES];
  [panel setAllowsMultipleSelection:NO];
  int ret = [panel runModalForDirectory:@"/tmp" file:nil types:nil];
  if ( ret == NSCancelButton ) {
    exit(0);
  }
  NSArray* paths = [panel filenames];
  if ( [paths count] != 1 ) {
    exit(0);
  }
  NSString* rootPath = [paths objectAtIndex:0];

  NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
  [center addObserver:self selector:@selector(didMount:)
                 name:kUserFileSystemDidMount object:nil];
  [center addObserver:self selector:@selector(didUnmount:)
                 name:kUserFileSystemDidUnmount object:nil];
  
  NSString* mountPath = @"/Volumes/loop";
  loop_ = [[LoopbackFS alloc] initWithRootPath:rootPath];

  fs_ = [[UserFileSystem alloc] initWithDelegate:loop_ isThreadSafe:NO];
  
  NSMutableArray* options = [NSMutableArray array];
  NSString* volArg = 
  [NSString stringWithFormat:@"volicon=%@", 
   [[NSBundle mainBundle] pathForResource:@"LoopbackFS" ofType:@"icns"]];
  [options addObject:volArg];
  [options addObject:@"debug"];
  
  [fs_ mountAtPath:mountPath 
       withOptions:options];
}


- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
  [fs_ unmount];
  [fs_ release];
  [loop_ release];
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  return NSTerminateNow;
}

@end
