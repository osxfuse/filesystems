// ================================================================
// Copyright (C) 2007 Google Inc.
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
//  loop.m
//  LoopbackFS
//
//  Created by ted on 12/30/07.
//
// This is a cmdline version of LoopbackFS. Compile as follows:
//  clang -o loop LoopbackFS.m loop.m -framework OSXFUSE -framework Foundation
//
#import <Foundation/Foundation.h>
#import <OSXFUSE/GMUserFileSystem.h>
#import "LoopbackFS.h"

#define DEFAULT_MOUNT_PATH "/Volumes/loop"

int main(int argc, char* argv[]) {
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];

  NSUserDefaults *args = [NSUserDefaults standardUserDefaults];
  NSString* rootPath = [args stringForKey:@"rootPath"];
  NSString* mountPath = [args stringForKey:@"mountPath"];
  NSString* opts = [args stringForKey:@"o"];
  if (!mountPath || [mountPath isEqualToString:@""]) {
    mountPath = [NSString stringWithUTF8String:DEFAULT_MOUNT_PATH];
  }
  if (!rootPath) {
    printf("\nUsage: %s -rootPath <path> [-mountPath <path>] [-o options]\n", argv[0]);
    printf("  -rootPath: Local directory path to mount, ex: /tmp\n");
    printf("  -mountPath: Mount point to use. [Default='%s']\n",
           DEFAULT_MOUNT_PATH);
    printf("  -o: comma-separted list of options\n");
    printf("Ex: %s -rootPath /tmp -mountPath %s -o local,auto_xattr\n\n", argv[0],
           DEFAULT_MOUNT_PATH);
    return 0;
  }

  LoopbackFS* loop = [[LoopbackFS alloc] initWithRootPath:rootPath];
  GMUserFileSystem* userFS = [[GMUserFileSystem alloc] initWithDelegate:loop
                                                           isThreadSafe:YES];

  NSMutableArray* options = [NSMutableArray array];
  [options addObject:@"debug"];
  if (opts) {
    [options addObjectsFromArray:[opts componentsSeparatedByString:@","]];
  }
  [userFS mountAtPath:mountPath 
          withOptions:options 
     shouldForeground:YES 
      detachNewThread:NO];

  [userFS release];
  [loop release];

  [pool release];
  return 0;
}
