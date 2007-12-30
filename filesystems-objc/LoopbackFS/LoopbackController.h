//
//  LoopbackController.h
//  LoopbackFS
//
//  Created by ted on 12/27/07.
//  Copyright 2007 Google, Inc. All rights reserved.
//

#import <Cocoa/Cocoa.h>

@class UserFileSystem;
@class LoopbackFS;

@interface LoopbackController : NSObject {
  UserFileSystem* fs_;
  LoopbackFS* loop_;
}

@end
