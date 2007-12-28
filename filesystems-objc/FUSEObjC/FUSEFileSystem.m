//
//  FUSEFileSystem.m
//  FUSEObjC
//
//  Created by alcor on 12/6/06.
//  Copyright 2006 Google. All rights reserved.
//

#import "FUSEFileSystem.h"
#define FUSE_USE_VERSION 26
#include <fuse.h>

#include <string.h>
#include <errno.h>
#include <fcntl.h>
#import "GTResourceFork.h"
#import "IconFamily.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/mount.h>

NSString* const FUSEManagedVolumeIcon = @"FUSEManagedVolumeIcon";
NSString* const FUSEManagedDirectoryIconFile = @"FUSEManagedDirectoryIconFile";
NSString* const FUSEManagedDirectoryIconResource = @"FUSEManagedDirectoryIconResource";
NSString* const FUSEManagedFileResource = @"FUSEMangedFileResource";
NSString* const FUSEManagedDirectoryResource = @"FUSEManagedDirectoryResource";

#define VOLUMEICON @".VolumeIcon.icns"

#define kResourceForkXattr @"com.apple.ResourceFork"

@interface IconFamily (RawData)

@end

@implementation IconFamily (RawData)
- (NSData *)iconData {
  return [NSData dataWithBytes:*hIconFamily length:GetHandleSize((Handle)hIconFamily)];
}
@end


@interface NSString (UTF8Copy)
- (char *)copyUTF8String;
@end
@implementation NSString (UTF8Copy)
- (char *)copyUTF8String {
  const char *original = [self UTF8String];
  size_t len = strlen(original)+1;
  char *copy = malloc(len);
  strlcpy(copy, original, len);
  return copy;
}
@end

@interface NSString (UniquePath)
-(NSString *)firstUnusedFilePath;
@end

@implementation NSString (UniquePath)
-(NSString *)firstUnusedFilePath{
	NSString *basePath=[self stringByDeletingPathExtension];
	NSString *extension=[self pathExtension];
	NSString *alternatePath=self;
	int i;
	for (i=1;[[NSFileManager defaultManager] fileExistsAtPath:alternatePath]; i++)
		alternatePath=[NSString stringWithFormat:@"%@ %d.%@",basePath,i,extension];
	return alternatePath;
}

@end


@interface NSData (BufferOffset) 
- (int)getBytes:(char *)buf size:(size_t)size offset:(off_t)offset;
@end

@implementation NSData (BufferOffset) 
- (int)getBytes:(char *)buf size:(size_t)size offset:(off_t)offset {
  size_t len = [self length];
  if (offset + size > len)
    size = len - offset;
  
  if (offset > len) {
    NSLog(@"read too many bytes %d > %d", offset, len);
    return 0;
  }
  
  NSRange range = NSMakeRange(offset, size);
  [self getBytes:buf range:range];
  return size;
}
@end

#pragma mark -

@interface FUSEFileSystem (FUSEFileSystemPrivate)
- (NSArray *)fullDirectoryContentsAtPath:(NSString *)path;
- (void)startFuse;
- (void)stopFuse;
@end

@interface FUSEFileSystem (FuseFunctions)

//- (void)fuseInit;
//- (void)fuseDestroy;
//- (BOOL)shouldStartFuse;
//- (NSString *)mountName;
//- (NSString *)mountPoint;

@end



@implementation FUSEFileSystem
+ (void)initialize {
  NSDictionary *defaults = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"NSUserDefaults"];
  if (defaults)
    [[NSUserDefaults standardUserDefaults] registerDefaults:defaults];
}

- (id)init {
  // FUSEFileSystem automatically converts into the desired subclass
  if ([self isMemberOfClass:[FUSEFileSystem class]]) {
    [self release];
    NSString *fuseClassName =
      [[NSBundle mainBundle] objectForInfoDictionaryKey:@"FUSEFileSystemClass"];
    Class fuseClass = NSClassFromString(fuseClassName);
    if (fuseClass && fuseClass != [FUSEFileSystem class]) {
      
      self = [[fuseClass alloc] init];
      isMounted_ = FALSE;
    } else {
      NSLog(@"Could not find class: %@", fuseClassName);
      return nil;
    }
  }
  return self;
}

+ (FUSEFileSystem *)currentFS {
  struct fuse_context* context = fuse_get_context();
  assert(context);
  return (FUSEFileSystem *)context->private_data;
}

#pragma mark Configuration

- (NSString *)mountName {
  NSString *mountName = [[[NSUserDefaults standardUserDefaults] stringForKey:@"FUSEMountName"] retain];
  if (!mountName) mountName = NSStringFromClass([self class]);
  return mountName;
}

- (NSString *)mountPoint {
  if (!mountPoint_) {
    mountPoint_ =  [[NSUserDefaults standardUserDefaults] stringForKey:@"FUSEMountPath"];
    // mountPoint_ = [mountPoint_ firstUnusedFilePath];
    [mountPoint_ retain];
  }
  return [[mountPoint_ copy] autorelease];
}

- (BOOL)isForeground {
  return YES;
}

- (BOOL)isThreadSafe {
  return NO;
}

- (NSArray *)fuseOptions {
  return [NSArray array];
}

#pragma mark Lifecycle

- (BOOL)shouldStartFuse {
  return YES;
}

- (void)fuseWillMount {}
- (void)fuseDidMount {}

- (void)fuseWillUnmount{}
- (void)fuseDidUnmount{}

#pragma mark Resource Forks and HFS headers

- (BOOL)usesResourceForks{
  return NO;
}

- (NSURL *)URLContentOfWeblocAtPath:(NSString *)path {
  return nil;
}


- (NSString *)resourcePathForPath:(NSString *)path {
  NSString *name = [path lastPathComponent];
  path = [path stringByDeletingLastPathComponent];
  name = [@"._" stringByAppendingString:name];
  path = [path stringByAppendingPathComponent:name];
  return path;
}


