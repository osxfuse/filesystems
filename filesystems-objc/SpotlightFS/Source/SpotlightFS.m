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
//  SpotlightFS.m
//  SpotlightFS
//
//  Created by Greg Miller <jgm@> on 1/19/07.
//

// The SpotlightFS file system looks roughly like:
// 
// /Volumes/SpotlightFS/
//                |
//                `-> SmarterFolder/
//                             |
//                             `-> ...
//                                  `-> :Users:blah:blah -> /Users/blah/blah
//                                  `-> :Users:blah:google -> /Users/blah/google
//                |
//                `-> SpotlightSavedSearch1/
//                             `-> :Users:foo:result -> /Users/foo/result
//                             `-> :Users:foo:blah -> /Users/foo/blah
//                |
//                `-> SpotlightSavedSearch2/
//                             `-> ...
//
//                |
//                `-> FooBar
//                      `-> :Users:foo:foobar -> /Users/foo/foobar
//                      `-> ...
//

#import <sys/types.h>
#import <unistd.h>
#import <CoreServices/CoreServices.h>
#import <Foundation/Foundation.h>
#import <OSXFUSE/OSXFUSE.h>
#import "SpotlightFS.h"
#import "NSError+POSIX.h"

// Key name for use in NSUserDefaults
static NSString* const kDefaultsSearchDirectories = @"SearchDirectories";

// The name of the top-level "smarter folder" that can be used to view the 
// contents of any random folder (and thus, Spotlight search)
static NSString* const kSmarterFolder = @"SmarterFolder";

// Path and file extension used to lookup Spotlight's saved searches
static NSString* const kSpotlightSavedSearchesPath = @"~/Library/Saved Searches";
static NSString* const kSpotlightSavedSearchesExtension = @"savedSearch";


// EncodePath
//
// Given a path of the form /Users/foo/bar, returns the string in the form
// :Users:foo:bar.  Before this encoding takes place, all colons in the path
// are replaced with the '|' character.  This means that paths which actually
// have the '|' character in them won't decode correctly, but that's fine for
// this little example file system.
//
static NSString *EncodePath(NSString *path) {
  path = [[path componentsSeparatedByString:@":"]
                   componentsJoinedByString:@"|"];
  return [[path componentsSeparatedByString:@"/"]
                   componentsJoinedByString:@":"];
}

// DecodePath
//
// Given a path of the form :Users:foo:bar, returns the path in the form
// /Users/foo/bar.
//
static NSString *DecodePath(NSString *path) {
  path = [[path componentsSeparatedByString:@":"]
                   componentsJoinedByString:@"/"];
  return [[path componentsSeparatedByString:@"|"]
                   componentsJoinedByString:@":"];
}


@implementation SpotlightFS

// -spotlightSavedSearches
//
// Returns an NSArray of filenames matching
// ~/Library/Saved Searches/*.savedSearch
//
- (NSArray *)spotlightSavedSearches {
  NSString *savedSearchesPath = [kSpotlightSavedSearchesPath stringByStandardizingPath];
  NSMutableArray *savedSearches = [NSMutableArray array];
  NSArray *files = [[NSFileManager defaultManager] directoryContentsAtPath:savedSearchesPath];
  for (NSString* filename in files) {
    if ([[filename pathExtension] isEqualToString:kSpotlightSavedSearchesExtension])
      [savedSearches addObject:[filename stringByDeletingPathExtension]];
  }
  return savedSearches;
}

// -contentsOfSpotlightSavedSearchNamed:
// 
// Returns the named Spotlight saved search plist parsed as an NSDictionary.
//
- (NSDictionary *)contentsOfSpotlightSavedSearchNamed:(NSString *)name {
  if (!name)
    return nil;
  
  NSString *savedSearchesPath = [kSpotlightSavedSearchesPath stringByStandardizingPath];
  NSDictionary *contents = nil;
  
  // Append the .savedSearch extension if necessary
  if (![[name pathExtension] isEqualToString:kSpotlightSavedSearchesExtension])
    name = [name stringByAppendingPathExtension:kSpotlightSavedSearchesExtension];
      
  NSString *fullpath = [savedSearchesPath stringByAppendingPathComponent:name];
  contents = [NSDictionary dictionaryWithContentsOfFile:fullpath];
  
  return contents;
}

