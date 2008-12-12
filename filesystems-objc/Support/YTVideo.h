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
//  YTVideo.h
//  YTFS
//
//  Created by ted on 12/11/08.
//
#import <Foundation/Foundation.h>

// NOTE: This is a very simple class that can fetch an xml feed of videos from
// YouTube and parse it into YTVideo objects. This is meant to be light 
// and simple for learning purposes; a real project should use the full-featured
// GData Objective-C API: http://code.google.com/p/gdata-objectivec-client/
@interface YTVideo : NSObject {
  NSXMLNode* xmlNode_;
}

// Returns a dictionary keyed by filename-safe video name of the top rated
// videos on YouTube. The vales are YTVideo*.
+ (NSDictionary *)fetchTopRatedVideos;

+ (id)videoWithXMLNode:(NSXMLNode *)node;
- (id)initWithXMLNode:(NSXMLNode *)node;

// Returns the URL to the thumbnail image for the video.
- (NSURL *)thumbnailURL;

// Returns the URL that will play the video in a web browser.
- (NSURL *)playerURL;

// Returns NSData for the xml string for this video node in pretty-print format.
- (NSData *)xmlData;

@end
