//
//  AccessibilityFS.m
//  AccessibilityFS
//
//  Created by Dave MacLachlan <dmaclach@> on 12/13/07.
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

#import <sys/types.h>
#import <unistd.h>
#import <CoreServices/CoreServices.h>
#import <AppKit/AppKit.h>
#import <MacFUSE/GMUserFileSystem.h>
#import "AccessibilityFS.h"
#import "NSImage+IconData.h"
#import "NSError+POSIX.h"
#import "GTMAXUIElement.h"

// A mapping of various attributes to selectors for extracting/setting their
// values as strings
typedef struct {
  CFStringRef attributeName;
  NSString *toStringSel;
  NSString *fromStringSel;
} AttributeToString;


// Simple function to make code a little easier to read.
static NSRange GTMNSMakeRangeFromTo(unsigned int start, unsigned int end) {
  return NSMakeRange(start, end - start);
}

@interface AccessibilityFS (AccessibilityFSPrivate)
- (NSArray *)topLevelDirectories;
- (NSData *)iconForPid:(pid_t)pid;
- (void)rootLevelModified;
- (void)axNotification:(NSString*)notification
                  from:(AXObserverRef)observer
                    on:(GTMAXUIElement*)element;
@end

// Keys for element of appDictionary_. appDictionary_ is keyed by 
// GTMAXUIElements, one for each process. It caches some information
// for each one of the elements, especially the date which is needed so the
// icon doesn't flicker in the Finder, and the icon, which is expensive
// to produce.
NSString *const kIconKey = @"iconKey";  // The icon for the process
NSString *const kNameKey = @"nameKey";  // The name for the process
NSString *const kDateKey = @"dateKey";  // The date the process was launched

NSString *const kXAttrPrefix = @"AccessibilityFS.";
NSString *const kActionsXAttr = @"Actions";

// Callback for our axobserver that watches for new elements being created
// in processes so we can update their modified dates appropriately so the
// Finder gets updated appropriately without flickering icons too badly.
static void axObserverCallback(AXObserverRef observer, 
                               AXUIElementRef elementRef, 
                               CFStringRef notification, 
                               void *refcon) {
  GTMAXUIElement *element = [GTMAXUIElement elementWithElement:elementRef];
  [(AccessibilityFS*)refcon axNotification:(NSString*)notification 
                                      from:observer 
                                        on:element];
}

@implementation AccessibilityFS
- (void)addApp:(NSDictionary*)appDict {
  NSString *identifier = [appDict objectForKey:@"NSApplicationBundleIdentifier"];
  if ([identifier isEqualToString:@"com.apple.finder"]) {
    return;
  }
  pid_t pid = [[appDict objectForKey:@"NSApplicationProcessIdentifier"] longValue];
  GTMAXUIElement *element = [GTMAXUIElement elementWithProcessIdentifier:pid];
  if (!element) {
    return;
  }
  int count = [element accessibilityAttributeValueCount:NSAccessibilityChildrenAttribute];
  if (count < 1) {
    return;
  }

  // Watch for elements being created and destroyed at the root level
  AXObserverRef observer;
  AXError error = AXObserverCreate(pid, axObserverCallback, &observer);
  if (error) {
    return;
  }
  error = AXObserverAddNotification(observer, 
                                    [element element], 
                                    kAXCreatedNotification, 
                                    self);
  if (error) {
    return;
  }
  error = AXObserverAddNotification(observer, 
                                    [element element], 
                                    kAXUIElementDestroyedNotification, 
                                    self);
  if (error) {
    return;
  }
  CFRunLoopAddSource ([[NSRunLoop currentRunLoop] getCFRunLoop], 
                      AXObserverGetRunLoopSource(observer), 
                      kCFRunLoopDefaultMode);
  
  // Cache our icon and name for easy access.
  NSMutableDictionary *dict = [NSMutableDictionary dictionary];
  NSString *name = [appDict objectForKey:@"NSApplicationName"];
  NSString *fullName = [NSString stringWithFormat:@"%@ (%d)", name, pid];
  [dict setObject:fullName forKey:kNameKey];
  NSData *icon = [self iconForPid:pid];
  if (icon) {
    [dict setObject:icon forKey:kIconKey];
  }
  [dict setObject:[NSDate date] forKey:kDateKey];
  [appDictionary_ setObject:dict forKey:element];
}

