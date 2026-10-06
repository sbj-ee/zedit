#include "macos_pasteboard.hpp"

#import <AppKit/AppKit.h>

#include <string>

namespace zedit::frontend {

namespace {

NSPasteboard* pasteboard_named(const std::string& name) {
  if (name.empty()) return [NSPasteboard generalPasteboard];
  return [NSPasteboard pasteboardWithName:[NSString stringWithUTF8String:name.c_str()]];
}

std::optional<std::string> to_std(NSString* s) {
  if (s == nil) return std::nullopt;
  const char* utf8 = [s UTF8String];
  if (utf8 == nullptr) return std::nullopt;
  return std::string(utf8);
}

std::optional<std::string> data_as_string(NSData* d) {
  if (d == nil) return std::nullopt;
  return std::string(static_cast<const char*>([d bytes]), static_cast<size_t>([d length]));
}

}  // namespace

zedit::core::ClipboardFlavors read_macos_pasteboard(const std::string& pasteboard_name) {
  zedit::core::ClipboardFlavors flavors;
  @autoreleasepool {
    NSPasteboard* pb = pasteboard_named(pasteboard_name);
    NSArray<NSPasteboardType>* types = [pb types];
    if (types == nil) return flavors;

    if ([types containsObject:NSPasteboardTypeString]) {
      flavors.plain_text = to_std([pb stringForType:NSPasteboardTypeString]);
    }
    if ([types containsObject:NSPasteboardTypeHTML]) {
      // Chromium/Electron write public.html as a string; fall back to the
      // raw bytes (as UTF-8) for writers that set data instead.
      flavors.html = to_std([pb stringForType:NSPasteboardTypeHTML]);
      if (!flavors.html) flavors.html = data_as_string([pb dataForType:NSPasteboardTypeHTML]);
    }
    if ([types containsObject:NSPasteboardTypeRTF]) {
      flavors.rtf = data_as_string([pb dataForType:NSPasteboardTypeRTF]);
    } else if (!flavors.plain_text && [types containsObject:NSPasteboardTypeRTFD]) {
      // RTFD (RTF + attachments) only: let AppKit flatten it to a string.
      NSData* rtfd = [pb dataForType:NSPasteboardTypeRTFD];
      if (rtfd != nil) {
        NSAttributedString* attributed = [[NSAttributedString alloc] initWithRTFD:rtfd
                                                              documentAttributes:nil];
        if (attributed != nil) flavors.plain_text = to_std([attributed string]);
      }
    }
    NSMutableArray<NSString*>* urls = [NSMutableArray array];
    for (NSPasteboardItem* item in [pb pasteboardItems]) {
      NSString* url = [item stringForType:NSPasteboardTypeFileURL];
      if (url == nil) url = [item stringForType:NSPasteboardTypeURL];
      if (url != nil) [urls addObject:url];
    }
    if ([urls count] > 0) flavors.uri_list = to_std([urls componentsJoinedByString:@"\n"]);
  }
  return flavors;
}

std::optional<std::string> read_macos_clipboard_text() {
  static NSInteger cached_change_count = -1;
  static std::optional<std::string> cached_text;
  NSInteger change_count = [[NSPasteboard generalPasteboard] changeCount];
  if (change_count != cached_change_count) {
    cached_text = zedit::core::clipboard_plain_text(read_macos_pasteboard());
    cached_change_count = change_count;
  }
  return cached_text;
}

void write_macos_pasteboard(const std::string& pasteboard_name,
                            const zedit::core::ClipboardFlavors& flavors) {
  @autoreleasepool {
    NSPasteboard* pb = pasteboard_named(pasteboard_name);
    [pb clearContents];
    NSMutableArray<NSPasteboardType>* types = [NSMutableArray array];
    if (flavors.plain_text) [types addObject:NSPasteboardTypeString];
    if (flavors.html) [types addObject:NSPasteboardTypeHTML];
    if (flavors.rtf) [types addObject:NSPasteboardTypeRTF];
    [pb declareTypes:types owner:nil];
    if (flavors.plain_text) {
      [pb setString:[NSString stringWithUTF8String:flavors.plain_text->c_str()]
            forType:NSPasteboardTypeString];
    }
    if (flavors.html) {
      [pb setString:[NSString stringWithUTF8String:flavors.html->c_str()]
            forType:NSPasteboardTypeHTML];
    }
    if (flavors.rtf) {
      [pb setData:[NSData dataWithBytes:flavors.rtf->data() length:flavors.rtf->size()]
          forType:NSPasteboardTypeRTF];
    }
  }
}

bool macos_pasteboard_has_type(const std::string& pasteboard_name, const std::string& uti) {
  @autoreleasepool {
    NSArray<NSPasteboardType>* types = [pasteboard_named(pasteboard_name) types];
    return types != nil && [types containsObject:[NSString stringWithUTF8String:uti.c_str()]];
  }
}

std::string create_unique_macos_pasteboard() {
  @autoreleasepool {
    return to_std([[NSPasteboard pasteboardWithUniqueName] name]).value_or(std::string{});
  }
}

void release_macos_pasteboard(const std::string& pasteboard_name) {
  if (pasteboard_name.empty()) return;  // never release the general pasteboard
  @autoreleasepool {
    [pasteboard_named(pasteboard_name) releaseGlobally];
  }
}

}  // namespace zedit::frontend
