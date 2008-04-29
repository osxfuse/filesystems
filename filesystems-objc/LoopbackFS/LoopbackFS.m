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
//  LoopbackFS.m
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

#import <sys/xattr.h>
#import <sys/stat.h>
#import "LoopbackFS.h"
#import <MacFUSE/GMUserFileSystem.h>
#import "NSError+POSIX.h"

@implementation LoopbackFS

#if 1
#define LOG_OP(fmt, ...) NSLog(fmt, ## __VA_ARGS__)
#else
#define LOG_OP(fmt, ...) do {} while(0)
#endif

- (id)initWithRootPath:(NSString *)rootPath {
  if ((self = [super init])) {
    rootPath_ = [rootPath retain];
  }
  return self;
}

- (void) dealloc {
  [rootPath_ release];
  [super dealloc];
}

#pragma mark Moving an Item

- (BOOL)moveItemAtPath:(NSString *)source 
                toPath:(NSString *)destination
                 error:(NSError **)error {
  LOG_OP(@"[0x%x] moveItemAtPath: %@ -> %@", [NSThread currentThread], 
         source, destination);

  // We use rename directly here since NSFileManager can sometimes fail to 
  // rename and return non-posix error codes.
  NSString* p_src = [rootPath_ stringByAppendingString:source];
  NSString* p_dst = [rootPath_ stringByAppendingString:destination];
  int ret = rename([p_src UTF8String], [p_dst UTF8String]);
  if ( ret < 0 ) {
    *error = [NSError errorWithPOSIXCode:errno];
  }
  return YES;
}

#pragma mark Removing an Item

- (BOOL)removeItemAtPath:(NSString *)path error:(NSError **)error {
  LOG_OP(@"[0x%x] removeItemAtPath: %@", [NSThread currentThread], path);

  NSString* p = [rootPath_ stringByAppendingString:path];
  BOOL isDirectory = NO;
  BOOL exists = 
    [[NSFileManager defaultManager] fileExistsAtPath:p isDirectory:&isDirectory];
  if (exists && isDirectory) {
    // We need to special-case directories here since NSFileManager will happily
    // do a recursive remove :-(
    int ret = rmdir([p UTF8String]);
    if (ret < 0) {
      *error = [NSError errorWithPOSIXCode:errno];
      return NO;
    }
    return YES;
  }
  return [[NSFileManager defaultManager] removeItemAtPath:p error:error];
}

#pragma mark Creating an Item

- (BOOL)createDirectoryAtPath:(NSString *)path 
                   attributes:(NSDictionary *)attributes
                        error:(NSError **)error {
  LOG_OP(@"[0x%x] createDirectoryAtPath: %@", [NSThread currentThread], path);

  NSString* p = [rootPath_ stringByAppendingString:path];
  return [[NSFileManager defaultManager] createDirectoryAtPath:p 
                                   withIntermediateDirectories:NO
                                                    attributes:attributes
                                                        error:error];
}

- (BOOL)createFileAtPath:(NSString *)path 
              attributes:(NSDictionary *)attributes
            fileDelegate:(id *)fileDelegate
                   error:(NSError **)error {
  LOG_OP(@"[0x%x] createFileAtPath: %@", [NSThread currentThread], path); 

  NSString* p = [rootPath_ stringByAppendingString:path];
  mode_t mode = [[attributes objectForKey:NSFilePosixPermissions] longValue];  
  int fd = creat([p UTF8String], mode);
  if ( fd < 0 ) {
    *error = [NSError errorWithPOSIXCode:errno];
    return NO;
  }
  *fileDelegate = [NSNumber numberWithLong:fd];
  return YES;
}

#pragma mark Linking an Item

- (BOOL)linkItemAtPath:(NSString *)path
                toPath:(NSString *)otherPath
                 error:(NSError **)error {
  LOG_OP(@"[0x%x] linkItemAtPath: %@ <- %@", 
         [NSThread currentThread], path, otherPath); 

  NSString* p_path = [rootPath_ stringByAppendingString:path];
  NSString* p_otherPath = [rootPath_ stringByAppendingString:otherPath];

  // We use link rather than the NSFileManager equivalent because it will copy
  // the file rather than hard link if part of the root path is a symlink.
  int rc = link([p_path UTF8String], [p_otherPath UTF8String]);
  if ( rc <  0 ) {
    *error = [NSError errorWithPOSIXCode:errno];
    return NO;
  }
  return YES;
}

#pragma mark Symbolic Links

