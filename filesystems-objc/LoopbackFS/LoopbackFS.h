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
//  LoopbackFS.h
//  LoopbackFS
//
//  Created by ted on 12/12/07.
//
// This is a simple but complete example filesystem that mounts a local 
// directory. You can modify this to see how the Finder reacts to returning
// specific error codes or not implementing a particular GMUserFileSystem
// operation.
//
// For example, you can mount "/tmp" in /Volumes/loop. Note: It is 
// probably not a good idea to mount "/" through this filesystem.

#import <Cocoa/Cocoa.h>

@interface LoopbackFS : NSObject  {
  NSString* rootPath_;   // The local file-system path to mount.
}
- (id)initWithRootPath:(NSString *)rootPath;
- (void)dealloc;

@end