- (id)init {
  if ((self = [super init])) {
    appDictionary_ = [[NSMutableDictionary dictionary] retain];
    NSWorkspace *ws = [NSWorkspace sharedWorkspace];
    NSNotificationCenter *nc = [ws notificationCenter];
    [nc addObserver:self 
           selector:@selector(appLaunched:)
               name:NSWorkspaceDidLaunchApplicationNotification
             object:nil];
    [nc addObserver:self 
           selector:@selector(appTerminated:)
               name:NSWorkspaceDidTerminateApplicationNotification
             object:nil];
    NSEnumerator *appEnum = [[ws launchedApplications] objectEnumerator];
    NSDictionary *dict;
    while ((dict = [appEnum nextObject])) {
      [self addApp:dict];
    }    
    [self rootLevelModified];
  }
  return self;
}

- (void)dealloc {
  NSNotificationCenter *nc = [[NSWorkspace sharedWorkspace] notificationCenter];
  [nc removeObserver:self];
  [appDictionary_ release];
  [rootModifiedDate_ release];
  [super dealloc];
}

- (void)appLaunched:(NSNotification*)notification {
  [self addApp:[notification userInfo]];
  [self rootLevelModified];
}

- (void)appTerminated:(NSNotification*)notification {
  NSDictionary *userInfo = [notification userInfo];
  pid_t pid = [[userInfo objectForKey:@"NSApplicationProcessIdentifier"] longValue];
  GTMAXUIElement *element = [GTMAXUIElement elementWithProcessIdentifier:pid];
  if (element) {
    [appDictionary_ removeObjectForKey:element];
    [self rootLevelModified];
  }
}

- (void)rootLevelModified {
  // An app has been launched or terminated, so update the modification date
  // for "/" so that the Finder updates correctly.
  [rootModifiedDate_ release];
  rootModifiedDate_ = [[NSDate date] retain];
}

- (void)axNotification:(NSString*)notification
                  from:(AXObserverRef)observer
                    on:(GTMAXUIElement*)element {
  // A direct child element of one of the process elements has been created
  // or destroyed. Update the processes modification date so that the
  // Finder will update correctly.
  GTMAXUIElement *processElement = [element processElement];
  NSMutableDictionary *appDict = [appDictionary_ objectForKey:processElement];
  [appDict setObject:[NSDate date] forKey:kDateKey];
}

#pragma mark AccessibilityFS Utilities
      
- (pid_t)pidFromName:(NSString*)name {
  // name example: Finder (xxxxx) where xxxx is the pid.
  pid_t pid = 0;
  int length = [name length];
  int start = length - 1;
  if ([name characterAtIndex:start] == ')') {
    do {
      start -= 1;
    } while ([name characterAtIndex:start] != '(');
    NSString *pidstr = [name substringWithRange:GTMNSMakeRangeFromTo(start + 1, 
                                                                     length - 1)];
    pid = [pidstr intValue];
  }
  return pid;
}

- (GTMAXUIElement*)elementFromElement:(GTMAXUIElement*)parent 
                               named:(NSString*)name {
  if (parent == NULL) {
    pid_t pid = [self pidFromName:name];
    return [GTMAXUIElement elementWithProcessIdentifier:pid];
  }
  NSArray *values = [parent accessibilityAttributeValue:NSAccessibilityChildrenAttribute];
  NSEnumerator *valuesEnum = [(NSArray*)values objectEnumerator];
  GTMAXUIElement *child = nil;
  while ((child = [valuesEnum nextObject])) {
    if ([name isEqualToString:[child description]]) {
      break;
    }
  }
  return child;
}