- (BOOL)createSymbolicLinkAtPath:(NSString *)path 
             withDestinationPath:(NSString *)otherPath
                           error:(NSError **)error {
  LOG_OP(@"[0x%x] createSymbolicLinkAtPath: %@", [NSThread currentThread], path); 
  
  NSString* p_src = [rootPath_ stringByAppendingString:path];
  NSString* p_dst = [rootPath_ stringByAppendingString:otherPath];
  return [[NSFileManager defaultManager] createSymbolicLinkAtPath:p_src
                                              withDestinationPath:p_dst
                                                            error:error];  
}

- (NSString *)destinationOfSymbolicLinkAtPath:(NSString *)path
                                        error:(NSError **)error {
  LOG_OP(@"[0x%x] desinationOfSymbolicLinkAtPath: %@", [NSThread currentThread], path); 
  
  NSString* p = [rootPath_ stringByAppendingString:path];
  return [[NSFileManager defaultManager] destinationOfSymbolicLinkAtPath:p
                                                                   error:error];
}

#pragma mark File Contents

- (BOOL)openFileAtPath:(NSString *)path 
                  mode:(int)mode
          fileDelegate:(id *)fileDelegate
                 error:(NSError **)error {
  LOG_OP(@"[0x%x] openFileAtPath: %@", [NSThread currentThread], path); 

  NSString* p = [rootPath_ stringByAppendingString:path];
  int fd = open([p UTF8String], mode);
  if ( fd < 0 ) {
    *error = [NSError errorWithPOSIXCode:errno];
    return NO;
  }
  *fileDelegate = [NSNumber numberWithLong:fd];
  return YES;
}

- (void)releaseFileAtPath:(NSString *)path fileDelegate:(id)fileDelegate {
  LOG_OP(@"[0x%x] releaseFileAtPath: %@", [NSThread currentThread], path);

  NSNumber* num = (NSNumber *)fileDelegate;
  int fd = [num longValue];
  close(fd);
}

- (int)readFileAtPath:(NSString *)path 
         fileDelegate:(id)fileDelegate
               buffer:(char *)buffer 
                 size:(size_t)size 
               offset:(off_t)offset
                error:(NSError **)error {
  LOG_OP(@"[0x%x] readFileAtPath: %@, offset=%lld, size=%d", 
         [NSThread currentThread], path, offset, size); 

  NSNumber* num = (NSNumber *)fileDelegate;
  int fd = [num longValue];
  int ret = pread(fd, buffer, size, offset);
  if ( ret < 0 ) {
    *error = [NSError errorWithPOSIXCode:errno];
    return -1;
  }
  return ret;
}

- (int)writeFileAtPath:(NSString *)path 
          fileDelegate:(id)fileDelegate 
                buffer:(const char *)buffer
                  size:(size_t)size 
                offset:(off_t)offset
                 error:(NSError **)error {
  LOG_OP(@"[0x%x] writeFileAtPath: %@, offset=%lld, size=%d", 
         [NSThread currentThread], path, offset, size);

  NSNumber* num = (NSNumber *)fileDelegate;
  int fd = [num longValue];
  int ret = pwrite(fd, buffer, size, offset);
  if ( ret < 0 ) {
    *error = [NSError errorWithPOSIXCode:errno];
    return -1;
  }
  return ret;
}

- (BOOL)truncateFileAtPath:(NSString *)path 
                    offset:(off_t)offset 
                     error:(NSError **)error {
  LOG_OP(@"[0x%x] truncateFileAtPath:%@, offset=%d", [NSThread currentThread],
         path, offset);

  NSString* p = [rootPath_ stringByAppendingString:path];
  int ret = truncate([p UTF8String], offset);
  if ( ret < 0 ) {
    *error = [NSError errorWithPOSIXCode:errno];
    return NO;    
  }
  return YES;
}

#pragma mark Directory Contents

- (NSArray *)contentsOfDirectoryAtPath:(NSString *)path error:(NSError **)error {
  LOG_OP(@"[0x%x] directory contents: %@", [NSThread currentThread], path);

  NSString* p = [rootPath_ stringByAppendingString:path];
  return [[NSFileManager defaultManager] contentsOfDirectoryAtPath:p error:error];
}

#pragma mark Getting and Setting Attributes

- (NSDictionary *)attributesOfItemAtPath:(NSString *)path 
                                   error:(NSError **)error {
  LOG_OP(@"[0x%x] attributesOfItemAtAPath: %@", [NSThread currentThread], path);

  NSString* p = [rootPath_ stringByAppendingString:path];
  NSDictionary* attribs = 
    [[NSFileManager defaultManager] attributesOfItemAtPath:p error:error];
  return attribs;
}

