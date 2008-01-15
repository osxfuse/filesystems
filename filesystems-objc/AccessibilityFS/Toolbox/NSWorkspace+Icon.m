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

#import <Carbon/Carbon.h>
#import "NSWorkspace+Icon.h"

@implementation NSWorkspace (GTMWorkspaceIconAddition)
- (NSImage *)fullIconFromIconRef:(IconRef)iconRef {
  NSImage *image = nil;
  IconFamilyHandle iconFamily = NULL;
  OSStatus err = IconRefToIconFamily(iconRef, kSelectorAllAvailableData, &iconFamily);
  if (err == noErr && iconFamily) {
    NSData *data = [NSData dataWithBytes:*iconFamily 
                                  length:GetHandleSize((Handle)iconFamily)];
    image = [[[NSImage alloc] initWithData:data] autorelease];
    DisposeHandle((Handle)iconFamily);
  }
  return image;
}

- (NSImage *)fullIconForFile:(NSString *)fullPath {
  const UInt8 *filePath = (const UInt8 *)[fullPath fileSystemRepresentation];
  if (!filePath) {
    return nil;
  }

  FSRef ref;
  Boolean isDir;
  OSStatus err = FSPathMakeRef(filePath,
                               &ref,
                               &isDir);
  if (err != noErr) {
    return nil;
  }
  
  SInt16 label;
  IconRef	iconRef = nil;
  err = GetIconRefFromFileInfo(&ref, 0, NULL, kFSCatInfoNone, NULL,
                               kIconServicesNormalUsageFlag, &iconRef, &label);
  if (err != noErr) {
    return nil;
  }
  
  NSImage *image = [self fullIconFromIconRef:iconRef];
  ReleaseIconRef(iconRef);
  return image;
}

- (NSImage *)fullIconForType:(OSType)iconType {
  IconRef iconRef = NULL;
  NSImage *image = nil;
  OSStatus error = GetIconRef(kOnSystemDisk, kSystemIconsCreator, 
                              iconType, &iconRef);
  if (!error && iconRef) {
    image = [self fullIconFromIconRef:iconRef];
    ReleaseIconRef(iconRef);
  }
  return image;
}

@end
