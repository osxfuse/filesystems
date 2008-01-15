//
//  GTMAXUIElement.h
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

#import <Foundation/Foundation.h>

// A objc wrapper for a AXUIElement.
// We implement hash and isEqual so they can be used as keys.
@interface GTMAXUIElement : NSObject <NSCopying> {
  AXUIElementRef element_;
}

// Returns true if this app can use accessibility. Checks to see if API is
// enabled or this process is trusted. If this does not return true, all
// of the methods below will fail in weird and wonderful ways.
+ (BOOL)isAccessibilityEnabled;

// Returns a GTMAXUIElement for the system wide element
+ (id)systemWideElement;

// Returns a GTMAXUIElement for the given element
+ (id)elementWithElement:(AXUIElementRef)element;

// Returns a GTMAXUIElement for the given process
+ (id)elementWithProcessIdentifier:(pid_t)pid;

// Initialized as the element for the current process
- (id)init;

// Initialized with the given element (designated initializer)
- (id)initWithElement:(AXUIElementRef)element;

// Initialized wih the element for the pid.
- (id)initWithProcessIdentifier:(pid_t)pid;

// Returns the element we are wrapping.
- (AXUIElementRef)element;

// Attributes
// Returns the list of supported attribute names
- (NSArray*)accessibilityAttributeNames;

// Returns value for attribute. 
// If attribute has no value returns NSNull.
// Returns nil on error.
- (id)accessibilityAttributeValue:(NSString*)attribute;

// Returns YES is attribute is settable
- (BOOL)accessibilityIsAttributeSettable:(NSString*)attribute;

// Returns YES if attribute is set to value
- (BOOL)setAccessibilityValue:(CFTypeRef)value forAttribute:(NSString*)attribute;

// Returns the number of values in the array of the atribute returns an array.
// Returns -1 on error.
- (int)accessibilityAttributeValueCount:(NSString*)attribute;

// parameterized attribute methods
// Returns the list of supported parameterized attributes
- (NSArray *)accessibilityParameterizedAttributeNames;
// 
// Returns the value for a parameterized attribute.
// If attribute has no value, returns NSNull.
// returns nil on error
- (id)accessibilityAttributeValue:(NSString *)attribute forParameter:(id)parameter;

// action methods
// Returns the list of actions supported by the element
- (NSArray *)accessibilityActionNames;
// Returns a localized name of the action description
- (NSString *)accessibilityActionDescription:(NSString *)action;
// Returns YES if action is performed.
- (BOOL)performAccessibilityAction:(NSString *)action;

// Returns a string value for the given attribute if possible.
- (NSString*)stringValueForAttribute:(NSString*)attribute;

// Sets a value for the given attribute if possible. Returns YES on success.
- (BOOL)setStringValue:(NSString*)string forAttribute:(NSString*)attribute;

// Processes
// Returns the pid for the parent process of theelement.
- (pid_t)processIdentifier;
// Returns a GMAXUIElement wrappiung the parent process of the element.
- (GTMAXUIElement*)processElement;
@end