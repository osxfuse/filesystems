//
//  GTMAXUIElement.m
//  AccessibilityFS
//
//  Created by Dave MacLachlan on 2008/01/11.
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

#import "GTMAXUIElement.h"
#import <unistd.h>
#import <AppKit/AppKit.h>

@interface GTMAXUIElement (GTMAXUIElementTypeConversion)
- (NSString*)stringValueForCFType:(CFTypeRef)cfValue;
- (NSString*)stringValueForCFArray:(CFArrayRef)cfArray;
- (NSString*)stringValueForAXValue:(AXValueRef)axValueRef;
- (CFTypeRef)createCFTypeOfSameTypeAs:(CFTypeRef)valueType 
                           withString:(NSString*)string;
- (AXValueRef)createAXValueOfType:(AXValueType)type
                       withString:(NSString*)stringValue;
- (CFTypeRef)stringToBool:(NSString*)string;
@end

@implementation GTMAXUIElement
+ (BOOL)isAccessibilityEnabled {
  return AXAPIEnabled() | AXIsProcessTrusted();
}

+ (id)systemWideElement {
  AXUIElementRef elementRef = AXUIElementCreateSystemWide();
  GTMAXUIElement *element = [[[[self class] alloc] initWithElement:elementRef] autorelease];
  CFRelease(elementRef);
  return element;
}

+ (id)elementWithElement:(AXUIElementRef)element {
  return [[[[self class] alloc] initWithElement:element] autorelease];
}

+ (id)elementWithProcessIdentifier:(pid_t)pid {
  return [[[[self class] alloc] initWithProcessIdentifier:pid] autorelease];
}

- (id)init {
  return [self initWithProcessIdentifier:getpid()];
}

- (id)initWithElement:(AXUIElementRef)element {
  if ((self = [super init])) {
    if (!element) {
      [self release];
      return nil;
    }
    element_ = CFRetain(element);
  }
  return self;
}

- (id)initWithProcessIdentifier:(pid_t)pid {
  if ((self = [super init])) {
    if (pid) {
      element_ = AXUIElementCreateApplication(pid);
    }
    if (!element_) {
      [self release];
      return nil;
    }
  }
  return self;
}

- (void)dealloc {
  if (element_) {
    CFRelease(element_);
  }
  [super dealloc];
}

- (id)copyWithZone:(NSZone *)zone {
  return [[[self class] alloc] initWithElement:element_];
}

- (AXUIElementRef)element {
  return element_;
}

- (unsigned)hash {
  return CFHash(element_);
}

- (BOOL)isEqual:(id)object {
  BOOL equal = NO;
  if ([object isKindOfClass:[self class]]) {
    equal = CFEqual([self element], [object element]);
  }
  return equal;
}

- (NSString*)debugDescription {
  CFTypeRef cfDescription = CFCopyDescription(element_);
  NSString *description = [NSString stringWithFormat:@"%@ (%@)",
                           cfDescription, 
                           [self description]];
  CFRelease(cfDescription);
  return description;
}

- (NSString*)description {
  NSString *name = nil;
  NSString *role = [self accessibilityAttributeValue:NSAccessibilityRoleDescriptionAttribute];
  NSString *subname = [self accessibilityAttributeValue:NSAccessibilityTitleAttribute];
  NSNumber *index = [self accessibilityAttributeValue:NSAccessibilityIndexAttribute];
  if (!subname || [subname isEqual:[NSNull null]]) {
    subname = [self accessibilityAttributeValue:NSAccessibilityDescriptionAttribute];
  }  
  if (!subname || [subname isEqual:[NSNull null]]) {
    GTMAXUIElement *titleElement = [self accessibilityAttributeValue:NSAccessibilityTitleUIElementAttribute];
    if (titleElement && !([titleElement isEqual:[NSNull null]])) {
      subname = [titleElement accessibilityAttributeValue:NSAccessibilityTitleAttribute];
    }
  }
  if (subname && ![subname isEqual:[NSNull null]]) {
    name = [NSString stringWithFormat:@"%@ : %@", role, subname];
  } else {
    name = role;
  }
  if (index && ![index isEqual:[NSNull null]]) {
    name = [NSString stringWithFormat:@"%@ %@", name, index];
  }
  return name;  
}

