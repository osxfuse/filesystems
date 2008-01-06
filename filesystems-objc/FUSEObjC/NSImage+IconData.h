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
//  NSImage+IconData.h
//  MacFUSE
//
//  Created by ted on 12/28/07.
//
#import <AppKit/NSImage.h>

@class NSData;

@interface NSImage (IconData)

// Creates the data for a .icns file from this NSImage.  You can use a width
// of either 256 or 512 bytes.  Only 10.5+ supports 512x512 icons.
// TODO: Better name for this method?
- (NSData *)icnsDataWithWidth:(int)width;

@end
