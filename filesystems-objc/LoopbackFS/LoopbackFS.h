//
//  LoopbackFS.h
//  LoopbackFS
//
//  Created by ted on 12/12/07.
//  Copyright 2007 Google, Inc. All rights reserved.
//
// This is a simple but complete example filesystem that mounts a local 
// directory. You can modify this to see how the Finder reacts to returning
// specific error codes or not implementing a particular UserFileSystem
// operation.
//
// For example, you can mount "/tmp" in /Volumes/loop. Note: It is 
// probably not a good idea to mount "/" through this filesystem.

#import <Cocoa/Cocoa.h>

@interface LoopbackFS : NSObject  {
  NSString* rootPath_;   // The local file-system path to mount.
  id delegate_;  // We pass on UserFileSystem lifecycle calls to the delegate.
}
- (id)initWithRootPath:(NSString *)rootPath;
- (void)dealloc;

- (void)setDelegate:(id)delegate;

@end