- (int)accessibilityAttributeValueCount:(NSString*)attribute {
  CFIndex count;
  AXError error = AXUIElementGetAttributeValueCount(element_, 
                                                    (CFStringRef)attribute,
                                                    &count);
  if (error) {
    count = -1;
  }
  return count;
}
  
- (CFTypeRef)accessibilityCopyAttributeCFValue:(NSString*)attribute {
  CFTypeRef value = NULL;
  AXError error = AXUIElementCopyAttributeValue(element_, 
                                                (CFStringRef)attribute, 
                                                &value);
  if (error == kAXErrorNoValue) {
    value = kCFNull;
  } else if (error) {
    value = nil;
  }
  return value;
}

- (id)accessibilityAttributeValue:(NSString*)attribute {
  CFTypeRef value = [self accessibilityCopyAttributeCFValue:attribute];
  if (!value) return nil;
  id nsValue = nil;
  CFTypeID axTypeID = AXUIElementGetTypeID();
  if (CFGetTypeID(value) == axTypeID) {
    nsValue = [GTMAXUIElement elementWithElement:(AXUIElementRef)value];
  } else if (CFGetTypeID(value) == CFArrayGetTypeID()) {
    nsValue = [NSMutableArray array];
    NSEnumerator *enumerator = [(NSArray*)value objectEnumerator];
    id object;
    while ((object = [enumerator nextObject])) {
      if (CFGetTypeID((CFTypeRef)object) == axTypeID) {
        [nsValue addObject:[GTMAXUIElement elementWithElement:(AXUIElementRef)object]];
      } else {
        [nsValue addObject:object];
      }
    }
  } else {
    nsValue = [[(id)CFMakeCollectable(value) retain] autorelease];
  }
  CFRelease(value);
  return nsValue;
}

- (BOOL)accessibilityIsAttributeSettable:(NSString*)attribute {
  Boolean settable;
  AXError error = AXUIElementIsAttributeSettable(element_, 
                                                 (CFStringRef)attribute, 
                                                 &settable);
  if (error) {
    settable = FALSE;
  }
  return settable;
}

- (BOOL)setAccessibilityValue:(CFTypeRef)value forAttribute:(NSString*)attribute {
  AXError axerror = AXUIElementSetAttributeValue(element_, 
                                                 (CFStringRef)attribute, 
                                                 value);
  return axerror == kAXErrorSuccess;
}

- (NSArray*)accessibilityAttributeNames {
  CFArrayRef array = NULL;
  NSArray *nsArray = nil;
  AXError axerror = AXUIElementCopyAttributeNames(element_, &array);
  if (!axerror) {
    nsArray = [(NSArray*)CFMakeCollectable(array) autorelease];
  }
  return nsArray;
}

- (pid_t)processIdentifier {
  pid_t pid;
  AXError error = AXUIElementGetPid(element_, &pid);
  if (error) {
    pid = 0;
  }
  return pid;
}

- (GTMAXUIElement*)processElement {
  GTMAXUIElement *processElement = nil;
  pid_t pid = [self processIdentifier];
  if (pid) {
    processElement = [GTMAXUIElement elementWithProcessIdentifier:pid];
  }
  return processElement;
}

- (BOOL)performAccessibilityAction:(NSString*)action {
  return AXUIElementPerformAction(element_, (CFStringRef)action) == kAXErrorSuccess;
}

- (NSArray *)accessibilityActionNames {
  CFArrayRef array;
  NSArray *nsArray = nil;
  AXError error = AXUIElementCopyActionNames(element_, &array);
  if (!error) {
    nsArray = [(NSArray*)CFMakeCollectable(array) autorelease];
  }
  return nsArray;
}

- (NSString *)accessibilityActionDescription:(NSString *)action {
  CFStringRef description;
  NSString *nsDescription;
  AXError error = AXUIElementCopyActionDescription(element_, 
                                                   (CFStringRef)action,
                                                   &description);
  if (!error) {
    nsDescription = [(NSString*)CFMakeCollectable(description) autorelease];
  }
  return nsDescription;
}

