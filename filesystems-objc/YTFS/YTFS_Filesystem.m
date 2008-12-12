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
//  YTFS_Filesystem.m
//  YTFS
//
//  Created by ted on 12/7/08.
//
#import "YTFS_Filesystem.h"
#import "YTVideo.h"
#import "NSImage+IconData.h"
#import <MacFUSE/MacFUSE.h>

// Category on NSError to  simplify creating an NSError based on posix errno.
@interface NSError (POSIX)
+ (NSError *)errorWithPOSIXCode:(int)code;
@end
@implementation NSError (POSIX)
+ (NSError *)errorWithPOSIXCode:(int) code {
  return [NSError errorWithDomain:NSPOSIXErrorDomain code:code userInfo:nil];
}
@end

@implementation YTFS_Filesystem

#pragma mark Directory Contents

- (NSArray *)contentsOfDirectoryAtPath:(NSString *)path error:(NSError **)error {
  if ([path isEqualToString:@"/"]) {
    return [videos_ allKeys];
  }
  *error = [NSError errorWithPOSIXCode:ENOENT];
  return nil;
}

#pragma mark Getting Attributes

- (NSDictionary *)attributesOfItemAtPath:(NSString *)path
                                userData:(id)userData
                                   error:(NSError **)error {
  if ([self videoAtPath:path]) {
    return [NSDictionary dictionary];
  }
  return nil;
}

#pragma mark File Contents

- (NSData *)contentsAtPath:(NSString *)path {
  YTVideo* video = [self videoAtPath:path];
  if (video) {
    return [video xmlData];
  }
  return nil;
}

#pragma mark FinderInfo and ResourceFork (Optional)

- (NSDictionary *)finderAttributesAtPath:(NSString *)path 
                                   error:(NSError **)error {
  NSDictionary* attribs = nil;
  if ([self videoAtPath:path]) {
    NSNumber* finderFlags = [NSNumber numberWithLong:kHasCustomIcon];
    attribs = [NSDictionary dictionaryWithObject:finderFlags
                                          forKey:kGMUserFileSystemFinderFlagsKey];
  }
  return attribs;
}

- (NSDictionary *)resourceAttributesAtPath:(NSString *)path
                                     error:(NSError **)error {
  NSMutableDictionary* attribs = nil;
  YTVideo* video = [self videoAtPath:path];
  if (video) {
    attribs = [NSMutableDictionary dictionary];
    NSURL* url = [video playerURL];
    if (url) {
      [attribs setObject:url forKey:kGMUserFileSystemWeblocURLKey];
    }
    url = [video thumbnailURL];
    if (url) {
      NSImage* image = [[[NSImage alloc] initWithContentsOfURL:url] autorelease];
      NSData* icnsData = [image icnsDataWithWidth:256];
      [attribs setObject:icnsData forKey:kGMUserFileSystemCustomIconDataKey];
    }
  }
  return attribs;  
}

#pragma mark Init and Dealloc

- (id)initWithVideos:(NSDictionary *)videos {
  if ((self = [super init])) {
    videos_ = [videos retain];
  }
  return self;
}
- (void)dealloc {
  [videos_ release];
  [super dealloc];
}

- (YTVideo *)videoAtPath:(NSString *)path {
  NSArray* components = [path pathComponents];
  if ([components count] != 2) {
    return nil;
  }
  YTVideo* video = [videos_ objectForKey:[components objectAtIndex:1]];
  return video;
}

@end
