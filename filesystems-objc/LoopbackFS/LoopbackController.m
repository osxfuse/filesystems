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

  NSString* mountPath = @"/Volumes/loop";
  loop_ = [[LoopbackFS alloc] initWithRootPath:rootPath];
  [loop_ setDelegate:self];

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

- (void)didUmount {
  [[NSApplication sharedApplication] terminate:nil];

}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
  [fs_ umount];
  [fs_ release];
  [loop_ release];
  return NSTerminateNow;
}

@end
