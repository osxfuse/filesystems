//
//  NSImage+IconData.h
//  MacFUSE
//
//  Created by ted on 12/28/07.
//  Copyright 2007 Google, Inc. All rights reserved.
//
#import <Cocoa/Cocoa.h>

@interface NSImage (IconData)

// Creates the data for a .icns file from this NSImage. This uses the IconFamily
// class to create various sized representation of this NSImage in icns format.
- (NSData *)icnsData;

@end
