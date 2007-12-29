//
//  NSImage+IconData.m
//  MacFUSE
//
//  Created by ted on 12/28/07.
//  Copyright 2007 Google, Inc. All rights reserved.
//
#import "NSImage+IconData.h"
#import "IconFamily.h"

@interface IconFamily (RawData)

@end

@implementation IconFamily (RawData)

- (NSData *)iconData {
  return [NSData dataWithBytes:*hIconFamily length:GetHandleSize((Handle)hIconFamily)];
}

@end

@implementation NSImage (IconData)

- (NSData *)icnsData {
  IconFamily* family = [IconFamily iconFamilyWithThumbnailsOfImage:self];
  return [family iconData];
}

@end