- (NSData *)resourceHeaderForPath:(NSString *)path 
                         withResourceSize:(UInt32)size 
                                    flags:(UInt16)flags {
  char header[82] = {
    0x00, 0x05, 0x16, 0x07, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00,
    0x00, 0x32, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x52, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00};
  size = EndianU32_NtoB(size);
  
  FileInfo info;
  bzero(&info, sizeof(FileInfo));
  info.finderFlags = flags;
  info.finderFlags = EndianU16_NtoB(info.finderFlags);
  //info.fileType = EndianU32_NtoB('test');
  //info.fileCreator = EndianU32_NtoB('test');
  
  
  memcpy(header + 0x2E, &size, sizeof(UInt32));
  memcpy(header + 0x32, &info, sizeof(FileInfo));
  return [NSData dataWithBytes:&header length:82];
}


- (NSData *)resourceForkContentsForPath:(NSString *)path {
  NSData *data = nil;
  @synchronized(self) {
    GTResourceFork *fork = [self resourceForkForPath:path];
    [fork write];
    data =  [fork dataRepresentation];
  }
  return data;
}

- (GTResourceFork *)resourceForkForPath:(NSString *)path {
  GTResourceFork *fork = [self customIconResourceForkForPath:path];
  
  if ([path hasSuffix:@".webloc"]) {
    NSURL *url = [self URLContentOfWeblocAtPath:path];
    if (url) {
      
      if (!fork) fork = [[[GTResourceFork alloc] init] autorelease];
      
      NSString *urlString = [url absoluteString];
      
      [fork setData:[urlString dataUsingEncoding:NSUTF8StringEncoding]
        forResource:256
             ofType:'url '];
    }
    
  }
  return fork;
}

- (BOOL)pathHasResourceFork:(NSString *)path {
  if ([self iconDataForPath:path]) return YES;
  return [self resourceForkContentsForPath:path] != nil;
}

- (NSData *)resourceHeaderAndForkForPath:(NSString *)path
                         includeResource:(BOOL)includeResource
                                   flags:(UInt16)flags {
  if (![self usesResourceForks]) return nil;
  
  NSData *resourceFork = includeResource ? [self resourceForkContentsForPath:path] : nil;
  NSData *headerData = [self resourceHeaderForPath:path withResourceSize:[resourceFork length] flags:flags];

  NSMutableData *data = [NSMutableData data];
  [data appendData:headerData];
  
  if (resourceFork)
    [data appendData:resourceFork];
  
  return data;
}

#pragma mark Icons

+ (NSData *)iconDataForImage:(NSImage *)image {
  NSData* data = nil;
  if (image != nil) {
    IconFamily *family = [IconFamily iconFamilyWithThumbnailsOfImage:image];
    data = [family iconData];
  }
  return data;
}

- (NSData *)iconDataForPath:(NSString *)path {  
  NSString *iconPath = [self iconFileForPath:path];
  if (iconPath) {
    return [NSData dataWithContentsOfFile:iconPath];
  }
  return [FUSEFileSystem iconDataForImage:[self iconForPath:path]];
}

- (NSString *)iconFileForPath:(NSString *)path {
    if ([path isEqualToString:@"/"]) {
      return [[NSBundle mainBundle] pathForResource:@"Fuse" ofType:@"icns"];
    } 
    return nil;
}

- (NSImage *)iconForPath:(NSString *)path {
  return nil;
}

- (GTResourceFork *)customIconResourceForkForPath:(NSString *)path {
  NSData *imageData = [self iconDataForPath:path];
  if (imageData) {
    GTResourceFork *fork = [[[GTResourceFork alloc] init] autorelease];
    [fork setData:imageData
      forResource:-16455
           ofType:'icns'];
    return fork;
  }
  return nil;
}

#pragma mark Advanced File Operations

- (BOOL)fillStatvfsBuffer:(struct statvfs *)stbuf 
                  forPath:(NSString *)path 
                    error:(NSError **)error {
  NSMutableDictionary* attributes = [NSMutableDictionary dictionary];
  NSNumber* defaultSize = [NSNumber numberWithLongLong:(2LL * 1024 * 1024 * 1024)];
  [attributes setObject:defaultSize forKey:NSFileSystemSize];
  [attributes setObject:defaultSize forKey:NSFileSystemFreeSize];
  [attributes setObject:defaultSize forKey:NSFileSystemNodes];
  [attributes setObject:defaultSize forKey:NSFileSystemFreeNodes];
  // TODO: NSFileSystemNumber? Or does fuse do that for us?
  
  // A subclass can override any of the above defaults by implementing the
  // attributesOfFileSystemForPath selector and returning a custom dictionary.
  *error = nil;
  NSDictionary *customAttribs = [self attributesOfFileSystemForPath:path 
                                                              error:error];
  if (!customAttribs) {
    if (!(*error)) {
      *error = [FUSEFileSystem errorWithCode:ENODEV];
    }
    return NO;
  }
  [attributes addEntriesFromDictionary:customAttribs];
  
  // Maximum length of filenames
  // TODO: Create our own key so that a fileSystem can override this.
  stbuf->f_namemax = 255;
  
  // Block size
  // TODO: Create our own key so that a fileSystem can override this.
  stbuf->f_bsize = stbuf->f_frsize = 4096;
  
  // Size in blocks
  NSNumber* size = [attributes objectForKey:NSFileSystemSize];
  assert(size);
  stbuf->f_blocks = (fsblkcnt_t)([size longLongValue] / stbuf->f_frsize);
  
  // Number of free / available blocks
  NSNumber* freeSize = [attributes objectForKey:NSFileSystemFreeSize];
  assert(freeSize);
  stbuf->f_bfree = stbuf->f_bavail = 
    (fsblkcnt_t)([freeSize longLongValue] / stbuf->f_frsize);
  
  // Number of nodes
  NSNumber* numNodes = [attributes objectForKey:NSFileSystemNodes];
  assert(numNodes);
  stbuf->f_files = (fsfilcnt_t)[numNodes longLongValue];
  
  // Number of free / available nodes
  NSNumber* freeNodes = [attributes objectForKey:NSFileSystemFreeNodes];
  assert(freeNodes);
  stbuf->f_ffree = stbuf->f_favail = (fsfilcnt_t)[freeNodes longLongValue];
  
  return YES;
}