- (NSArray *)accessibilityParameterizedAttributeNames {
  CFArrayRef names;
  NSArray *nsNames = nil;
  AXError error = AXUIElementCopyParameterizedAttributeNames(element_,
                                                             &names);
  if (!error) {
    nsNames = [(NSArray*)CFMakeCollectable(names) autorelease];
  }
  return nsNames;
}

- (id)accessibilityAttributeValue:(NSString *)attribute 
                     forParameter:(id)parameter {
  CFTypeRef value;
  id nsValue;
  AXError error = AXUIElementCopyParameterizedAttributeValue(element_,
                                                             (CFStringRef)attribute,
                                                             (CFTypeRef)parameter,
                                                             &value);
  if (error == kAXErrorNoValue) {
    nsValue = [NSNull null];
  } else if (!error) {
    nsValue = [(id)CFMakeCollectable(value) autorelease];
  }
  return nsValue;
}

- (NSString*)stringValueForAttribute:(NSString*)attribute {
  CFTypeRef cfValue = [self accessibilityCopyAttributeCFValue:attribute];
  NSString *stringValue = [self stringValueForCFType:cfValue];
  if (cfValue) {
    CFRelease(cfValue);
  }
  return stringValue;
}

- (BOOL)setStringValue:(NSString*)string forAttribute:(NSString*)attribute {
  CFTypeRef cfPreviousValue = [self accessibilityCopyAttributeCFValue:attribute];
  if (!cfPreviousValue || cfPreviousValue == kCFNull) return NO;
  CFTypeRef cfValue = [self createCFTypeOfSameTypeAs:cfPreviousValue withString:string];
  CFRelease(cfPreviousValue);
  BOOL isGood = [self setAccessibilityValue:cfValue forAttribute:attribute];
  if (cfValue) {
    CFRelease(cfValue);
  }
  return isGood;
}

@end

@implementation GTMAXUIElement (GTMAXUIElementTypeConversion)

- (CFTypeRef)stringToBool:(NSString*)string {
  CFTypeRef value = kCFBooleanFalse;
  if (string && [string length] > 0) {
    unichar uchar = [string characterAtIndex:0];
    if ((uchar == 'T') || (uchar == 't') 
        || (uchar == 'Y') || (uchar == 'y')) {
      value = kCFBooleanTrue;
    } else {
      value = [string intValue] != 0 ? kCFBooleanTrue : kCFBooleanFalse;
    }
  }
  return value;
}

- (AXValueRef)createAXValueOfType:(AXValueType)type 
                       withString:(NSString*)stringValue {
  union {
    CGPoint point;
    CGSize size;
    CGRect rect;
    CFRange range;
    AXError error;
  } axValue;
  
  switch (type) {
    case kAXValueCGPointType: {
      NSPoint nsValue = NSPointFromString(stringValue);
      axValue.point = *(CGPoint*)&nsValue;
      break;
    }
      
    case kAXValueCGSizeType: {
      NSSize nsValue = NSSizeFromString(stringValue);
      axValue.size = *(CGSize*)&nsValue;
      break;
    }
      
    case kAXValueCGRectType: {
      NSRect nsValue = NSRectFromString(stringValue);
      axValue.rect = *(CGRect*)&nsValue;
      break;
    }
      
    case kAXValueCFRangeType: {
      NSRange nsValue = NSRangeFromString(stringValue);
      axValue.range = *(CFRange*)&nsValue;
      break;
    }
      
    case kAXValueAXErrorType:
      axValue.error = [stringValue intValue];
      break;
      
    default:
      NSLog(@"Unknown AXValueType: %d", type);
      return NULL;
      break;
  }

  return AXValueCreate(type, &axValue);
}

