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

#include <CoreFoundation/CoreFoundation.h>

int main() {
  CFUserNotificationRef passwordDialog;
  SInt32 error;
  CFDictionaryRef dialogTemplate;
  CFOptionFlags responseFlags;
  int button;
  CFStringRef passwordRef;
  CFIndex passwordMaxSize;
  char *password;
  
  CFBundleRef myBundle = CFBundleGetMainBundle();
  CFURLRef myURL = CFBundleCopyExecutableURL(myBundle);
  CFURLRef myParentURL = CFURLCreateCopyDeletingLastPathComponent(kCFAllocatorDefault,
                                                                  myURL);
  CFRelease(myURL);
  CFURLRef myIconURL = CFURLCreateCopyAppendingPathComponent(kCFAllocatorDefault,
                                                             myParentURL,
                                                             CFSTR("ssh.icns"),
                                                             false);
  CFRelease(myParentURL);
  
  const void* keys[] = {
    kCFUserNotificationAlertHeaderKey,
    kCFUserNotificationTextFieldTitlesKey,
    kCFUserNotificationAlternateButtonTitleKey,
    kCFUserNotificationIconURLKey
  };
  const void* values[] = {
    CFSTR("sshfs Password"),
    CFSTR(""),
    CFSTR("Cancel"),
    myIconURL
  };
  dialogTemplate = CFDictionaryCreate(kCFAllocatorDefault,
                                      keys,
                                      values,
                                      sizeof(keys)/sizeof(*keys),
                                      &kCFTypeDictionaryKeyCallBacks,
                                      &kCFTypeDictionaryValueCallBacks);
  CFRelease(myIconURL);
  
  passwordDialog = CFUserNotificationCreate(kCFAllocatorDefault,
                                            0,
                                            kCFUserNotificationPlainAlertLevel
                                             |
                                            CFUserNotificationSecureTextField(0),
                                            &error,
                                            dialogTemplate);
  
  if (error)
    return error;
  
  error = CFUserNotificationReceiveResponse(passwordDialog,
                                            0,
                                            &responseFlags);
  if (error)
    return error;
  
  button = responseFlags & 0x3;
  if (button == kCFUserNotificationAlternateResponse)
    return 1;
  
  passwordRef = CFUserNotificationGetResponseValue(passwordDialog,
                                                   kCFUserNotificationTextFieldValuesKey,
                                                   0);
  
  passwordMaxSize = CFStringGetMaximumSizeForEncoding(CFStringGetLength(passwordRef),
                                                      kCFStringEncodingUTF8);
  password = malloc(passwordMaxSize);
  CFStringGetCString(passwordRef,
                     password,
                     passwordMaxSize,
                     kCFStringEncodingUTF8);
  CFRelease(passwordDialog);
  
  printf("%s", password);
  free(password);
  return 0;
}