// -userCreatedFolders
//
// Returns all the top-level folders that the user explicitly craeted.  These
// folders are stored in the NSUserDefaults databaes for the running app.
//
- (NSArray *)userCreatedFolders {
  NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
  NSArray *userCreatedFolders = [defaults stringArrayForKey:kDefaultsSearchDirectories];
  if (userCreatedFolders == nil)
    userCreatedFolders = [NSArray array];
  return userCreatedFolders;
}

// -isUserCreatedFolder:
//
// Returns YES if the specified folder is a user created folder. Also try
// prepending a leading slash to the user created folders, incase |path| starts
// with a slash.
//
- (BOOL)isUserCreatedFolder:(NSString *)path {
  NSArray *folders = [self userCreatedFolders];
  for (NSString* folder in folders) {
    if ([folder isEqualToString:path])
      return YES;
    if ([[@"/" stringByAppendingPathComponent:folder] isEqualToString:path])
      return YES;
  }
  return NO;
}

// -setUserCreatedFolders:
//
// Sets the folder names to use for the top-level user-created folders.
//
- (void)setUserCreatedFolders:(NSArray *)folders {
  [[NSUserDefaults standardUserDefaults] setObject:folders
                                            forKey:kDefaultsSearchDirectories];
}

// -addUserCreatedFolder:
//
// Adds the specified user-created folder to the list of all user-created folders.
//
- (void)addUserCreatedFolder:(NSString *)folder {
  if (!folder)
    return;
  
  NSArray *currentFolders = [self userCreatedFolders];
  
  if ([currentFolders containsObject:folder])
    return;
  
  NSMutableArray *folders = [[currentFolders mutableCopy] autorelease];
  [folders addObject:folder];
  [self setUserCreatedFolders:folders];
}

// -removeUserCreatedFolder:
//
// Removes the specified folder from the list of user-created folders.
//
- (void)removeUserCreatedFolder:(NSString *)folder {
  if (!folder)
    return;
  NSArray *currentFolders = [self userCreatedFolders];
  NSMutableArray *folders = [[currentFolders mutableCopy] autorelease];
  [folders removeObject:folder];
  [self setUserCreatedFolders:folders];
}

// -topLevelDirectories
//
// Returns an NSArray of all top-level folders.  This includes all Spotlight's
// saved search folders, user-created smart folders, and our "SmarterFolder".
//
- (NSArray *)topLevelDirectories {
  NSArray *spotlightSavedSearches = [self spotlightSavedSearches];
  NSArray *userCreatedFolders = [self userCreatedFolders];
  return [[spotlightSavedSearches arrayByAddingObjectsFromArray:userCreatedFolders]
                                  arrayByAddingObject:kSmarterFolder];
}

