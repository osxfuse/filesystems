//
//  NSWorkspace+Icon.h
//
//  Created by Dave MacLachlan on 12/18/07
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


#import <Cocoa/Cocoa.h>

/// Category for getting better icons than NSWorkspace provides by default
@interface NSWorkspace (GTMWorkspaceIconAddition)
// The standard NSWorkspace version of iconForFile only returns 32x32 icons.
// We want bigger more beautiful icons, so this will give you an autoreleased
// NSImage that contains reps of all of the icons for the path that are in the
// icns associated with the file at |fullPath|.
//
// Arguments:
//  fullPath - path to file you want icon for
//
// Returns:
//  NSImage with all reps that can be extracted. nil on failure.
- (NSImage *)fullIconForFile:(NSString *)fullPath;

// The standard NSWorkspace version of iconForFile only returns 32x32 icons.
// We want bigger more beautiful icons, so this will give you an autoreleased
// NSImage that contains reps of all of the icons for |iconType|.
// |iconType| is one of the types in Icons.h eg kGenericFolderIcon
//
// Arguments:
//  fullPath - path to file you want icon for
//
// Returns:
//  NSImage with all reps that can be extracted. nil on failure.

- (NSImage *)fullIconForType:(OSType)iconType;
@end