- (GTMAXUIElement*)elementFromPath:(NSString*)path {
  NSArray *pathComponents = [path pathComponents];
  GTMAXUIElement *element = nil;
  if ([pathComponents count] > 1) {
    NSEnumerator *componentEnum = [pathComponents objectEnumerator];
    [componentEnum nextObject];
    NSString *pathComponent;
    while ((pathComponent = [componentEnum nextObject])) {
      GTMAXUIElement *child = [self elementFromElement:element 
                                                named:pathComponent];
      if (child) {
        element = child;
      } else {
        element = nil;
        break;
      }
    }
  }
  return element;
}

- (NSData *)iconForPid:(pid_t)pid {
  // Badges the icon for pid on a folder.
  const int kIconSize = 256;  // Size of the icon to make
  
  ProcessSerialNumber psn;
  OSStatus error = GetProcessForPID(pid, &psn);
  if (error) return nil;
  FSRef bundleFSRef;
  error = GetProcessBundleLocation(&psn, &bundleFSRef);
  if (error) return nil;
  CFURLRef url = CFURLCreateFromFSRef(NULL, &bundleFSRef);
  if (!url) return nil;
  NSString *path = [(NSURL*)url path];
  CFRelease(url);
  NSWorkspace *ws = [NSWorkspace sharedWorkspace];
  NSImage *appIcon = [ws iconForFile:path];
  NSString *folderType = NSFileTypeForHFSTypeCode(kGenericFolderIcon);
  NSImage *folderIcon = [ws iconForFileType:folderType];
  NSSize largeIconSize = NSMakeSize(kIconSize, kIconSize);
  [appIcon setSize:largeIconSize];
  [folderIcon setSize:largeIconSize];
  [folderIcon lockFocus];
  NSRect sourceRect;
  sourceRect.origin = NSMakePoint(0, 0);
  sourceRect.size = [appIcon size];
  NSRect destRect;
  NSSize size = [folderIcon size];
  // These numbers gotten by experimentation on Leopard to see what looked
  // best for the majority of icons. May need some tweaking for Tiger.
  destRect.origin.x = size.width * 0.25;
  destRect.origin.y = size.height * 0.17;
  destRect.size.width = size.width * 0.5;
  destRect.size.height = size.height * 0.5;
  NSGraphicsContext *context = [NSGraphicsContext currentContext];
  NSImageInterpolation interpolation = [context imageInterpolation];
  [context setImageInterpolation:NSImageInterpolationHigh];
  [appIcon drawInRect:destRect 
             fromRect:sourceRect 
            operation:NSCompositeSourceOver 
             fraction:1.0];
  [context setImageInterpolation:interpolation];
  [folderIcon unlockFocus];
  return [folderIcon icnsDataWithWidth:kIconSize];
}

- (NSArray *)topLevelDirectories {
  NSMutableArray *contents = [NSMutableArray array];
  NSEnumerator *appsEnum = [appDictionary_ keyEnumerator];
  id key;
  while ((key = [appsEnum nextObject])) {
    NSDictionary *dict = [appDictionary_ objectForKey:key];
    [contents addObject:[dict objectForKey:kNameKey]];
  }
  return contents;
}

#pragma mark == Overridden GMUserFileSystem Methods
- (UInt16)finderFlagsAtPath:(NSString *)path {
  UInt16 flags = 0;
  NSArray *pathComponents = [path pathComponents];
  if ([pathComponents count] == 2) {
    flags = kHasCustomIcon;
  }
  return flags;
}

