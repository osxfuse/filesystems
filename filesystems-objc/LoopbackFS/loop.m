//
//  loop.m
//  LoopbackFS
//
//  Created by ted on 12/30/07.
//  Copyright 2007 Google. All rights reserved.
//
// This is a cmdline version of LoopbackFS. Compile as follows:
//  gcc -o loop LoopbackFS.m loop.m -framework MacFUSE -framework Foundation
//
#import <Foundation/Foundation.h>
#import <MacFUSE/UserFileSystem.h>
#import "LoopbackFS.h"

#define DEFAULT_MOUNT_PATH "/Volumes/loop"

int main(int argc, char* argv[]) {
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];

  NSUserDefaults *args = [NSUserDefaults standardUserDefaults];
  NSString* rootPath = [args stringForKey:@"rootPath"];
  NSString* mountPath = [args stringForKey:@"mountPath"];
  if (!mountPath || [mountPath isEqualToString:@""]) {
    mountPath = [NSString stringWithUTF8String:DEFAULT_MOUNT_PATH];
  }
  if (!rootPath) {
    printf("\nUsage: %s -rootPath <path> [-mountPath <path>]\n", argv[0]);
    printf("  -rootPath: Local directory path to mount, ex: /tmp\n");
    printf("  -mountPath: Mount point to use. [Default='%s']\n",
           DEFAULT_MOUNT_PATH);
    printf("Ex: %s -rootPath /tmp -mountPath %s\n\n", DEFAULT_MOUNT_PATH);
    return 0;
  }

  LoopbackFS* loop = [[LoopbackFS alloc] initWithRootPath:rootPath];
  UserFileSystem* userFS = [[UserFileSystem alloc] initWithDelegate:loop 
                                                       isThreadSafe:YES];

  NSMutableArray* options = [NSMutableArray array];
  [options addObject:@"debug"];
  [userFS mountAtPath:mountPath 
          withOptions:options 
     shouldForeground:YES 
      detachNewThread:NO];

  [userFS release];
  [loop release];

  [pool release];
  return 0;
}