// Determines whether the given path is a for a resource managed by 
// FUSEFileSystem, such as a custom icon for a file. The optional
// "type" param is set to the type of the managed resource. The optional
// "dataPath" param is set to the file that represents this resource. For
// example, for a custom icon resource fork, this would be the corresponding 
// data fork. For a custom directory icon, this would be the directory itself.
- (BOOL)isManagedResourceAtPath:(NSString *)path
                           type:(NSString **)type
                      dataPath:(NSString **)dataPath {
  if (![self usesResourceForks]) {
    return NO;
  }
  NSString* parentDir = [path stringByDeletingLastPathComponent];
  NSString* name = [path lastPathComponent];
  if ([name isEqualToString:VOLUMEICON]) {
    if (type) {
      *type = FUSEManagedVolumeIcon;
    }
    if (dataPath) {
      *dataPath = parentDir;
    }
    return YES;
  } else if ([name isEqualToString:@"Icon\r"]) {
    if (type) {
      *type = FUSEManagedDirectoryIconFile;
    }
    if (dataPath) {
      *dataPath = parentDir;
    }
    return YES;
  } else if ([name isEqualToString:@"._Icon\r"]) {
    if (type) {
      *type = FUSEManagedDirectoryIconResource;
    }
    if (dataPath) {
      *dataPath = parentDir;
    }
    return YES;
  } else if ([name hasPrefix:@"._"]) {
    if (type || dataPath) {
      // Since this is a request for a resource fork, we fix up the path to 
      // refer the data fork.
      name = [name substringFromIndex:2];
      NSString* dp = [parentDir stringByAppendingPathComponent:name];
      if (type) {
        BOOL isDirectory = NO; // Default to NO
        [self fileExistsAtPath:dp isDirectory:&isDirectory];
        if (isDirectory) {
          *type = FUSEManagedDirectoryResource;
        } else {
          *type = FUSEManagedFileResource;
        }
      }
      if (dataPath) {
        *dataPath = dp;
      }
    }
    return YES;
  }
  return NO;
}

- (BOOL)fillStatBuffer:(struct stat *)stbuf 
               forPath:(NSString *)path 
                 error:(NSError **)error {
  // TODO: support passthrough paths for all reads/writes/stats
  NSString *realPath = [self passthroughPathForPath:path];
  if (realPath) {
    return (lstat([realPath fileSystemRepresentation], stbuf) == 0);
  }

  NSString* dataPath = path;  // Default to the given path.
  BOOL isManagedResource = 
    [self isManagedResourceAtPath:path type:nil dataPath:&dataPath];

  BOOL isDirectory = NO;
  BOOL exists = [self fileExistsAtPath:dataPath isDirectory:&isDirectory];
  if (!exists) return NO;
  if (isManagedResource) {
    // If this is a managed "resource", then it can't be directory even if its
    // representative dataPath is a directory.
    isDirectory = NO;
  }
  
  NSMutableDictionary* attributes = [NSMutableDictionary dictionary];
  [attributes setObject:[NSNumber numberWithLong:0555] 
                 forKey:NSFilePosixPermissions];
  [attributes setObject:[NSNumber numberWithLong:1]
                 forKey:NSFileReferenceCount];    // 1 means "don't know"
  [attributes setObject:(isDirectory ? NSFileTypeDirectory : NSFileTypeRegular)
                 forKey:NSFileType];

  // A subclass an override any of the above defaults by implementing the
  // attributesOfItemAtPath: selector and returning a custom dictionary.
  *error = nil;
  NSDictionary *customAttribs = [self attributesOfItemAtPath:dataPath error:error];
  if (!customAttribs) {
    if (!(*error)) {
      *error = [FUSEFileSystem errorWithCode:ENODEV];
    }
    return NO;
  }
  [attributes addEntriesFromDictionary:customAttribs];
  
  if (isManagedResource) {
    // If this is a request for a "special" file, then we'll need to force manual
    // calculation of the size by converting to the proper resource format.
    [attributes removeObjectForKey:NSFileSize];
  }
  
  // Permissions (mode)
  NSNumber* perm = [attributes objectForKey:NSFilePosixPermissions];
  stbuf->st_mode = [perm longValue];
  NSString* fileType = [attributes objectForKey:NSFileType];
  if ([fileType isEqualToString:NSFileTypeDirectory ]) {
    stbuf->st_mode |= S_IFDIR;
  } else if ([fileType isEqualToString:NSFileTypeRegular]) {
    stbuf->st_mode |= S_IFREG;
  } else if ([fileType isEqualToString:NSFileTypeSymbolicLink]) {
    stbuf->st_mode |= S_IFLNK;
  } else {
    *error = [FUSEFileSystem errorWithCode:EFTYPE];
    NSLog(@"Illegal file type: '%@' at path '%@'", fileType, path);
    return NO;
  }
  
  // Owner and Group
  // Note that if the owner or group IDs are not specified, the effective
  // user and group IDs for the current process are used as defaults.
  NSNumber* uid = [attributes objectForKey:NSFileOwnerAccountID];
  NSNumber* gid = [attributes objectForKey:NSFileGroupOwnerAccountID];
  stbuf->st_uid = uid ? [uid longValue] : geteuid();
  stbuf->st_gid = gid ? [gid longValue] : getegid();

  // nlink
  NSNumber* nlink = [attributes objectForKey:NSFileReferenceCount];
  stbuf->st_nlink = [nlink longValue];
      
  // TODO: For the timespec, there is a .tv_nsec (= nanosecond) part as well.
  // Since the NSDate returns a double, we can fill this in as well.

  // mtime, atime
  NSDate* mdate = [attributes objectForKey:NSFileModificationDate];
  if (mdate) {
    time_t t = (time_t) [mdate timeIntervalSince1970];
    stbuf->st_mtimespec.tv_sec = t;
    stbuf->st_atimespec.tv_sec = t;
  }

  // ctime  TODO: ctime is not "creation time" rather it's the last time the 
  // inode was changed.  mtime would probably be a closer approximation.
  NSDate* cdate = [attributes objectForKey:NSFileCreationDate];
  if (cdate) {
    stbuf->st_ctimespec.tv_sec = [cdate timeIntervalSince1970];
  }

  // Size for regular files.
  if (!isDirectory) {
    NSNumber* size = [attributes objectForKey:NSFileSize];
    if (size) {
      stbuf->st_size = [size longLongValue];
    } else {
      // NOTE: We use the given path here, since managedContentsForPath will
      // handle managed resources and return the proper data.
      NSData* data = [self managedContentsForPath:path];
      if (data) {
        stbuf->st_size = [data length];
      } else {
        stbuf->st_size = 0;
      }
    }
  }

  // Set the number of blocks used so that Finder will display size on disk 
  // properly. The man page says that this is in terms of 512 byte blocks.
  if (stbuf->st_size > 0) {
    stbuf->st_blocks = stbuf->st_size / 512;
    if (stbuf->st_size % 512) {
      ++(stbuf->st_blocks);
    }
  }

  return YES;  
}


