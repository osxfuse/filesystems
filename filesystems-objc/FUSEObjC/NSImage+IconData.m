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
//  NSImage+IconData.m
//  MacFUSE
//
//  Created by ted on 12/28/07.
//
#import <Foundation/Foundation.h>
#import "NSImage+IconData.h"

@implementation NSImage (IconData)

- (NSData *)icnsDataWithWidth:(int)width {
  OSType icnsType;
  switch (width) {
    case 128:
      icnsType = 'ic07';      // kIconServices128PixelDataARGB
      break;
    case 256:
      icnsType = kIconServices256PixelDataARGB;
      break;
    case 512:
      icnsType = 'ic09';     // kIconServices512PixelDataARGB
      break;
      
    default:
      NSLog(@"Invalid icon width: %d", width);
      return nil;      
  }
  
  // TODO: We should probably set the image source rect to preserve the aspect
  // ratio of the original image by clipping part of it.  This leads to a
  // smaller .icns file result, which is nice. Basically clip the top and 
  // bottom (or left and right) of part of the source image. This might also
  // lead to better performance?

  // See Cocoa Drawing Guide > Images > Creating a Bitmap
  // Also see IconStorage.h in OSServices.Framework in CoreServices umbrella.
  NSRect rect = NSMakeRect(0, 0, width, width);
  size_t bitmapSize = 4 * rect.size.width * rect.size.height;
  Handle bitmapHandle = NewHandle(bitmapSize);
  unsigned char* bitmapData = (unsigned char *)*bitmapHandle;
  unsigned char* planes[2];
  planes[0] = bitmapData;
  planes[1] = nil;
  NSBitmapFormat format = NSAlphaFirstBitmapFormat;
  // TODO: The docs say that the image data should be in non-premultiplied
  // format, but when NSAlphaNonpremultipliedBitmapFormat is used we get an error
  // about invalid parameters for graphics context.
  //   - I read somewhere that this used to work on Tiger?
  //   - Based on test images, things work fine even without this?
  //   - Maybe can use the vImage stuff to fix-up if need be?  See 
  //      vImageUnpremultiplyData_ARGB8888(...);
  // format |= NSAlphaNonpremultipliedBitmapFormat;
  NSBitmapImageRep* rep = 
    [[[NSBitmapImageRep alloc] 
      initWithBitmapDataPlanes:planes  // Single plane; just our bitmapData
                    pixelsWide:rect.size.width
                    pixelsHigh:rect.size.height
                 bitsPerSample:8 
               samplesPerPixel:4  // ARGB
                      hasAlpha:YES 
                      isPlanar:NO 
                colorSpaceName:NSCalibratedRGBColorSpace 
                  bitmapFormat:format
                   bytesPerRow:0  // Let it compute bytesPerRow and bitsPerPixel
                  bitsPerPixel:0] autorelease];
  [NSGraphicsContext saveGraphicsState];
  NSGraphicsContext* context = 
    [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
  [context setShouldAntialias:YES];  // TODO: Do we want this?
  [NSGraphicsContext setCurrentContext:context];
  [self drawInRect:rect fromRect:NSZeroRect operation:NSCompositeCopy fraction:1.0];
  [NSGraphicsContext restoreGraphicsState];

  // We need to use SetIconFamilyData here rather than just setting the raw
  // bytes because icon family will compress the bitmap for us in a format that
  // I haven't yet bothered to figure out. Maybe it is just compressed TIFF?
  Handle familyHandle = NewHandle(0);
  SetIconFamilyData((IconFamilyHandle)familyHandle, icnsType, bitmapHandle);
  DisposeHandle(bitmapHandle);
  NSData* data = 
    [NSData dataWithBytes:*familyHandle length:GetHandleSize(familyHandle)];
  DisposeHandle(familyHandle);
  return data;
}

@end
