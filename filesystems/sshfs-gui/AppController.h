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

#import <Cocoa/Cocoa.h>

@interface AppController : NSObject
{
  IBOutlet NSPanel *connectPanel_;
  IBOutlet NSTextField *pathField_;
  IBOutlet NSTextField *serverField_;
  IBOutlet NSTextField *usernameField_;
}

- (IBAction)showConnect:(id)sender;
- (IBAction)connectCancel:(id)sender;
- (IBAction)connectOK:(id)sender;

- (IBAction)clearRecents:(id)sender;

- (void)sshConnect:(NSString *)server
          username:(NSString *)username
              path:(NSString *)path;

- (void)addToRecents:(NSString *)server
            username:(NSString *)username
                path:(NSString *)path;
- (void)menuNeedsUpdate:(NSMenu *)menu;
- (void)connectToRecent:(id)sender;
@end