#pragma mark Internal Reading?

- (UInt16)finderFlagsForPath:(NSString *)path {
  if ([self iconDataForPath:path]) {
      return kHasCustomIcon;
  }
  return 0;
}

- (NSData *)managedContentsForPath:(NSString *)path {
  
  NSString *realPath = [self passthroughPathForPath:path];
  if (realPath) {
    return ([[NSFileManager defaultManager] contentsAtPath:realPath]);
  }

  NSString* dataPath = path;  // Default to the given path.
  NSString* type = nil;
  BOOL isManagedResource = 
    [self isManagedResourceAtPath:path type:&type dataPath:&dataPath];
  if (!isManagedResource) {
    return [self contentsAtPath:path];  // Whatever the subclass would return.
  }
  
  if ([type isEqualToString:FUSEManagedVolumeIcon]) {
    return [self iconDataForPath:dataPath];  // Just return the icon directly.
  } else if ([type isEqualToString:FUSEManagedDirectoryIconFile]) {
    return nil;  // The Icon\r file contains no data.
  }
  
  int flags = [self finderFlagsForPath:dataPath];
  BOOL includeResource = YES;
  if ([type isEqualToString:FUSEManagedDirectoryIconResource]) {
    flags |= kIsInvisible;
    includeResource = YES;
  } else if ([type isEqualToString:FUSEManagedFileResource]) {
    includeResource = YES;
  } else if ([type isEqualToString:FUSEManagedDirectoryResource]) {
    includeResource = NO;
  } else {
    NSLog(@"Unknown managed file type: %@", type);
    return nil;
  }
  return [self resourceHeaderAndForkForPath:dataPath
                 includeResource:includeResource
                           flags:flags];
}

// Directory contents with invisible resources added
- (NSArray *)fullDirectoryContentsAtPath:(NSString *)path error:(NSError **)error {
  NSArray *contents = [self contentsOfDirectoryAtPath:path error:error];
  if (contents == nil) {
    return nil;
  }
  
  NSMutableArray *fullContents = [NSMutableArray array];
  [fullContents addObject:@"."];
  [fullContents addObject:@".."];
  [fullContents addObjectsFromArray:contents];
  
  if ([self usesResourceForks]) {
    for (int i = 0, count = [contents count]; i < count; i++) {
    NSString *childPath = [contents objectAtIndex:i];
      if ([self pathHasResourceFork:[path stringByAppendingPathComponent:childPath]]) {
        [fullContents addObject:[@"._" stringByAppendingString:childPath]];
      }
    }
    
    if ([self iconDataForPath:path]) {
      if ([path isEqualToString:@"/"]) {
        [fullContents addObject:VOLUMEICON];
      } else { 
        [fullContents addObject:@"Icon\r"];
        [fullContents addObject:@"._Icon\r"];
      }
    }
  }
  return fullContents;
}

#pragma mark Moving an Item

- (BOOL)moveItemAtPath:(NSString *)source 
                toPath:(NSString *)destination
                 error:(NSError **)error {
  *error = [FUSEFileSystem errorWithCode:EACCES];
  return NO;
}

#pragma mark Removing an Item

- (BOOL)removeItemAtPath:(NSString *)path error:(NSError **)error {
  *error = [FUSEFileSystem errorWithCode:EACCES];
  return NO;
}

#pragma mark Creating an Item

- (BOOL)createDirectoryAtPath:(NSString *)path 
                   attributes:(NSDictionary *)attributes
                        error:(NSError **)error {
  *error = [FUSEFileSystem errorWithCode:EACCES];
  return NO;
}

- (BOOL)createFileAtPath:(NSString *)path 
              attributes:(NSDictionary *)attributes
               outHandle:(id *)outHandle
                   error:(NSError **)error {
  *error = [FUSEFileSystem errorWithCode:EACCES];
  return NO;
}


#pragma mark Linking an Item