// -encodedPathResultsForSpotlightQuery:
//
// This method is what actually runs the given spotlight query.  We first try to
// create an MDQuery from the given query directly.  If this fails, we try to
// create a query using the given query string as the text to match.  Once we
// have a valid MDQuery, we execute it synchronously, then we create and return
// an NSArray of all the matching file paths (encoded).
//
- (NSArray *)encodedPathResultsForSpotlightQuery:(NSString *)queryString
                                           scope:(NSArray *)scopeDirectories {
  // Try to create an MDQueryRef from the given queryString.
  //  MDQueryRef query = MDQueryCreate(kCFAllocatorDefault,
  //                                   (CFStringRef)queryString,
  //                                   NULL, NULL);
  
  // 10/8/2007 Apple bug - radar 5529459
  // This does not work on Leopard, because MDQueryCreate() does not return NULL
  // when given an improperly formatted query string, as it's documented to do.
  // One way to work around this is to see if the query string contains an "="
  // and if so assume the query string is properly formatted, otherwise, format
  // the query ourselves, using the logic in the if body below. This is a hack
  // and hopefully will be fixed soon (10.5.2???) in which case we will remove 
  // this.
  
  // The previous MDQueryCreate will fail if queryString isn't a validly
  // formatted MDQuery.  In this case, we'll create a valid MDQuery and try
  // again.
  
  if ([queryString rangeOfString:@"="].location == NSNotFound) {
    queryString = [NSString stringWithFormat:
                   @"* == \"%@\"wcd || kMDItemTextContent = \"%@\"c",
                   queryString, queryString
                   ];
    //
    //    query = MDQueryCreate(kCFAllocatorDefault,
    //                          (CFStringRef)queryString,
    //                          NULL, NULL);
  }
  
  MDQueryRef query = MDQueryCreate(kCFAllocatorDefault,
                                   (CFStringRef)queryString,
                                   NULL, NULL);  
  if (query == NULL)
    return nil;
  
  if (scopeDirectories)
    MDQuerySetSearchScope(query, (CFArrayRef)scopeDirectories, 0 /* options */);
  
  // Create and execute the query synchronously.
  Boolean ok = MDQueryExecute(query, kMDQuerySynchronous);
  if (!ok) {
    NSLog(@"failed to execute query\n");
    CFRelease(query);
    return nil;
  }
  
  int count = MDQueryGetResultCount(query);
  NSMutableArray *symlinkNames = [NSMutableArray array];
  
  for (int i = 0; i < count; i++) {
    MDItemRef item = (MDItemRef)MDQueryGetResultAtIndex(query, i);
    NSString *name = (NSString *)MDItemCopyAttribute(item, kMDItemPath);
    [name autorelease];
    [symlinkNames addObject:EncodePath(name)];
  }
  
  CFRelease(query);
  
  return symlinkNames;
}


#pragma mark == Overridden GMUserFileSystem Delegate Methods


- (NSArray *)contentsOfDirectoryAtPath:(NSString *)path error:(NSError **)error {
  if (!path) {
    if ( error ) {
      *error = [NSError errorWithPOSIXCode:EINVAL];
    }
    return nil;
  }
  
  NSString *lastComponent = [path lastPathComponent];
  
  if ([lastComponent isEqualToString:@"/"]) {
    return [self topLevelDirectories];
  }
  
  // Special case the /SmarterSearches folder to have it appear empty
  if ([lastComponent isEqualToString:kSmarterFolder])
    return nil;
  
  NSString *query = lastComponent;
  NSArray *scopeDirectories = nil;
  
  // If we're supposed to display the contents for a spotlight saved search
  // directory, then we want to use the RawQuery from the saved search's plist.
  // Otherwise, we just use the directory name itself as the query.
  if ([[self spotlightSavedSearches] containsObject:lastComponent]) {
    NSDictionary *ssPlist = [self contentsOfSpotlightSavedSearchNamed:lastComponent];
    query = [ssPlist objectForKey:@"RawQuery"];
    scopeDirectories = [[ssPlist objectForKey:@"SearchCriteria"]
                                 objectForKey:@"FXScopeArrayOfPaths"];
  }
  
  return [self encodedPathResultsForSpotlightQuery:query scope:scopeDirectories];
}

- (BOOL)createDirectoryAtPath:(NSString *)path
                   attributes:(NSDictionary *)attributes
                        error:(NSError **)error {
  if (!path) {
    if ( error ) {
      *error = [NSError errorWithPOSIXCode:EINVAL];
    }
    return NO;
  }
  
  // We only allow directories to be created at the very top level
  NSString *dirname = [path stringByDeletingLastPathComponent];
  if ([dirname isEqualToString:@"/"]) {
    [self addUserCreatedFolder:[path lastPathComponent]];
    return YES;
  }
  
  return NO;
}