- (NSArray *)contentsOfDirectoryAtPath:(NSString *)path 
                                 error:(NSError **)error {
  NSArray *pathComponents = [path pathComponents];
  
  if ([[pathComponents lastObject] isEqualToString:@"/"]) {
    return [self topLevelDirectories];
  }
  
  GTMAXUIElement *element = [self elementFromPath:path];
  NSArray *values = [element accessibilityAttributeValue:NSAccessibilityChildrenAttribute];
  NSEnumerator *valuesEnum = [values objectEnumerator];
  
  NSMutableArray *contents = [NSMutableArray array];
  if ([pathComponents count] == 2) {
    [contents addObject:@"Icon\r"];
  }
  GTMAXUIElement *child;
  while ((child = [valuesEnum nextObject])) {
    [contents addObject:[child description]];
  }
  return contents;
}

- (NSDictionary *)attributesOfItemAtPath:(NSString *)path 
                                   error:(NSError **)error {
  
  NSString* lastComponent = [path lastPathComponent];
  if ([lastComponent hasPrefix:@"._"] ||
      [lastComponent isEqualToString:@"Icon\r"]) {
    return nil;
  }

  BOOL isDir = YES;
  int mode = 0755;
  BOOL isRoot = [lastComponent isEqualToString:@"/"];
  if (!isRoot) {
    GTMAXUIElement *element = [self elementFromPath:path];
    if (!element) {
      *error = [NSError errorWithPOSIXCode:ENOENT];
      return nil;
    }
    if([element accessibilityAttributeValueCount:NSAccessibilityChildrenAttribute] < 1) {
      mode = 0644;
      isDir = NO;
    }
    
  }

  NSDate *fileDate = isRoot ? rootModifiedDate_ : [NSDate date];
  NSArray *pathComponents = [path pathComponents];
  if ([pathComponents count] == 2) {
    pid_t pid = [self pidFromName:[pathComponents objectAtIndex:1]];
    GTMAXUIElement *key = [GTMAXUIElement elementWithProcessIdentifier:pid];
    if (key) {
      NSDictionary *appDict = [appDictionary_ objectForKey:key];
      if (appDict) {
        NSDate *date = [appDict objectForKey:kDateKey];
        if (date) {
          fileDate = date;
        }
      }
    }
  }
  
  return [NSDictionary dictionaryWithObjectsAndKeys:
          [NSNumber numberWithInt:mode], NSFilePosixPermissions,
          [NSNumber numberWithInt:geteuid()], NSFileOwnerAccountID,
          [NSNumber numberWithInt:getegid()], NSFileGroupOwnerAccountID,
          fileDate, NSFileCreationDate,
          fileDate, NSFileModificationDate,
          isDir ? NSFileTypeDirectory : NSFileTypeRegular, NSFileType,
          nil];
}

- (BOOL)setAttributes:(NSDictionary *)attributes 
         ofItemAtPath:(NSString *)path
                error:(NSError **)error {
  return YES;
}

- (BOOL)openFileAtPath:(NSString *)path 
                  mode:(int)mode
          fileDelegate:(id *)outHandle
                 error:(NSError **)error {
  GTMAXUIElement *element = [self elementFromPath:path];
  *outHandle = [element retain];
  return *outHandle != nil;
}

- (void)releaseFileAtPath:(NSString *)path fileDelegate:(id)handle {
  [handle release];
}  

- (int)writeFileAtPath:(NSString *)path 
          fileDelegate:(id)handle 
                buffer:(const char *)buffer
                  size:(size_t)size 
                offset:(off_t)offset
                 error:(NSError **)error {
  if (!handle) {
    *error = [NSError errorWithPOSIXCode:EINVAL];
    return -1;
  }
  NSString *action = [[[NSString alloc] initWithBytes:buffer 
                                               length:size 
                                             encoding:NSUTF8StringEncoding] autorelease];
  NSCharacterSet *whiteset = [NSCharacterSet whitespaceAndNewlineCharacterSet];
  action = [action stringByTrimmingCharactersInSet:whiteset];
  if (![handle performAccessibilityAction:action]) {
    *error = [NSError errorWithPOSIXCode:EIO];
    size = -1;
  }
  return size;
}