// TODO: fusefm version.
- (BOOL)linkItemAtPath:(NSString *)path
                toPath:(NSString *)otherPath
                 error:(NSError **)error {
  *error = [FUSEFileSystem errorWithCode:ENOTSUP];  // TODO: not in man page.
  return NO;
}

#pragma mark Symbolic Links

// TODO: The fusefm_ equivalent is not yet implemented.
- (BOOL)createSymbolicLinkAtPath:(NSString *)path 
             withDestinationPath:(NSString *)otherPath
                           error:(NSError **)error {
  *error = [FUSEFileSystem errorWithCode:ENOTSUP];  // TODO: not in man page.
  return NO; 
}

- (NSString *)destinationOfSymbolicLinkAtPath:(NSString *)path
                                        error:(NSError **)error {
  *error = [FUSEFileSystem errorWithCode:ENOENT];
  return nil;
}

#pragma mark File Contents

- (NSData *)contentsAtPath:(NSString *)path {
  return nil; 
}

- (BOOL)openFileAtPath:(NSString *)path 
                  mode:(int)mode
             outHandle:(id *)outHandle 
                 error:(NSError **)error {
  *outHandle = [[FUSEFileSystem currentFS] managedContentsForPath:path];
  if (*outHandle == nil) {
    *error = [FUSEFileSystem errorWithCode:ENOENT];
    return NO;
  }
  return YES;
}

- (void)releaseFileAtPath:(NSString *)path handle:(id)handle {
    // No implementation.
}

- (int)readFileAtPath:(NSString *)path 
               handle:(id)handle
               buffer:(char *)buffer 
                 size:(size_t)size 
               offset:(off_t)offset
                error:(NSError **)error {
  NSData* data = handle;
  if ( data == nil ) {
    *error = [FUSEFileSystem errorWithCode:EACCES];
    return -1;
  }
  return [data getBytes:buffer size:size offset:offset];
}

- (int)writeFileAtPath:(NSString *)path 
                handle:(id)handle 
                buffer:(const char *)buffer
                  size:(size_t)size 
                offset:(off_t)offset
                 error:(NSError **)error {
  *error = [FUSEFileSystem errorWithCode:EACCES];
  return -1; 
}

- (BOOL)truncateFileAtPath:(NSString *)path 
                    offset:(off_t)offset 
                     error:(NSError **)error {
  *error = [FUSEFileSystem errorWithCode:EACCES];
  return NO;
}

#pragma mark Directory Contents

- (BOOL)fileExistsAtPath:(NSString *)path isDirectory:(BOOL *)isDirectory{
  *isDirectory = [path isEqualToString:@"/"];
  return YES; 
}

- (NSArray *)contentsOfDirectoryAtPath:(NSString *)path error:(NSError **)error {
  *error = [FUSEFileSystem errorWithCode:ENOENT];
  return nil;
}

#pragma mark Getting and Setting Attributes

- (NSDictionary *)attributesOfItemAtPath:(NSString *)path 
                                   error:(NSError **)error {
  return [NSDictionary dictionary];
}

- (NSDictionary *)attributesOfFileSystemForPath:(NSString *)path
                                          error:(NSError **)error {
  return [NSDictionary dictionary];
}

- (BOOL)setAttributes:(NSDictionary *)attributes 
         ofItemAtPath:(NSString *)path
                error:(NSError **)error {
  *error = [FUSEFileSystem errorWithCode:ENODEV];
  return NO;
}

#pragma mark Extended Attributes

- (NSData *)valueOfExtendedAttribute:(NSString *)name forPath:(NSString *)path
                               error:(NSError **)error {
  *error = [FUSEFileSystem errorWithCode:ENOTSUP];
  return nil;
}

- (BOOL)setExtendedAttribute:(NSString *)name 
                     forPath:(NSString *)path 
                       value:(NSData *)value
                       flags:(int) flags
                       error:(NSError **)error {
  *error = [FUSEFileSystem errorWithCode:ENOTSUP];
  return NO;
}

- (NSArray *)extendedAttributesForPath:path error:(NSError **)error {
  *error = [FUSEFileSystem errorWithCode:ENOTSUP];
  return nil;
}

#pragma mark Passthrough
// TODO: support passthrough paths for all reads/writes/stats
- (NSString *)passthroughPathForPath:(NSString *)path {
  return nil;
}

#pragma mark Finder

- (void)showInFinder {
  [[NSWorkspace sharedWorkspace] selectFile:[self mountPoint]
                   inFileViewerRootedAtPath:[[self mountPoint] stringByDeletingLastPathComponent]];

}

#pragma mark FUSE Operations

static int fusefm_statfs(const char* path, struct statvfs* stbuf) {
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  int res = -ENOENT;
  memset(stbuf, 0, sizeof(struct statvfs));
  NSError* error = nil;
  FUSEFileSystem* fs = [FUSEFileSystem currentFS];
  if ([fs fillStatvfsBuffer:stbuf 
                    forPath:[NSString stringWithUTF8String:path]
                      error:&error]) {
    res = 0;
  } else {
    if (error != nil) {
      res = -[error code];
    }
  }
  [pool release];
  return res;
}

static int fusefm_getattr(const char *path, struct stat *stbuf) {
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  int res = -ENOENT;
  memset(stbuf, 0, sizeof(struct stat));
  NSError* error = nil;
  FUSEFileSystem* fs = [FUSEFileSystem currentFS];
  if ([fs fillStatBuffer:stbuf 
                 forPath:[NSString stringWithUTF8String:path]
                   error:&error]) {
    res = 0;
  } else {
    if (error != nil) {
      res = -[error code];
    }
  }
  [pool release];
  return res;
}

static int fusefm_fgetattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
  // TODO: This is a quick hack to get fstat up and running.
  return fusefm_getattr(path, stbuf);
}

