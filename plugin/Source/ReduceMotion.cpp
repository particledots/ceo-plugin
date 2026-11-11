// Asks the system whether the user wants less motion. On macOS this file is compiled as Objective-C++
// (see plugin/CMakeLists.txt); elsewhere the answer is always "no".

#if defined (__APPLE__)
 #import <AppKit/AppKit.h>

 bool ceoSystemPrefersReducedMotion()
 {
     @autoreleasepool
     {
         return [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceMotion] ? true : false;
     }
 }
#else
 bool ceoSystemPrefersReducedMotion()
 {
     return false;
 }
#endif
