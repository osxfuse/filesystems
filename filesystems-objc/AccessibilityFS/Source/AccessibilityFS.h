//
//  AccessibilityFS.h
//  AccessibilityFS
//
//  Created by Dave MacLachlan <dmaclach@> on 12/14/07.
//
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

// A MacFUSE file system for probing around through various applications'
// UIs exposed through Accessibility.
@interface AccessibilityFS : NSObject {
  @private
  // A collection of info about processes keyed by their GMAXUIElement.
  NSMutableDictionary *appDictionary_; 
  
  // The last time root was modified due to an app launching or terminating.
  NSDate *rootModifiedDate_;
}
@end
