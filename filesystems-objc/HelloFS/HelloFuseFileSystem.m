//
//  HelloFuseFileSystem.m
//  GoogleHelloFuse
//
//  Created by alcor on 12/15/06.
//  Copyright 2006 Google. All rights reserved.
//

#import "HelloFuseFileSystem.h"


static NSString *helloStr = @"Hello World!\n";
static NSString *helloPath = @"/hello.txt";

@implementation HelloFuseFileSystem

- (NSArray *)contentsOfDirectoryAtPath:(NSString *)path error:(NSError **)error {
  return [NSArray arrayWithObject:[helloPath lastPathComponent]];
}

- (NSData *)contentsAtPath:(NSString *)path {
  if ([path isEqualToString:helloPath])
    return [helloStr dataUsingEncoding:NSUTF8StringEncoding];  
  return nil;
}


#pragma optional icon method

- (BOOL)usesResourceForks{
return YES;
}
- (NSString *)iconFileForPath:(NSString *)path {
  if ([path isEqualToString:@"/"])
    return [[NSBundle mainBundle] pathForResource:@"Fuse" ofType:@"icns"];
  if ([path isEqualToString:helloPath])
    return [[NSBundle mainBundle] pathForResource:@"hellodoc" ofType:@"icns"];
  return nil;
}

@end
