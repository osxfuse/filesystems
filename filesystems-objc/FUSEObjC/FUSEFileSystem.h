//
//  FUSEFileSystem.h
//  FUSEObjC
//
//  Created by alcor on 12/6/06.
//  Copyright 2006 Google. All rights reserved.
//

#import <Cocoa/Cocoa.h>
@class FUSEFileWrapper;
@class GTResourceFork;

extern NSString* const FUSEManagedVolumeIcon;
extern NSString* const FUSEManagedDirectoryIconFile;
extern NSString* const FUSEManagedDirectoryIconResource;
extern NSString* const FUSEManagedFileResource;
extern NSString* const FUSEManagedDirectoryResource;

@interface FUSEFileSystem : NSObject {
  NSDictionary *files_;
  NSString *mountPoint_;
  BOOL isMounted_;  // Should Finder see that this filesystem has been mounted? 
}
+ (FUSEFileSystem *)sharedManager;

#pragma mark Required Methods

// In order to create the most minimal read-only filesystem possible then you
// need to override only the following four methods (declared below):
//
// - (BOOL)fileExistsAtPath:(NSString *)path isDirectory:(BOOL *)isDirectory;
// - (NSArray *)contentsOfDirectoryAtPath:(NSString *)path 
//                                  error:(NSError **)error;
// - (NSDictionary *)attributesOfItemAtPath:(NSString *)path 
//    error:(NSError **)error;
// - (NSData *)contentsAtPath:(NSString *)path;
 
#pragma mark Configuration

- (NSString *)mountName;
- (NSString *)mountPoint;
- (BOOL)isForeground;        // Defaults to YES  (Probably best to use default)
- (BOOL)isThreadSafe;        // Defaults to NO

// Returns an array of fuse options to set.  The set of available options can
// be found at:  http://code.google.com/p/macfuse/wiki/OPTIONS
// For example, to turn on debug output add @"debug" to the returned NSArray.
- (NSArray *)fuseOptions;  // Default: empty array

#pragma mark Lifecycle

- (BOOL)shouldStartFuse;

- (void)fuseWillMount;
- (void)fuseDidMount;

- (void)fuseWillUnmount;
- (void)fuseDidUnmount;

#pragma mark Resource Forks

- (BOOL)usesResourceForks; // Enable resource forks (icons, weblocs, more)

// Custom icons for provided files
- (NSString *)iconFileForPath:(NSString *)path; // path to icns file
- (NSImage *)iconForPath:(NSString *)path;
// you must be running in NSApplication (not command line tool) to use NSImage

#pragma mark Special Files

// Path of a real file to pass all function calls to
- (NSString *)passthroughPathForPath:(NSString *)path;

// Convenience methods used to provide resource fork for .webloc files
- (NSURL *)URLContentOfWeblocAtPath:(NSString *)path;

#pragma mark Moving an Item

- (BOOL)moveItemAtPath:(NSString *)source 
                toPath:(NSString *)destination
                 error:(NSError **)error;

#pragma mark Removing an Item

- (BOOL)removeItemAtPath:(NSString *)path error:(NSError **)error;

#pragma mark Creating an Item

- (BOOL)createDirectoryAtPath:(NSString *)path 
                   attributes:(NSDictionary *)attributes
                        error:(NSError **)error;

- (BOOL)createFileAtPath:(NSString *)path 
              attributes:(NSDictionary *)attributes
               outHandle:(id *)outHandle
                   error:(NSError **)error;

#pragma mark Linking an Item

- (BOOL)linkItemAtPath:(NSString *)path
                toPath:(NSString *)otherPath
                 error:(NSError **)error;

#pragma mark Symbolic Links

- (BOOL)createSymbolicLinkAtPath:(NSString *)path 
             withDestinationPath:(NSString *)otherPath
                           error:(NSError **)error;
- (NSString *)destinationOfSymbolicLinkAtPath:(NSString *)path
                                        error:(NSError **)error;

#pragma mark File Contents

// If contentsAtPath is implemented then you can skip open/release/read.
- (NSData *)contentsAtPath:(NSString *)path;

- (BOOL)openFileAtPath:(NSString *)path 
                  mode:(int)mode
             outHandle:(id *)outHandle
                 error:(NSError **)error;