static int fusefm_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                          off_t offset, struct fuse_file_info *fi) {
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  int res = -ENOENT;

  NSError* error = nil;
  FUSEFileSystem* fs = [FUSEFileSystem currentFS];
  NSArray *contents = [fs fullDirectoryContentsAtPath:[NSString stringWithUTF8String:path] 
                                                error:&error];
  if (contents) {
    res = 0;
    for (int i = 0, count = [contents count]; i < count; i++) {
      filler(buf, [[contents objectAtIndex:i] UTF8String], NULL, 0);
    }
  } else {
    if (error != nil) {
      res = -[error code];
    }
  }
  [pool release];
  return res;
}

static int fusefm_create(const char* path, mode_t mode, struct fuse_file_info* fi) {
  int res = -EACCES;
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
  @try {
    NSError* error = nil;
    id object = nil;
    NSMutableDictionary* attribs = [NSMutableDictionary dictionary];
    [attribs setObject:[NSNumber numberWithLong:mode] forKey:NSFilePosixPermissions];
    FUSEFileSystem* fs = [FUSEFileSystem currentFS];
    if ([fs createFileAtPath:[NSString stringWithUTF8String:path]
                  attributes:attribs
                   outHandle:&object
                       error:&error]) {
      res = 0;
      if (object != nil) {
        fi->fh = (uint64_t)(int)[object retain];
      }
    } else {
      if (error != nil) {
        res = -[error code];
      }
    }
  }
  @catch (NSException * e) {
  }
  [pool release];
  return res;
}

static int fusefm_open(const char *path, struct fuse_file_info *fi) {
  int res = -ENOENT;  // TODO: Default to 0 (success) since a file-system does
                      // not necessarily need to implement open.
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  @try {
    id object = nil;
    NSError* error = nil;
    FUSEFileSystem* fs = [FUSEFileSystem currentFS];
    if ([fs openFileAtPath:[NSString stringWithUTF8String:path]
                      mode:fi->flags
                 outHandle:&object
                     error:&error]) {
      res = 0;
      if (object != nil) {
        fi->fh = (uint64_t)(int)[object retain];
      }
    } else {
      if (error != nil) {
        res = -[error code];
      }
    }
  }
  @catch (NSException * e) {
  }
  [pool release];
  return res;
}


static int fusefm_release(const char *path, struct fuse_file_info *fi) {
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  id object = (id)(int)fi->fh;
  FUSEFileSystem* fs = [FUSEFileSystem currentFS];
  [fs releaseFileAtPath:[NSString stringWithUTF8String:path] handle:object];
  if (object) {
    [object release]; 
  }
  [pool release];
  return 0;
}

static int fusefm_truncate(const char* path, off_t offset) {
  int res = -ENOTSUP;
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];

  NSError* error = nil;
  FUSEFileSystem* fs = [FUSEFileSystem currentFS];
  if ([fs truncateFileAtPath:[NSString stringWithUTF8String:path]
                      offset:offset
                       error:&error]) {
    res = 0;
  } else {
    if (error != nil) {
      res = -[error code];
    }
  }
  
  [pool release];
  return res;
}

static int fusefm_ftruncate(const char* path, off_t offset, struct fuse_file_info *fh) {
  return fusefm_truncate(path, offset);
}

static int fusefm_chown(const char* path, uid_t uid, gid_t gid) {
  int res = 0;  // Return success by default.
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];

  NSMutableDictionary* attribs = [NSMutableDictionary dictionary];
  [attribs setObject:[NSNumber numberWithLong:uid] forKey:NSFileOwnerAccountID];
  [attribs setObject:[NSNumber numberWithLong:gid] forKey:NSFileGroupOwnerAccountID];
  NSError* error = nil;
  FUSEFileSystem* fs = [FUSEFileSystem currentFS];
  if ([fs setAttributes:attribs 
           ofItemAtPath:[NSString stringWithUTF8String:path]
                  error:&error]) {
    res = 0;
  } else {
    if (error != nil) {
      res = -[error code];
    }
  }
  [pool release];

  return res;
}

static int fusefm_chmod(const char* path, mode_t mode) {
  int res = 0;  // Return success by default.
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
  
  NSMutableDictionary* attribs = [NSMutableDictionary dictionary];
  [attribs setObject:[NSNumber numberWithLong:mode] forKey:NSFilePosixPermissions];
  NSError* error = nil;
  FUSEFileSystem* fs = [FUSEFileSystem currentFS];
  if ([fs setAttributes:attribs 
           ofItemAtPath:[NSString stringWithUTF8String:path]
                  error:&error]) {
    res = 0;
  } else {
    if (error != nil) {
      res = -[error code];
    }
  }
  [pool release];
  
  return res;
}

int fusefm_utimens(const char* path, const struct timespec tv[2]) {
  int res = 0;  // Return success by default.
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
  
  NSMutableDictionary* attribs = [NSMutableDictionary dictionary];
  NSDate* modification = 
  [NSDate dateWithTimeIntervalSince1970:tv[1].tv_sec];
  
  [attribs setObject:modification forKey:NSFileModificationDate];
  NSError* error = nil;
  FUSEFileSystem* fs = [FUSEFileSystem currentFS];
  if ([fs setAttributes:attribs 
           ofItemAtPath:[NSString stringWithUTF8String:path]
                  error:&error]) {
    res = 0;
  } else {
    if (error != nil) {
      res = -[error code];
    }
  }
  [pool release];
  
  return res;
}

static int fusefm_fsync(const char* path, int isdatasync,
                        struct fuse_file_info* fi) {
  return 0;
}

