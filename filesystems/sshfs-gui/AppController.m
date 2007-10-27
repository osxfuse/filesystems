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

#import "AppController.h"
#import "DiskImageUtilities.h"

#import <Cocoa/Cocoa.h>
#import "GoogleShared_SystemVersion.h"

#include <stdlib.h>
#include <sys/param.h>
#include <sys/mount.h>

#define kRecentCount    10 // NSDocumentController's _recentsLimit ivar
#define kRecentServers  @"recentservers"
#define kServerName     @"server"
#define kUsernameName   @"username"
#define kPathName       @"pathname"

@implementation AppController

- (void)applicationWillFinishLaunching:(NSNotification *)notification {
  [DiskImageUtilities handleApplicationLaunchFromReadOnlyDiskImage];
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
  [self showConnect:self];
}

- (IBAction)showConnect:(id)sender {
  int connectResult = [NSApp runModalForWindow:connectPanel_];
  
  if (connectResult == NSRunAbortedResponse) {
    [connectPanel_ orderOut:self];
    return;
  }
  
  NSString *server   = [serverField_   stringValue];
  NSString *username = [usernameField_ stringValue];
  NSString *path     = [pathField_     stringValue];
  [connectPanel_ orderOut:self];
  [self sshConnect:server username:username path:path];
}

- (IBAction)connectCancel:(id)sender {
  [NSApp abortModal];
}

- (IBAction)connectOK:(id)sender {
  [NSApp stopModal];
}

- (void)sshConnect:(NSString *)server
          username:(NSString *)username
              path:(NSString *)path {
  
  // create a folder in /Volumes
  NSFileManager *fileManager = [NSFileManager defaultManager];
  NSString *mountpoint = [NSString stringWithFormat:@"/Volumes/%@", server];
  int tries = 0;
  while ([fileManager fileExistsAtPath:mountpoint])
    mountpoint = [NSString stringWithFormat:@"/Volumes/%@ %d", server, ++tries];
  [fileManager createDirectoryAtPath:mountpoint attributes:nil];
  
  // setup for task
  NSString *askPassPath = [[NSBundle mainBundle] pathForResource:@"askpass"
                                                          ofType:@""];
  NSString *sshfsPath = [[NSBundle mainBundle] pathForResource:@"sshfs-static"
                                                        ofType:@""];
  if ([GoogleShared_SystemVersion isLeopard]) {
      sshfsPath = [sshfsPath stringByAppendingString:@"-10.5"];
  }
  NSDictionary *sshfsEnv = [NSDictionary dictionaryWithObjectsAndKeys:
    @"NONE", @"DISPLAY", // I know "NONE" is bogus; I just need something
    askPassPath, @"SSH_ASKPASS",
    nil];
    
  NSArray *sshfsArgs = [NSArray arrayWithObjects:
    [NSString stringWithFormat:@"%@@%@:%@", username, server, path],
    [NSString stringWithFormat:@"-ovolname=%@", server],
    @"-oping_diskarb",
    @"-oreconnect",
    @"-oNumberOfPasswordPrompts=1",
    mountpoint,
    nil
    ];
  
  // fire it off
  NSTask *sshfsTask = [[[NSTask alloc] init] autorelease];
  [sshfsTask setLaunchPath:sshfsPath];
  [sshfsTask setArguments:sshfsArgs];
  [sshfsTask setEnvironment:sshfsEnv];
  
  [sshfsTask launch];
  [sshfsTask waitUntilExit];
  if ([sshfsTask terminationStatus]) {
    [fileManager removeFileAtPath:mountpoint handler:nil];
  }
  else {
    [self addToRecents:server username:username path:path];
  }
}

- (IBAction)clearRecents:(id)sender {
  NSUserDefaults *userDefaults = [NSUserDefaults standardUserDefaults];
  [userDefaults setObject:[NSArray array] forKey:kRecentServers];
}

- (void)addToRecents:(NSString *)server
            username:(NSString *)username
                path:(NSString *)path {
  // make the item
  NSDictionary *newRecentItem = [NSDictionary dictionaryWithObjectsAndKeys:
    server, kServerName, username, kUsernameName, path, kPathName, nil];
  
  // get our list
  NSUserDefaults *userDefaults = [NSUserDefaults standardUserDefaults];
  NSArray *oldRecents = [userDefaults arrayForKey:kRecentServers];
  if (!oldRecents)
    oldRecents = [NSArray array];
  
  // we don't want dups, so remove it if it's already there
  NSMutableArray *newRecents = [NSMutableArray arrayWithArray:oldRecents];
  [newRecents removeObject:newRecentItem];
  
  // insert and save
  [newRecents insertObject:newRecentItem atIndex:0];
  if ([newRecents count] > kRecentCount)
    [newRecents removeObjectsInRange:
      NSMakeRange(kRecentCount, [newRecents count] - kRecentCount)];
  
  [userDefaults setObject:newRecents forKey:kRecentServers];
}

- (void)menuNeedsUpdate:(NSMenu *)menu {
  // pull out all the old menu items
  NSMenuItem *item = [menu itemAtIndex:0];
  while ([item representedObject] != nil || [item isSeparatorItem]) {
    [menu removeItemAtIndex:0];
    item = [menu itemAtIndex:0];
  }
  
  // get the new items
  NSUserDefaults *userDefaults = [NSUserDefaults standardUserDefaults];
  NSArray *recents = [userDefaults arrayForKey:kRecentServers];
  if (!recents)
    recents = [NSArray array];
  
  // put them in
  for (int i=0; i < [recents count]; ++i) {
    NSDictionary *recent = [recents objectAtIndex:i];
    NSString *recentName = [NSString stringWithFormat:@"%@ %C %@ %C %@",
      [recent objectForKey:kServerName], 0x00B7,
      [recent objectForKey:kUsernameName], 0x00B7,
      [recent objectForKey:kPathName]];
    NSMenuItem *recentMenuItem =
      [[[NSMenuItem alloc] initWithTitle:recentName
                                  action:@selector(connectToRecent:)
                           keyEquivalent:@""] autorelease];
    [recentMenuItem setTarget:self];
    [recentMenuItem setRepresentedObject:recent];
    
    [menu insertItem:recentMenuItem atIndex:i];
  }
  
  // put in a separator if we put any items in
  if ([recents count]) {
    [menu insertItem:[NSMenuItem separatorItem] atIndex:[recents count]];
  }
}

- (void)connectToRecent:(id)sender {
  NSDictionary *connect = [sender representedObject];
  [self sshConnect:[connect objectForKey:kServerName]
          username:[connect objectForKey:kUsernameName]
              path:[connect objectForKey:kPathName]];
}

@end