- (BOOL)fileExistsAtPath:(NSString *)path isDirectory:(BOOL *)isDirectory {
  if (!path || !isDirectory)
    return NO;

  NSArray *tlds = [self topLevelDirectories];
  int numComponents = [[path pathComponents] count];
  
  // Handle the top level root directory
  if ([path isEqualToString:@"/"]) {
    *isDirectory = YES;
    return YES;
  }

  // Handle "._" and "Icon\r" that we don't deal with.
  NSString* lastComponent = [path lastPathComponent];
  if ([lastComponent hasPrefix:@"._"] ||
      [lastComponent isEqualToString:@"Icon\r"]) {
    return NO;
  }

  // Handle stuff in the /SmarterFolder
  if ([path hasPrefix:[@"/" stringByAppendingString:kSmarterFolder]]) {
    // We don't allow the creation of folders in the smarter folder.  But
    // before the Finder actually attempts to create a folder, it checks for 
    // existence.  So, we always report that a folder named "untitled folder"
    // does *not* exist.  That way, Finder will then try to create that folder,
    // we'll return an error, and the user will get a reasonable error message.
    if ([lastComponent hasPrefix:@"untitled folder"])
      return NO;
    
    // We report all other directories as existing
    *isDirectory = YES;
    return YES;
  }
  
  // Handle other top-level directories, which may contain spotlight's saved
  // searches, as well as other user-created folders.
  if (numComponents == 2 && [tlds containsObject:lastComponent]) {
    *isDirectory = YES;
    return YES;
  }
  
  // Handle symlinks in any of the top level directories, e.g. 
  // /foo/symlink
  if (numComponents == 3) {
    // See the comments above for why we have to special case "untitled folder"
    if ([[path lastPathComponent] hasPrefix:@"untitled folder"])
      return NO;
    
    *isDirectory = NO;
    return YES;
  }
  
  // If the default is YES then finder will hang when trying to create a new 
  // Folder (because it will keep probing to try to find an unused Folder name)
  return NO;
}

// By default, directories are not writeable, with the notable exceptions below:
// - Slash is writable
// - User created directories in slash are writable
- (NSDictionary *)attributesOfItemAtPath:(NSString *)path userData:(id)userData error:(NSError **)error {
  if (!path) {
    if ( error ) {
      *error = [NSError errorWithPOSIXCode:EINVAL];
    }
    return nil;
  }
  BOOL isDirectory;
  if (![self fileExistsAtPath:path isDirectory:&isDirectory]) {
    if ( error ) {
      *error = [NSError errorWithPOSIXCode:ENOENT];
    }
    return nil;
  }
  
  NSMutableDictionary *attr = nil;
  int mode = 0500;
  
  NSString *pathdir = [path stringByDeletingLastPathComponent];
  NSString *smarter = [@"/" stringByAppendingString:kSmarterFolder];
  
  if ([pathdir isEqualToString:@"/"] || [pathdir isEqualToString:smarter]) {
    
    if ([path isEqualToString:@"/"] || [self isUserCreatedFolder:path]) {
      mode = 0700;
    }
    
    attr = [NSDictionary dictionaryWithObjectsAndKeys:
      [NSNumber numberWithInt:mode], NSFilePosixPermissions,
      [NSNumber numberWithInt:geteuid()], NSFileOwnerAccountID,
      [NSNumber numberWithInt:getegid()], NSFileGroupOwnerAccountID,
      [NSDate date], NSFileCreationDate,
      [NSDate date], NSFileModificationDate,
      (isDirectory ? NSFileTypeDirectory : NSFileTypeRegular), NSFileType,
      nil];
    
  } else {
    
    NSString *decodedPath = DecodePath([path lastPathComponent]);
    NSFileManager *fm = [NSFileManager defaultManager];
    attr = [[[fm fileAttributesAtPath:decodedPath traverseLink:NO] mutableCopy] autorelease];
    if (!attr)
      attr = [NSMutableDictionary dictionary];
    [attr setObject:NSFileTypeSymbolicLink forKey:NSFileType];
    
  } 
  if (!attr) {
    if ( error ) {
      *error = [NSError errorWithPOSIXCode:ENOENT];
    }
  }
  return attr;
}