- (void)releaseFileAtPath:(NSString *)path handle:(id)handle;

- (int)readFileAtPath:(NSString *)path 
               handle:(id)handle
               buffer:(char *)buffer 
                 size:(size_t)size 
               offset:(off_t)offset
                error:(NSError **)error;

- (int)writeFileAtPath:(NSString *)path 
                handle:(id)handle 
                buffer:(const char *)buffer
                  size:(size_t)size 
                offset:(off_t)offset
                 error:(NSError **)error;

- (BOOL)truncateFileAtPath:(NSString *)path 
                    offset:(off_t)offset 
                     error:(NSError **)error;

#pragma mark Directory Contents

- (BOOL)fileExistsAtPath:(NSString *)path isDirectory:(BOOL *)isDirectory;

- (NSArray *)contentsOfDirectoryAtPath:(NSString *)path error:(NSError **)error;

#pragma mark Getting and Setting Attributes

- (NSDictionary *)attributesOfItemAtPath:(NSString *)path 
                                   error:(NSError **)error;

- (NSDictionary *)attributesOfFileSystemForPath:(NSString *)path
                                          error:(NSError **)error;

- (BOOL)setAttributes:(NSDictionary *)attributes 
         ofItemAtPath:(NSString *)path
                error:(NSError **)error;

#pragma mark Extended Attributes

- (NSArray *)extendedAttributesForPath:path 
                                 error:(NSError **)error;

- (NSData *)valueOfExtendedAttribute:(NSString *)name 
                             forPath:(NSString *)path
                               error:(NSError **)error;

- (BOOL)setExtendedAttribute:(NSString *)name
                     forPath:(NSString *)path
                       value:(NSData *)value
                       flags:(int)flags
                       error:(NSError **)error;

//
// Advanced functions
//

#pragma mark Advanced Resource Forks and HFS headers

- (BOOL)pathHasResourceFork:(NSString *)path;

// Determines whether the given path is a for a resource managed by 
// FUSEFileSystem, such as a custom icon for a file. The optional
// "type" param is set to the type of the managed resource. The optional
// "dataPath" param is set to the file that represents this resource. For
// example, for a custom icon resource fork, this would be the corresponding 
// data fork. For a custom directory icon, this would be the directory itself.
- (BOOL)isManagedResourceAtPath:(NSString *)path type:(NSString **)type
                       dataPath:(NSString **)dataPath;

- (NSData *)managedContentsForPath:(NSString *)path;

// ._ location for a given path
- (NSString *)resourcePathForPath:(NSString *)path;
- (UInt16)finderFlagsForPath:(NSString *)path;

- (GTResourceFork *)resourceForkForPath:(NSString *)path;

// Raw data of resource fork
- (NSData *)resourceForkContentsForPath:(NSString *)path;

// HFS header (first 82 bytes of the ._ file)
- (NSData *)resourceHeaderForPath:(NSString *)path 
                         withResourceSize:(UInt32)size 
                                    flags:(UInt16)flags;

// Combined HFS header and Resource Fork
- (NSData *)resourceHeaderAndForkForPath:(NSString *)path
                                 includeResource:(BOOL)includeResource
                                           flags:(UInt16)flags;

#pragma mark Advanced File Operations

- (BOOL)fillStatBuffer:(struct stat *)stbuf 
               forPath:(NSString *)path
                 error:(NSError **)error;
- (BOOL)fillStatvfsBuffer:(struct statvfs *)stbuf 
                  forPath:(NSString *)path
                    error:(NSError **)error;

#pragma mark Advanced Icons

+ (NSData *)iconDataForImage:(NSImage *)image;
- (NSData *)iconDataForPath:(NSString *)path;
- (GTResourceFork *)customIconResourceForkForPath:(NSString *)path;

#pragma mark Internal Lifecycle

- (void)fuseInit;
- (void)fuseDestroy;
- (void)applicationDidFinishLaunching:(NSNotification *)notification;
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender;

#pragma mark Utility Methods

// Creates an autoreleased NSError in the NSPOSIXErrorDomain
+ (NSError *)errorWithCode:(int)code;

@end

