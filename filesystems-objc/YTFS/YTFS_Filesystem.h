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
//  YTFS_Filesystem.h
//  YTFS
//
//  Created by ted on 12/7/08.
//
// Filesystem operations.
//
#import <Foundation/Foundation.h>

// The core set of file system operations. This class will serve as the delegate
// for GMUserFileSystemFilesystem. For more details, see the section on 
// GMUserFileSystemOperations found in the documentation at:
// http://macfuse.googlecode.com/svn/trunk/core/sdk-objc/Documentation/index.html
@interface YTFS_Filesystem : NSObject  {
  NSDictionary* videos_;
}
- (id)initWithVideos:(NSDictionary *)videos;
- (YTVideo *)videoAtPath:(NSString *)path;
@end

