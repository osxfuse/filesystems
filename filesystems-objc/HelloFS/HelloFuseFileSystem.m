// ================================================================
// Copyright (C) 2006 Google Inc.
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
//  HelloFuseFileSystem.m
//  GoogleHelloFuse
//
//  Created by alcor on 12/15/06.
//
#import "HelloFuseFileSystem.h"
#import <MacFUSE/MacFUSE.h>

static NSString *helloStr = @"Hello World!\n";
static NSString *helloPath = @"/hello.txt";

@implementation HelloFuseFileSystem

- (NSArray *)contentsOfDirectoryAtPath:(NSString *)path error:(NSError **)error {
  return [NSArray arrayWithObject:[helloPath lastPathComponent]];
}

- (NSData *)contentsAtPath:(NSString *)path {
  if ([path isEqualToString:helloPath])
    return [helloStr dataUsingEncoding:NSUTF8StringEncoding];
  return nil;
}

#pragma optional Custom Icon

- (NSDictionary *)finderAttributesAtPath:(NSString *)path 
                                   error:(NSError **)error {
  if ([path isEqualToString:helloPath]) {
    NSNumber* finderFlags = [NSNumber numberWithLong:kHasCustomIcon];
    return [NSDictionary dictionaryWithObject:finderFlags
                                       forKey:kGMUserFileSystemFinderFlagsKey];
  }
  return nil;
}

- (NSDictionary *)resourceAttributesAtPath:(NSString *)path
                                     error:(NSError **)error {
  if ([path isEqualToString:helloPath]) {
    NSString *file = [[NSBundle mainBundle] pathForResource:@"hellodoc" ofType:@"icns"];
    return [NSDictionary dictionaryWithObject:[NSData dataWithContentsOfFile:file]
                                       forKey:kGMUserFileSystemCustomIconDataKey];
  }
  return nil;
}

@end