static int fusefm_write(const char* path, const char* buf, size_t size, 
                        off_t offset, struct fuse_file_info* fi) {
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];

  NSError* error = nil;
  FUSEFileSystem* fs = [FUSEFileSystem currentFS];
  int length = [fs writeFileAtPath:[NSString stringWithUTF8String:path]
                            handle:(id)(int)fi->fh
                            buffer:buf
                              size:size
                            offset:offset
                             error:&error];
  if ( error != nil) {
    length = -[error code];
  }

  [pool release];
  return length;
}

static int fusefm_read(const char *path, char *buf, size_t size, off_t offset,
                       struct fuse_file_info *fi) {
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

  NSError* error = nil;
  FUSEFileSystem* fs = [FUSEFileSystem currentFS];
  int length = [fs readFileAtPath:[NSString stringWithUTF8String:path]
                           handle:(id)(int)fi->fh
                           buffer:buf
                             size:size
                           offset:offset
                              error:&error];
  if ( error != nil) {
    length = -[error code];
  }
  
  [pool release];
  return length;
}

static int fusefm_readlink(const char *path, char *buf, size_t size)
{
  int res = -ENOENT;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  NSString* linkPath = [NSString stringWithUTF8String:path];
  NSError* error = nil;
  FUSEFileSystem* fs = [FUSEFileSystem currentFS];
  NSString *pathContent = [fs destinationOfSymbolicLinkAtPath:linkPath
                                                        error:&error];
  if (pathContent != nil) {
    res = 0;
    [pathContent getFileSystemRepresentation:buf maxLength:size];
  } else {
    if (error != nil) {
      res = -[error code];
    }
  }
  [pool release];
  return 0;
}

static int fusefm_getxattr(const char *path, const char *name, char *value,
                           size_t size) {
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  int res = -ENOATTR;
  @try {
    NSError* error = nil;
    FUSEFileSystem* fs = [FUSEFileSystem currentFS];
    NSData *data = [fs valueOfExtendedAttribute:[NSString stringWithUTF8String:name]
                                        forPath:[NSString stringWithUTF8String:path]
                                          error:&error];
    if (data != nil) {
      res = [data length];  // default to returning size of buffer.
      if (value) {
        if (size > [data length]) {
          size = [data length];
        }
        [data getBytes:value length:size];
        res = size;  // bytes read
      }
    } else if (error != nil) {
      res = -[error code];
    }
  }
  @catch (NSException * e) {
    res = -ENOTSUP;
  }
  [pool release];
  return res;
}

static int fusefm_setxattr(const char *path, const char *name, const char *value,
                           size_t size, int flags) {
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  int res = -EPERM;
  @try {
    NSError* error = nil;
    FUSEFileSystem* fs = [FUSEFileSystem currentFS];
    if ([fs setExtendedAttribute:[NSString stringWithUTF8String:name]
                         forPath:[NSString stringWithUTF8String:path]
                           value:[NSData dataWithBytes:value length:size]
                           flags:flags
                           error:&error]) {
      res = 0;
    } else {
      if ( error != nil ) {
        res = -[error code];
      }
    }
  }
  @catch (NSException * e) {
    res = -ENOTSUP;
  }
  [pool release];
  return res;
}

static int fusefm_listxattr(const char *path, char *list, size_t size)
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  int res = -ENOTSUP;
  @try {
    NSError* error = nil;
    FUSEFileSystem* fs = [FUSEFileSystem currentFS];
    NSArray *attributeNames =
      [fs extendedAttributesForPath:[NSString stringWithUTF8String:path]
                              error:&error];
    if ( attributeNames != nil ) {
      char zero = 0;
      NSMutableData *data = [NSMutableData dataWithCapacity:size];  
      for (int i = 0, count = [attributeNames count]; i < count; i++) {
        [data appendData:[[attributeNames objectAtIndex:i] dataUsingEncoding:NSUTF8StringEncoding]];
        [data appendBytes:&zero length:1];
      }
      res = [data length];  // default to returning size of buffer.
      if (list) {
        if (size > [data length]) {
          size = [data length];
        }
        [data getBytes:list length:size];
      }
    } else if (error != nil) {
      res = -[error code];
    }
  }
  @catch (NSException * e) {
    res = -ENOTSUP;
  }

  [pool release];
  return res;
}

static int fusefm_rename(const char* path, const char* toPath) {
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  int ret = -EACCES;

  NSString* source = [NSString stringWithUTF8String:path];
  NSString* destination = [NSString stringWithUTF8String:toPath];
  NSError* error = nil;
  FUSEFileSystem* fs = [FUSEFileSystem currentFS];
  if ([fs moveItemAtPath:source toPath:destination error:&error]) {
    ret = 0;  // Success!
  } else {
    if (error != nil) {
      ret = -[error code];
    }
  }
  [pool release];
  return ret;  
}

static int fusefm_mkdir(const char* path, mode_t mode) {
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  int ret = -EACCES;

  NSError* error = nil;
  // TODO: Create proper attributes dictionary from mode_t.
  FUSEFileSystem* fs = [FUSEFileSystem currentFS];
  if ([fs createDirectoryAtPath:[NSString stringWithUTF8String:path] 
                     attributes:nil
                          error:(NSError **)error]) {
    ret = 0;  // Success!
  } else {
    if (error != nil) {
      ret = -[error code];
    }
  }
  [pool release];
  return ret;
}

static int fusefm_unlink(const char* path) {
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  int ret = -EACCES;
  NSError* error = nil;
  FUSEFileSystem* fs = [FUSEFileSystem currentFS];
  if ([fs removeItemAtPath:[NSString stringWithUTF8String:path] 
                     error:&error]) {
    ret = 0;  // Success!
  } else {
    if (error != nil) {
      ret = -[error code];
    }
  }
  [pool release];
  return ret;
}

static int fusefm_rmdir(const char* path) {
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  int ret = -EACCES;
  
  NSError* error = nil;
  FUSEFileSystem* fs = [FUSEFileSystem currentFS];
  if ([fs removeItemAtPath:[NSString stringWithUTF8String:path] 
                     error:&error]) {
    ret = 0;  // Success!
  } else {
    if (error != nil) {
      ret = -[error code];
    }
  }
  [pool release];
  return ret;
}