- (NSString *)destinationOfSymbolicLinkAtPath:(NSString *)path error:(NSError **)error {
  if (!path) {
    if ( error ) {
      *error = [NSError errorWithPOSIXCode:EINVAL];
    }
    return nil;
  }
  
  NSString *lastComponent = [path lastPathComponent];
  
  if ([lastComponent hasPrefix:@":"])
    return DecodePath(lastComponent);
  
  if ( error ) {
    *error = [NSError errorWithPOSIXCode:ENOENT];
  }
  return nil;
}

- (BOOL)moveItemAtPath:(NSString *)source toPath:(NSString *)destination error:(NSError **)error {  
  if (!source || !destination) {
    if ( error ) {
      *error = [NSError errorWithPOSIXCode:EINVAL];
    }
    return NO;
  }
  
  NSArray *moveableDirs = [self userCreatedFolders];
  NSString *sourceBasename = [source lastPathComponent];
  
  // You can only rename user created directories at the top level, i.e., a 
  // directory that would have been created through a mkdir to this FS
  if (![moveableDirs containsObject:sourceBasename])
    return NO;
  
  NSString *destBasename = [destination lastPathComponent];
  
  // On this FS, you can't rename to a dir that already exists because we only
  // allow one level of directories
  if ([moveableDirs containsObject:destBasename])
    return NO;
  
  // OK, do the move
  [self removeUserCreatedFolder:sourceBasename];
  [self addUserCreatedFolder:destBasename];
  
  return YES;
}

- (BOOL)removeItemAtPath:(NSString *)path error:(NSError **)error {
  if (!path) {
    if ( error ) {
      *error = [NSError errorWithPOSIXCode:EINVAL];
    }
    return NO;
  }
  
  NSArray *components = [path pathComponents];
  int ncomp = [components count];
  if (ncomp < 2)
    return NO;
  
  NSArray *savedSearches = [self spotlightSavedSearches];
  NSString *firstDir = [components objectAtIndex:1];
  
  if ([firstDir isEqualToString:kSmarterFolder])
    return NO;
  else if ([savedSearches containsObject:firstDir])
    return NO;

  if (ncomp == 2)
    [self removeUserCreatedFolder:firstDir];
  
  return YES;
}

- (NSDictionary *)finderAttributesAtPath:(NSString *)path 
                                   error:(NSError **)error
{
  NSString *lastComponent = [path lastPathComponent];
  NSBundle *mainBundle = [NSBundle mainBundle];
  NSString *iconPath = [mainBundle pathForResource:@"SmartFolderBlue" ofType:@"icns"];
  
  if ([path isEqualToString:@"/"])
    return nil;  // Custom volume icon is handled by options to filesystem mount.
  
  if ([path isEqualToString:[@"/" stringByAppendingPathComponent:kSmarterFolder]])
    iconPath = [mainBundle pathForResource:@"DynamicFolderBlue" ofType:@"icns"];
  else if ([[self spotlightSavedSearches] containsObject:lastComponent])
    iconPath = [mainBundle pathForResource:@"SmartFolder" ofType:@"icns"];
  
  NSData* iconData = [NSData dataWithContentsOfFile:iconPath options:0 error:error];
  
  if ( iconData == nil )
    return nil;
  
  return [NSDictionary dictionaryWithObjectsAndKeys:iconData, kGMUserFileSystemCustomIconDataKey, nil];
}

@end