- (BOOL)truncateFileAtPath:(NSString *)path 
                    offset:(off_t)offset 
                     error:(NSError **)error {
  // Supporting truncate allows us to do things like "echo AXPress > blah"
  // where blah is one of our accessibility FS items.
  return YES;
}

- (NSData *)valueOfExtendedAttribute:(NSString *)name 
                        ofItemAtPath:(NSString *)path
                               error:(NSError **)error {
  // We only want to deal with our attributes
  if (![name hasPrefix:kXAttrPrefix]) {
    *error = [NSError errorWithPOSIXCode:ENOTSUP];
    return nil;
  }
  name = [name substringFromIndex:[kXAttrPrefix length]];
  GTMAXUIElement *element = [self elementFromPath:path];
  if (!element) {
    *error = [NSError errorWithPOSIXCode:ENOENT];
    return nil;
  }
  NSString *stringValue = nil;
  if ([name isEqualToString:kActionsXAttr]) {
    NSArray *actions = [element accessibilityActionNames];
    stringValue = [actions componentsJoinedByString:@", "];
    stringValue = [NSString stringWithFormat:@"{ %@ }", stringValue];
  } else {
    stringValue = [element stringValueForAttribute:name];
  }
  if (!stringValue) {
    *error = [NSError errorWithPOSIXCode:ENOATTR];
    return nil;
  }
  
  return [stringValue dataUsingEncoding:NSUTF8StringEncoding];
}

- (BOOL)setExtendedAttribute:(NSString *)name 
                ofItemAtPath:(NSString *)path 
                       value:(NSData *)value
                       flags:(int) flags
                       error:(NSError **)error {
  BOOL success = NO;
  // We only want to deal with our attributes
  if (![name hasPrefix:kXAttrPrefix]) {
    *error = [NSError errorWithPOSIXCode:ENOTSUP];
    return NO;
  }
  name = [name substringFromIndex:[kXAttrPrefix length]];

  GTMAXUIElement *element = [self elementFromPath:path];
  if (!element) {
    *error = [NSError errorWithPOSIXCode:ENOENT];
    return NO;
  }
  NSString *string = [[[NSString alloc] initWithData:value 
                                            encoding:NSUTF8StringEncoding] autorelease];
  success = [element setStringValue:string forAttribute:name];
  if (!success) {
    *error = [NSError errorWithPOSIXCode:EINVAL];
  }
  return success;
}

- (NSArray *)extendedAttributesOfItemAtPath:path error:(NSError **)error {
  GTMAXUIElement *element = [self elementFromPath:path];
  if (!element) {
    *error = [NSError errorWithPOSIXCode:ENOENT];
    return nil;
  }
  NSArray *array = [element accessibilityAttributeNames];
  if (!array) {
     *error = [NSError errorWithPOSIXCode:ENODEV];
  }
  array = [array arrayByAddingObject:kActionsXAttr];
  
  NSMutableArray *mutableArray = [NSMutableArray array];
  NSEnumerator *arrayEnumerator = [array objectEnumerator];
  NSString *attr;
  while ((attr = [arrayEnumerator nextObject])) {
    [mutableArray addObject:[NSString stringWithFormat:@"%@%@", 
                             kXAttrPrefix, attr]];
  }
  return mutableArray;
}

- (NSData *)iconDataAtPath:(NSString *)path {
  NSData *iconData = nil;
  NSArray *pathComponents = [path pathComponents];
  if ([pathComponents count] == 2) {
    NSString *name = [pathComponents objectAtIndex:1];
    pid_t pid = [self pidFromName:name];
    GTMAXUIElement *key = [GTMAXUIElement elementWithProcessIdentifier:pid];
    if (key) {
      NSDictionary *appDict = [appDictionary_ objectForKey:key];
      iconData = [appDict objectForKey:kIconKey];
    }
  }
  return iconData;
}
      
@end