- (NSString*)stringValueForAXValue:(AXValueRef)axValueRef {
  NSString *stringValue = nil;
  AXValueType axValueType = AXValueGetType(axValueRef);
  union {
    CGPoint point;
    CGSize size;
    CGRect rect;
    CFRange range;
    AXError error;
  } axValue;
  Boolean valueConvert = AXValueGetValue(axValueRef, axValueType, &axValue);
  if (!valueConvert) {
    NSLog(@"Unable to AXValueGetValue");
    return nil;
  }
  switch (axValueType) {
    case kAXValueCGPointType:
      stringValue = NSStringFromPoint(*(NSPoint*)&axValue);
      break;
      
    case kAXValueCGSizeType:
      stringValue = NSStringFromSize(*(NSSize*)&axValue);
      break;
      
    case kAXValueCGRectType:
      stringValue = NSStringFromRect(*(NSRect*)&axValue);
      break;
      
    case kAXValueCFRangeType:
      stringValue = NSStringFromRange(*(NSRange*)&axValue);
      break;
      
    case kAXValueAXErrorType:
      stringValue = [NSString stringWithFormat:@"%d", (*(AXError*)&axValue)];
      break;
      
    default:
      NSLog(@"Unknown AXValueType: %d", axValueType);
      break;
  }
  return stringValue;
}


- (NSString*)stringValueForCFArray:(CFArrayRef)cfArray {
  NSArray *array = (NSArray*)cfArray;
  NSEnumerator *arrayEnumerator = [array objectEnumerator];
  id value;
  NSMutableString *string = [NSMutableString stringWithString:@"{ "];
  value = [arrayEnumerator nextObject];
  if (value) {
    [string appendString:[self stringValueForCFType:(CFTypeRef)value]];
  }
  while ((value = [arrayEnumerator nextObject])) {
    [string appendFormat:@", %@", [self stringValueForCFType:(CFTypeRef)value]];
  }
  [string appendString:@" }"];
  return string;
}

- (NSString*)stringValueForCFType:(CFTypeRef)cfValue {
  NSString *stringValue = nil;
  if (!cfValue) return nil;
  CFTypeID cfType = CFGetTypeID(cfValue);
  if (cfType == CFStringGetTypeID()) {
    stringValue = [[(id)CFMakeCollectable(cfValue) retain] autorelease];
  } else if (cfType == CFURLGetTypeID()) {
    stringValue = [(NSURL*)cfValue absoluteString];
  } else if (cfType == CFNumberGetTypeID()) {
    stringValue = [(NSNumber*)cfValue stringValue];
  } else if (cfType == CFNullGetTypeID()) {
    stringValue = [NSString string];
  } else if (cfType == AXUIElementGetTypeID()) {
    stringValue = [[GTMAXUIElement elementWithElement:cfValue] description];
  } else if (cfType == AXValueGetTypeID()) {
    stringValue = [self stringValueForAXValue:cfValue];
  } else if (cfType == CFArrayGetTypeID()) {
    stringValue = [self stringValueForCFArray:cfValue];
  } else if (cfType == CFBooleanGetTypeID()) {
    stringValue = CFBooleanGetValue(cfValue) ? @"YES" : @"NO";
  } else {
    CFStringRef description = CFCopyDescription(cfValue);
    stringValue = [(id)CFMakeCollectable(description) autorelease];
  }
  return stringValue;       
}

  
- (CFTypeRef)createCFTypeOfSameTypeAs:(CFTypeRef)previousValue
                           withString:(NSString*)string {
  CFTypeRef value = NULL;
  CFTypeID valueType = CFGetTypeID(previousValue);
  if (valueType == CFStringGetTypeID()) {
    value = CFStringCreateCopy(NULL, (CFStringRef)string);
  } else if (valueType == CFURLGetTypeID()) {
    value = CFURLCreateWithString(NULL, (CFStringRef)string, NULL);
  } else if (valueType == CFNumberGetTypeID()) {
    double dValue = [string doubleValue];
    value = CFNumberCreate(NULL, kCFNumberDoubleType, &dValue);
  } else if (valueType == CFNullGetTypeID()) {
    value = kCFNull;
  } else if (valueType == AXValueGetTypeID()) {
    value = [self createAXValueOfType:AXValueGetType(previousValue) 
                           withString:string];
  } else if (valueType == CFBooleanGetTypeID()) {
    value = [self stringToBool:string];
  } 
  return value;
}

@end