- (NSDictionary *)attributesOfFileSystemForPath:(NSString *)path
                                          error:(NSError **)error {
  LOG_OP(@"[0x%x] attributesOfFileSystemForPath: %@", 
         [NSThread currentThread], path);

  NSString* p = [rootPath_ stringByAppendingString:path];
  return [[NSFileManager defaultManager] attributesOfFileSystemForPath:p error:error];
}

- (BOOL)setAttributes:(NSDictionary *)attributes 
         ofItemAtPath:(NSString *)path
                error:(NSError **)error {
  LOG_OP(@"[0x%x] setAttributes:%@ ofItemAtPath: %@", 
         [NSThread currentThread], attributes, path);
  NSString* p = [rootPath_ stringByAppendingString:path];
  
  NSNumber* flags = [attributes objectForKey:kGMUserFileSystemFileFlagsKey];
  if (flags != nil) {
    int rc = chflags([p UTF8String], [flags intValue]);
    if (rc < 0) {
      *error = [NSError errorWithPOSIXCode:errno];
      return NO;
    }
  }
  return [[NSFileManager defaultManager] setAttributes:attributes
                                          ofItemAtPath:p
                                                 error:error];
}

#pragma mark Extended Attributes

- (NSArray *)extendedAttributesOfItemAtPath:(NSString *)path error:(NSError **)error {
  LOG_OP(@"[0x%x] extended Attributes: %@", [NSThread currentThread], path);
  
  NSString* p = [rootPath_ stringByAppendingString:path];
  
  ssize_t size = listxattr([p UTF8String], nil, 0, 0);
  if ( size < 0 ) {
    *error = [NSError errorWithPOSIXCode:errno];
    return nil;
  }
  NSMutableData* data = [NSMutableData dataWithLength:size];
  size = listxattr([p UTF8String], [data mutableBytes], [data length], 0);
  if ( size < 0 ) {
    *error = [NSError errorWithPOSIXCode:errno];
    return nil;
  }
  NSMutableArray* contents = [NSMutableArray array];
  char* ptr = (char *)[data bytes];
  while ( ptr < ((char *)[data bytes] + size) ) {
    NSString* s = [NSString stringWithUTF8String:ptr];
    [contents addObject:s];
    ptr += ([s length] + 1);
  }
  return contents;
}

- (NSData *)valueOfExtendedAttribute:(NSString *)name 
                        ofItemAtPath:(NSString *)path
                               error:(NSError **)error {
  LOG_OP(@"[0x%x] value of extended attribute: %@ forPath:%@", 
         [NSThread currentThread], name, path);
  
  NSString* p = [rootPath_ stringByAppendingString:path];

  ssize_t size = getxattr([p UTF8String], [name UTF8String], nil, 0,
                         0, 0);
  if ( size < 0 ) {
    *error = [NSError errorWithPOSIXCode:errno];
    return nil;
  }
  NSMutableData* data = [NSMutableData dataWithLength:size];
  size = getxattr([p UTF8String], [name UTF8String], 
                  [data mutableBytes], [data length],
                  0, 0);
  if ( size < 0 ) {
    *error = [NSError errorWithPOSIXCode:errno];
    return nil;
  }  
  return data;
}

- (BOOL)setExtendedAttribute:(NSString *)name 
                ofItemAtPath:(NSString *)path 
                       value:(NSData *)value
                       flags:(int)flags
                       error:(NSError **)error {
  LOG_OP(@"[0x%x] set extended attribute: %@ forPath:%@", 
         [NSThread currentThread], name, path);

  NSString* p = [rootPath_ stringByAppendingString:path];
  int ret = setxattr([p UTF8String], [name UTF8String], 
                     [value bytes], [value length], 
                     0, 0);
  if ( ret < 0 ) {
    *error = [NSError errorWithPOSIXCode:errno];
    return NO;
  }
  return YES;
}

- (BOOL)removeExtendedAttribute:(NSString *)name
                   ofItemAtPath:(NSString *)path
                          error:(NSError **)error {
  LOG_OP(@"[0x%x] set extended attribute: %@ forPath:%@", 
         [NSThread currentThread], name, path);
  
  NSString* p = [rootPath_ stringByAppendingString:path];
  int ret = removexattr([p UTF8String], [name UTF8String], 0);
  if ( ret < 0 ) {
    *error = [NSError errorWithPOSIXCode:errno];
    return NO;
  }
  return YES;
}

@end
