/*
 * MacFUSE-Based procfs
 * Display Files
 *
 * Thanks to Daniel Waylonis <http://nekotech.com> for image processing code.
 */

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <Accelerate/Accelerate.h>
#include <IOKit/graphics/IOGraphicsLib.h>
#include <ApplicationServices/ApplicationServices.h>

extern "C" {

#include "procfs_displays.h"

#define MAX_DISPLAYS 8

static CGLContextObj CreateScreenGLContext(CGDirectDisplayID targetDisplay);
static void ReleaseScreenGLContext(CGLContextObj context);
static void CaptureScreenRectToBuffer(CGRect srcRect, vImage_Buffer *buffer);
static void *CreateFullScreenXRGB32(CGDirectDisplayID targetDisplay,
                                    int *width, int *height, int *rowBytes);
static CFMutableDataRef CreatePNGDataFromXRGB32Raster(void *raster,
                                                       int width, int height,
                                                       int bytesPerRow);

static CGLContextObj
CreateScreenGLContext(CGDirectDisplayID targetDisplay)
{
    CGOpenGLDisplayMask displayMask;
    CGLPixelFormatObj   pixelFormatObj;
    GLint               numPixelFormats;

    displayMask = CGDisplayIDToOpenGLDisplayMask(targetDisplay);

    CGLPixelFormatAttribute attribs[] = {
        kCGLPFAFullScreen,
        kCGLPFADisplayMask,
        (CGLPixelFormatAttribute)displayMask,
        (CGLPixelFormatAttribute)NULL
    };

    CGLContextObj context = NULL;
  
    /* Build a full-screen GL context */

    CGLChoosePixelFormat(attribs, &pixelFormatObj, &numPixelFormats);
    CGLCreateContext(pixelFormatObj, NULL, &context);
    CGLDestroyPixelFormat(pixelFormatObj);
    CGLSetCurrentContext(context);
    CGLSetFullScreen(context);

    return context;
}

static void
ReleaseScreenGLContext(CGLContextObj context)
{
    CGLSetCurrentContext(NULL);
    CGLClearDrawable(context);
    CGLDestroyContext(context);
}

static void
CaptureScreenRectToBuffer(CGRect srcRect, vImage_Buffer *buffer)
{
    /* Configure data packing and alignment for store operation. */
    /* 4-byte-aligned pixels packed. */

    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glPixelStorei(GL_PACK_ROW_LENGTH, buffer->rowBytes / 4);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
  
#if __BIG_ENDIAN__
    GLenum pixelDataType = GL_UNSIGNED_INT_8_8_8_8_REV;
#else
    GLenum pixelDataType = GL_UNSIGNED_INT_8_8_8_8;
#endif /* __BIG_ENDIAN__ */

    /*
     * Fetch data in XRGB format to our buffer (no alpha needed because we are
     * reading from the framebuffer, which has no alpha).
     */

    glReadPixels((GLint)srcRect.origin.x, (GLint)srcRect.origin.y,
                 (GLint)srcRect.size.width, (GLint)srcRect.size.height,
                 GL_BGRA, pixelDataType, buffer->data);
  
    /*
     * Vertically reflect in place (OpenGL uses inverted coordinate space,
     * so the image is upside down in memory with respect to what we want.
     */
    vImageVerticalReflect_ARGB8888(buffer, buffer, kvImageNoFlags);
}

static void *
CreateFullScreenXRGB32(CGDirectDisplayID targetDisplay,
                       int *width, int *height, int *rowBytes)
{
    GLint viewport[4];
    long  bytes;
    void *raster = NULL;

    CGLContextObj context = CreateScreenGLContext(targetDisplay);
    CGLSetCurrentContext(context);
    glReadBuffer(GL_FRONT);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glFinish();
  
    *width = viewport[2];
    *height = viewport[3];
    *rowBytes = (((*width) * 4) + 3) & ~3;
    bytes = (*rowBytes) * (*height);

    raster = malloc(bytes);
    if (!raster) {
        goto out;
    }
  
    vImage_Buffer buffer;
    buffer.data = raster;
    buffer.height = *height;
    buffer.width = *width;
    buffer.rowBytes = *rowBytes;
  
    CaptureScreenRectToBuffer(CGRectMake(0, 0, *width, *height), &buffer);

out:
    ReleaseScreenGLContext(context);
  
    return raster;
}

static CFMutableDataRef
CreatePNGDataFromXRGB32Raster(void *raster, int width, int height, 
                               int bytesPerRow)
{
    CGColorSpaceRef colorSpace;
    CGContextRef    context;
    CGImageRef      image = NULL;

    colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceGenericRGB);
    context = CGBitmapContextCreate(raster, width, height, 8, bytesPerRow,
                                    colorSpace, kCGImageAlphaNoneSkipFirst);
    CGColorSpaceRelease(colorSpace);

    if (context) {
        image = CGBitmapContextCreateImage(context);
        CGContextRelease(context);
    }
  
    if (!image) {
        return NULL;
    }
  
    CFMutableDataRef data = CFDataCreateMutable(kCFAllocatorDefault, 0);
    CGImageDestinationRef dest = 
        CGImageDestinationCreateWithData((CFMutableDataRef)data, kUTTypePNG,
                                         1, nil);
  
    if (dest) {
        CGImageDestinationAddImage(dest, image, nil);
        CGImageDestinationFinalize(dest);
        CFRelease(dest);
    }
  
    CGImageRelease(image);
  
    return data;
}