static void *fusefm_init(struct fuse_conn_info *conn) {
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

  FUSEFileSystem* fs = [FUSEFileSystem currentFS];
  [fs retain];
  [fs fuseInit];

  [pool release];
  return fs;
}

static void fusefm_destroy(void *private_data) {
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];

  FUSEFileSystem* fs = (FUSEFileSystem *)private_data;
  [fs fuseDestroy];
  [fs release];

  [pool release];
}

static struct fuse_operations fusefm_oper = {
  .init = fusefm_init,
  .destroy = fusefm_destroy,
  .statfs = fusefm_statfs,
  .getattr	= fusefm_getattr,
  .fgetattr = fusefm_fgetattr,
  .readdir	= fusefm_readdir,
  .open	= fusefm_open,
  .release	= fusefm_release,
  .read	= fusefm_read,
  .readlink	= fusefm_readlink,
  .write = fusefm_write,
  .create = fusefm_create,
  .getxattr	= fusefm_getxattr,
  .setxattr = fusefm_setxattr,
  .listxattr	= fusefm_listxattr,
  .mkdir = fusefm_mkdir,
  .unlink = fusefm_unlink,
  .rmdir = fusefm_rmdir,
  .rename = fusefm_rename,
  .truncate = fusefm_truncate,
  .ftruncate = fusefm_ftruncate,
  .chown = fusefm_chown,
  .chmod = fusefm_chmod,
  .utimens = fusefm_utimens,
  .fsync = fusefm_fsync,
};

// TODO: Better name for below mark
#pragma mark Internal Lifecycle?

- (void)fuseInit {    
  isMounted_ = YES;
  [self fuseDidMount];
}

- (void)fuseDestroy {
  isMounted_ = NO;
  [self fuseDidUnmount];
  [[NSApplication sharedApplication] terminate:[FUSEFileSystem currentFS]]; 
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
  if (![self shouldStartFuse]) return;
  [NSThread detachNewThreadSelector:@selector(startFuse) toTarget:self withObject:nil];
}

#if 0  // TODO: We should probably switch to just "open /".  
- (BOOL)applicationShouldHandleReopen:(NSApplication *)sender hasVisibleWindows:(BOOL)flag {
  NSLog(@"calling [self showInFinder]");
  if (isMounted_) {
    [self showInFinder];
  }
  return NO;
}
#endif

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
  [self stopFuse];
  return NSTerminateNow;
}

- (void)startFuse {
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

  // Trigger initialization of NSFileManager. This is rather lame, but if we
  // don't call directoryContents before we mount our FUSE filesystem and 
  // the filesystem uses NSFileManager we may deadlock. It seems that the
  // NSFileManager class will do lazy init and will query all mounted
  // filesystems. This leads to deadlock when we re-enter our mounted fuse fs. 
  // Once initialized it seems to work fine.
  [[NSFileManager defaultManager] directoryContentsAtPath:@"/Volumes"];
  
  NSString *mountPath = [self mountPoint];
  
  if (![mountPath length]){
    NSLog(@"No mount point specified");
    [pool release];
    return;
  } else {
    NSLog(@"mounting on: %@", mountPath);
  }
  
  // Create mount path
  NSFileManager *fileManager = [NSFileManager defaultManager];
  [fileManager createDirectoryAtPath:mountPath attributes:nil];
  
  // Create mount header
  NSData *volumeHeader = [self resourceHeaderAndForkForPath:@"/"
                                            includeResource:NO
                                                      flags:kHasCustomIcon];
  [volumeHeader writeToFile:[self resourcePathForPath:mountPath]
                 atomically:NO];
  
  
  NSMutableArray *arguments = [NSMutableArray arrayWithObjects:
    [[NSBundle mainBundle] executablePath],
    [NSString stringWithFormat:@"-ovolname=%@",[self mountName]],
    nil];
  if ([self isForeground]) {
    [arguments addObject:@"-f"];  // Forground rather than daemonize.
  }
  if (![self isThreadSafe]) {
    [arguments addObject:@"-s"];  // Force single-threaded mode.
  }
  NSArray* options = [self fuseOptions];
  for (int i = 0; i < [options count]; ++i) {
    NSString* option = [options objectAtIndex:i];
    if ([option length] > 0) {
      [arguments addObject:[NSString stringWithFormat:@"-o%@",option]];
    }
  }
  [arguments addObject:mountPath];

  // Start Fuse Main
  const char *argv[[arguments count]];
  
  for (int i = 0, count = [arguments count]; i < count; i++) {
    argv[i] = [[arguments objectAtIndex:i] copyUTF8String];
  }
  
  argv[0] = strcpy(malloc(strlen(argv[0]+1)), argv[0]);
  
  int argc = sizeof(argv) / sizeof(argv[0]);
  [self fuseWillMount];
  [pool release];
  fuse_main(argc, (char **)argv, &fusefm_oper, self); 
}

- (void)stopFuse {
  // Remove volume header file
  NSFileManager *fileManager = [NSFileManager defaultManager];
  [fileManager removeFileAtPath:[self resourcePathForPath:[self mountPoint]]
                        handler:nil];
  [self fuseWillUnmount];
  // Unmount
  NSTask *unmountTask = [NSTask launchedTaskWithLaunchPath:@"/sbin/umount" arguments:[NSArray arrayWithObjects: @"-v", [self mountPoint], nil]];
  [unmountTask waitUntilExit];
}

#pragma mark Utility Methods

+ (NSError *)errorWithCode:(int)code {
  return [NSError errorWithDomain:NSPOSIXErrorDomain code:code userInfo:nil];
}

@end