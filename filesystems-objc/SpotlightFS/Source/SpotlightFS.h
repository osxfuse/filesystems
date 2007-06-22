//
//  SpotlightFS.h
//  SpotlightFS
//
//  Created by Greg Miller <jgm@> on 1/19/07.
//  Copyright 2007 Google Inc. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import "FUSEFileSystem.h"

// A MacFUSE file system for creating real smart folders.  The SpotlightFS
// file system may consist of some top-level directories, and each of these
// directories contain symbolic links to files matching some Spotlight query.
//
// In the simplest case, the Spotlight query used to generate the contents of 
// a directory is simply the name of the diretory itself.  For example, a 
// directory named /Volumes/SpotlightFS/foo would contain symbolic links to
// all files that match a Spotlight query for the word "foo".
//
// Additionally, SpotlightFS looks in "~/Library/Saved Searches" for all files
// with a ".savedSearch" extension, and automatically creates top-level
// directories for these files.  The Spotlight query which generates the
// contents for these directories is found by reading the "RawQuery" key from
// the saved search plist.
//
// See the documentation for FUSEFileSystem for more details.
//
@interface SpotlightFS : FUSEFileSystem {
  // Empty
}

// Returns an array of all *.savedSearch files in "~/Library/Saved Searches".
// The returned paths have the ".savedSearch" extension removed.  If no 
// saved searches are found, then an empty array is returned rather than nil.
//
- (NSArray *)spotlightSavedSearches;

// Returns a dictionary with the contents of the saved search file named "name",
// or "name.savedSearch".
//
- (NSDictionary *)contentsOfSpotlightSavedSearchNamed:(NSString *)name;

// Returns all the user-created search folders.
//
- (NSArray *)userCreatedFolders;

// Returns YES if |path| is a user created folder.
//
- (BOOL)isUserCreatedFolder:(NSString *)path;

// Sets the full user-created folders array.
//
- (void)setUserCreatedFolders:(NSArray *)folders;

// Adds one search (i.e. directory name) to the list of user-created folders.
// As an example, "mkdir /Volumes/SpotlightFS/foo" will ultimately call through
// to this method to create the user-defined search for "foo".
//
- (void)addUserCreatedFolder:(NSString *)folder;

// Remove a user-defined search.
//
- (void)removeUserCreatedFolder:(NSString *)folder;

// Returns the concatenation of -spotlightSavedSearches and -userCreatedFolderes
//
- (NSArray *)topLevelDirectories;

// Runs the Spotlight query specified by "queryString", and returns the paths
// for all the matching files.  The returned paths are encoded, meaning that
// all forward slashes have been replaced by colons.
//
- (NSArray *)encodedPathResultsForSpotlightQuery:(NSString *)queryString
                                           scope:(NSArray *)scopeDirectories;

@end