static CGDirectDisplayID
PROCFS_GetDisplayIDForDisplayAtIndex(unsigned int index)
{
    CGDisplayErr      dErr;
    CGDisplayCount    displayCount;
    CGDisplayCount    maxDisplays = MAX_DISPLAYS;
    CGDirectDisplayID onlineDisplays[MAX_DISPLAYS];

    dErr = CGGetOnlineDisplayList(maxDisplays, onlineDisplays, &displayCount);

    if (dErr != kCGErrorSuccess) {
        displayCount = 0;
    }

    if (index > (displayCount - 1)) {
        return 0;
    }

    return onlineDisplays[index];
}

CGDisplayCount
PROCFS_GetDisplayCount(void)
{
    CGDisplayErr      dErr;
    CGDisplayCount    displayCount;
    CGDisplayCount    maxDisplays = MAX_DISPLAYS;
    CGDirectDisplayID onlineDisplays[MAX_DISPLAYS];

    dErr = CGGetOnlineDisplayList(maxDisplays, onlineDisplays, &displayCount);

    if (dErr != kCGErrorSuccess) {
        displayCount = 0;
    }

    return displayCount;
}

int
PROCFS_GetInfoForDisplayAtIndex(unsigned int index, char *buf, size_t *size)
{
    CGDirectDisplayID d = PROCFS_GetDisplayIDForDisplayAtIndex(index);
    size_t insize = *size;

    if (d == 0) {
        return -1;
    }

    size_t acc = 0;

    acc += snprintf(buf + acc, insize - acc, "%-20s%ldx%ld\n", "resolution",
                    CGDisplayPixelsWide(d), CGDisplayPixelsHigh(d));
    acc += snprintf(buf + acc, insize - acc, "%-20s%p\n", "base address",
                    CGDisplayBaseAddress(d));
    acc += snprintf(buf + acc, insize - acc, "%-20s%ld\n", "bits per pixel",
                    CGDisplayBitsPerPixel(d));
    acc += snprintf(buf + acc, insize - acc, "%-20s%ld\n", "bits per sample",
                    CGDisplayBitsPerSample(d));
    acc += snprintf(buf + acc, insize - acc, "%-20s%ld\n", "samples per pixel",
                    CGDisplaySamplesPerPixel(d));
    acc += snprintf(buf + acc, insize - acc, "%-20s%ld\n", "bytes per row",
                    CGDisplayBytesPerRow(d));
    acc += snprintf(buf + acc, insize - acc, "%-20s%s\n", "main display",
                    CGDisplayIsMain(d) ? "yes" : "no");
    acc += snprintf(buf + acc, insize - acc, "%-20s%s\n", "builtin display",
                    CGDisplayIsBuiltin(d) ? "yes" : "no");
    acc += snprintf(buf + acc, insize - acc, "%-20s%s\n", "OpenGL acceleration",
                    CGDisplayUsesOpenGLAcceleration(d) ? "yes" : "no");
    acc += snprintf(buf + acc, insize - acc, "%-20s%08x\n", "model number",
                    CGDisplayModelNumber(d));
    acc += snprintf(buf + acc, insize - acc, "%-20s%08x\n", "serial number",
                    CGDisplaySerialNumber(d));
    acc += snprintf(buf + acc, insize - acc, "%-20s%08x\n", "unit number",
                    CGDisplayUnitNumber(d));
    acc += snprintf(buf + acc, insize - acc, "%-20s%08x\n", "vendor number",
                    CGDisplayVendorNumber(d));

    *size = acc;

    return 0;
}

off_t
PROCFS_GetPNGSizeForDisplayAtIndex(unsigned int index)
{
    CGDirectDisplayID targetDisplay =
        PROCFS_GetDisplayIDForDisplayAtIndex(index);

    if (targetDisplay == 0) {
        return (off_t)0;
    }

    off_t size = ((off_t)CGDisplayPixelsWide(targetDisplay) *
                  (off_t)CGDisplayPixelsHigh(targetDisplay) *
                  (off_t)3) + (off_t)8192;

    return size;
}

int
PROCFS_GetPNGForDisplayAtIndex(unsigned int index, CFMutableDataRef *data)
{
    CGDirectDisplayID targetDisplay =
        PROCFS_GetDisplayIDForDisplayAtIndex(index);

    *data = (CFMutableDataRef)0;

    if (targetDisplay == 0) {
        return -1;
    }

    void *raster = NULL;
    int width, height, rowBytes;

    raster = CreateFullScreenXRGB32(targetDisplay, &width, &height, &rowBytes);
    if (!raster) {
        return -1;
    }

    *data = CreatePNGDataFromXRGB32Raster(raster, width, height, rowBytes);

    free(raster);

    if (!*data) {
        return -1;
    }

    return 0;
}

} /* extern "C" */
