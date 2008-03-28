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
//  YTController.m
//
//  Created by ted on 1/6/08.
//
#import "YTController.h"
#import "YTFS.h"
#import <MacFUSE/GMUserFileSystem.h>

@implementation YTController

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
  // Pump up our url cache.
  NSURLCache* cache = [NSURLCache sharedURLCache];
  [cache setDiskCapacity:(1024 * 1024 * 500)];
  [cache setMemoryCapacity:(1024 * 1024 * 40)];

#pragma mark INSERT CODE HERE

  NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
  [center addObserver:self selector:@selector(didMount:)
                 name:kGMUserFileSystemDidMount object:nil];
  [center addObserver:self selector:@selector(didUnmount:)
                 name:kGMUserFileSystemDidUnmount object:nil];

  NSMutableArray* options = [NSMutableArray array];
  [options addObject:@"ro"];
  [options addObject:@"volname=YTFS"];
  [options addObject:
   [NSString stringWithFormat:@"volicon=%@", 
    [[NSBundle mainBundle] pathForResource:@"ytfs" ofType:@"icns"]]];    
  
  YTFS* ytfs = [[YTFS alloc] initWithVideos:[self fetchVideos]];
  fs_ = [[GMUserFileSystem alloc] initWithDelegate:ytfs isThreadSafe:YES];
  [fs_ mountAtPath:@"/Volumes/ytfs" withOptions:options];
  
#pragma mark -
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
#pragma mark INSERT MORE CODE HERE

  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [fs_ unmount];
  id delegate = [fs_ delegate];
  [fs_ release];
  [delegate release];
  
#pragma mark -
  return NSTerminateNow;
}

@end

@implementation YTController (Notifications) 

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

@end

@implementation YTController (YTUtil)

// See: http://code.google.com/apis/youtube/overview.html
static NSString* const kFeedURL = 
  @"http://gdata.youtube.com/feeds/api/standardfeeds/top_rated?max-results=11";
- (NSDictionary *)fetchVideos {
  NSURL* url = [NSURL URLWithString:kFeedURL];
  NSURLRequest* request = [NSURLRequest requestWithURL:url];
  NSURLResponse* response = nil;
  NSError* error = nil;
  NSData* data = [NSURLConnection sendSynchronousRequest:request 
                                       returningResponse:&response
                                                   error:&error];
  if (data == nil) {
    NSRunAlertPanel(@"YTFS Error", @"Unable to download feed.",
                    @"Quit", nil, nil);
    exit(1);
  }
  NSXMLDocument* doc = [[NSXMLDocument alloc] initWithData:data 
                                                   options:0 
                                                     error:&error];  
  NSArray* xmlEntries = [[doc rootElement] nodesForXPath:@"./entry" 
                                                   error:&error];
  if ([xmlEntries count] == 0) {
    NSRunAlertPanel(@"YTFS Error", @"Feed has zero entries.  Bummer.",
                    @"Quit", nil, nil);
    exit(1);
  }

  NSMutableDictionary* videos = [NSMutableDictionary dictionary];
  for (int i = 0; i < [xmlEntries count]; ++i) {
    NSXMLNode* node = [xmlEntries objectAtIndex:i];
    NSArray* nodes = [node nodesForXPath:@"./title" error:&error];
    NSString* title = [[nodes objectAtIndex:0] stringValue];
    NSMutableString* name = [NSMutableString stringWithFormat:@"%@.webloc", title];
    [name replaceOccurrencesOfString:@"/"
                          withString:@":"
                             options:NSLiteralSearch
                               range:NSMakeRange(0, [name length])];
    [videos setObject:node forKey:name];
  }
  return videos;
}

@end
