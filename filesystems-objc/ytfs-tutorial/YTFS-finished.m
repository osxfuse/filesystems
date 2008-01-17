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
//  YTFS.m
//
//  Created by ted on 12/19/07.
//
#import "YTFS.h"
#import "NSImage+IconData.h"

@implementation YTFS

static NSString* const kPlayerURLQuery = @"./media:group/media:player/@url";
static NSString* const kThumbURLQuery = @"./media:group/media:thumbnail/@url"; 

#pragma mark GMUserFileSystem Delegate Operations

#pragma mark INSERT CODE HERE

- (NSArray *)contentsOfDirectoryAtPath:(NSString *)path 
                                 error:(NSError **)error {
  return [videos_ allKeys];
}

- (NSURL *)URLOfWeblocAtPath:(NSString *)path {
  return [self URLFromQuery:kPlayerURLQuery atPath:path];
}

- (NSData *)iconDataAtPath:(NSString *)path {
  NSURL* url = [self URLFromQuery:kThumbURLQuery atPath:path];
  if (url) {
    NSImage* image = [[[NSImage alloc] initWithContentsOfURL:url] autorelease];
    return [image icnsDataWithWidth:256];
  }
  return nil;
}

- (NSDictionary *)attributesOfItemAtPath:(NSString *)path 
                                   error:(NSError **)error {
  if ([self nodeAtPath:path]) {
    return [NSDictionary dictionary];
  }
  return nil;
}

// Optional: Just for fun we return the xml string as our file contents.
- (NSData *)contentsAtPath:(NSString *)path {
  NSXMLNode* node = [self nodeAtPath:path];
  if (node) {
    NSString* xml = [node XMLStringWithOptions:NSXMLNodePrettyPrint];
    return [xml dataUsingEncoding:NSUTF8StringEncoding];
  }
  return nil;
}

// Optional: Leads to one less call of iconDataAtPath.
- (UInt16)finderFlagsAtPath:(NSString *)path {
  return ([self nodeAtPath:path] ? kHasCustomIcon : 0);
}

#pragma mark -

#pragma mark Init and Dealloc

- (id)init { return [self initWithVideos:nil]; }
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

@end

@implementation YTFS (YTUtil)

- (NSXMLNode *)nodeAtPath:(NSString *)path {
  NSArray* components = [path pathComponents];
  if ([components count] != 2) {
    return nil;
  }
  NSXMLNode* node = [videos_ objectForKey:[components objectAtIndex:1]];
  return node;
}

- (NSURL *)URLFromQuery:(NSString *)query atPath:(NSString *)path {
  NSXMLNode* node = [self nodeAtPath:path];
  if (node != nil) {
    NSError* error = nil;
    NSArray* nodes = [node nodesForXPath:query error:&error];
    if (nodes != nil && [nodes count] > 0) {
      NSString* urlStr = [[nodes lastObject] stringValue];
      if (urlStr != nil) {
        return [NSURL URLWithString:urlStr];
      }
    }
  }
  return nil;
}

@end
