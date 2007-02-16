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

//
// Required methods
//

- (NSArray *)directoryContentsAtPath:(NSString *)path; // Array of NSStrings
- (BOOL)fileExistsAtPath:(NSString *)path isDirectory:(BOOL *)isDirectory;
- (NSDictionary *)fileAttributesAtPath:(NSString *)path;
- (NSData *)contentsAtPath:(NSString *)path;

//
// Optional methods
//

#pragma mark Resource Forks

- (BOOL)usesResourceForks; // Enable resource forks (icons, weblocs, more)

// Custom icons for provided files
- (NSString *)iconFileForPath:(NSString *)path; // path to icns file
- (NSImage *)iconForPath:(NSString *)path;
// you must be running in NSApplication (not command line tool) to use NSImage


#pragma mark Writing

- (BOOL)createDirectoryAtPath:(NSString *)path attributes:(NSDictionary *)attributes;

 // TODO: Support the single-shot create with data if we can.
 //- (BOOL)createFileAtPath:(NSString *)path contents:(NSData *)contents 
 //              attributes:(NSDictionary *)attributes;
- (BOOL)createFileAtPath:(NSString *)path attributes:(NSDictionary *)attributes;
- (BOOL)movePath:(NSString *)source toPath:(NSString *)destination handler:(id)handler;
- (BOOL)removeFileAtPath:(NSString *)path handler:(id)handler;


#pragma mark Lifecycle

- (NSString *)mountName;
- (NSString *)mountPoint;

- (BOOL)shouldMountInFinder; // Defaults to NO
- (BOOL)shouldStartFuse;

- (void)fuseWillMount;
- (void)fuseDidMount;

- (void)fuseWillUnmount;
- (void)fuseDidUnmount;


#pragma mark Special Files

// Path of a real file to pass all function calls to
- (NSString *)passthroughPathForPath:(NSString *)path;

// Convenience methods used to provide resource fork for .webloc files
- (NSURL *)URLContentOfWeblocAtPath:(NSString *)path;

- (NSDictionary *)fileSystemAttributesAtPath:(NSString *)path;

// Contents for a symbolic link (must have specified NSFileType as NSFileTypeSymbolicLink)
- (NSString *)pathContentOfSymbolicLinkAtPath:(NSString *)path;
- (BOOL)createSymbolicLinkAtPath:(NSString *)path pathContent:(NSString *)otherPath;
- (BOOL)linkPath:(NSString *)source toPath:(NSString *)destination handler:(id)handler;



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

- (id)openFileAtPath:(NSString *)path mode:(int)mode;
- (int)readFileAtPath:(NSString *)path handle:(id)handle
               buffer:(char *)buffer size:(size_t)size offset:(off_t)offset;

- (int)writeFileAtPath:(NSString *)path handle:(id)handle buffer:(const char *)buffer
                  size:(size_t)size offset:(off_t)offset;
- (void)releaseFileAtPath:(NSString *)path handle:(id)handle;

- (BOOL)fillStatBuffer:(struct stat *)stbuf forPath:(NSString *)path;
- (BOOL)fillStatvfsBuffer:(struct statvfs *)stbuf forPath:(NSString *)path;

- (BOOL)truncateFileAtPath:(NSString *)path offset:(off_t)offset;

- (NSArray *)extendedAttributesForPath:path; // List of attribute names
- (NSData *)valueOfExtendedAttribute:(NSString *)name forPath:(NSString *)path;


#pragma mark Advanced Icons

- (NSData *)iconDataForPath:(NSString *)path;
- (GTResourceFork *)customIconResourceForkForPath:(NSString *)path;

@end

