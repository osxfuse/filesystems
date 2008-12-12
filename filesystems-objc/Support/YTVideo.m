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
//  YTVideo.m
//  YTFS
//
//  Created by ted on 12/11/08.
//
#import "YTVideo.h"

// NOTE: This is a very simple class that can fetch an xml feed of videos from
// YouTube and parse it into YTVideo objects. This is meant to be light 
// and simple for learning purposes; a real project should use the full-featured
// GData Objective-C API: http://code.google.com/p/gdata-objectivec-client/

static NSString* const kPlayerURLQuery = @"./media:group/media:player/@url";
static NSString* const kThumbURLQuery = @"./media:group/media:thumbnail/@url"; 

@implementation YTVideo

+ (id)videoWithXMLNode:(NSXMLNode *)node {
  return [[[YTVideo alloc] initWithXMLNode:node] autorelease];
}

- (id)initWithXMLNode:(NSXMLNode *)node {
  if ((self = [super init])) {
    xmlNode_ = [node retain];
  }
  return self;
}
- (void)dealloc {
  [xmlNode_ release];
  [super dealloc];
}

- (NSURL *)URLFromQuery:(NSString *)query {
  NSError* error;
  NSArray* nodes = [xmlNode_ nodesForXPath:query error:&error];
  if (nodes != nil && [nodes count] > 0) {
    NSString* urlStr = [[nodes lastObject] stringValue];
    if (urlStr != nil) {
      return [NSURL URLWithString:urlStr];
    }
  }
  return nil;
}

- (NSURL *)thumbnailURL {
  return [self URLFromQuery:kThumbURLQuery];
}
- (NSURL *)playerURL {
  return [self URLFromQuery:kPlayerURLQuery];
}
- (NSData *)xmlData {
  NSString* xml = [xmlNode_ XMLStringWithOptions:NSXMLNodePrettyPrint];
  return [xml dataUsingEncoding:NSUTF8StringEncoding];
}

// See: http://code.google.com/apis/youtube/overview.html
// NOTE: Ideally the caller of this method would handle failure and handle dialog
// boxes, exiting from the app, etc. For the sake of the YTFS tutorial, this is
// done here to minimize changes needed to the YTFS_Controller class.
static NSString* const kFeedURL = 
@"http://gdata.youtube.com/feeds/api/standardfeeds/top_rated?max-results=11";
+ (NSDictionary *)fetchTopRatedVideos {
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
    YTVideo* video = [YTVideo videoWithXMLNode:node];
    [videos setObject:video forKey:name];
  }
  return videos;
}

@end
