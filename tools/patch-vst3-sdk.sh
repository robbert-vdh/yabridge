#!/usr/bin/env bash
#
# Patch the VST3 SDK and replace all MSVC-isms so it can be compiled with
# winegcc. We do it this way instead of modifying the SDK directly so we don't
# have to fork multiple repositories and keep them up to date.
# If anyone knows a better way to get the SDK to compile with Win32 supports
# under winegcc without having to modify it then please let me know, because I'd
# rather not have to do this.
#
# Usage:
# patch-vst3-sdk.sh <sdk_directory>

set -euo pipefail

sdk_directory=$1
if [[ -z $sdk_directory ]]; then
  echo "Usage:"
  echo "patch-vst3-sdk.sh <sdk_directory>"
  exit 1
fi

# Make sure all imports use the correct casing
find "$sdk_directory" -type f \( -iname '*.h' -or -iname '*.cpp' \) -print0 |
  xargs -0 sed -i -E 's/^#include <(Windows.h|ShlObj.h)>$/#include <\L\1\E>/'

# We're building with `WIN32_LEAN_AND_MEAN` because some of the definitions in
# there conflict with the C standard library as provided by GCC. This also
# excludes the shell API, which the VST3 SDK uses to open URLs.
sed -i "s/^#include <windows.h>$/#include <windows.h>  \\/\\/ patched for yabridge\\
#include <shellapi.h>/" "$sdk_directory/public.sdk/source/common/openurl.cpp"

# Use the string manipulation functions from the C standard library
sed -i 's/\bSMTG_OS_WINDOWS\b/0/g;s/\bSMTG_OS_LINUX\b/1/g' "$sdk_directory/base/source/fstring.cpp"
sed -i 's/\bSMTG_OS_WINDOWS\b/0/g;s/\bSMTG_OS_LINUX\b/1/g' "$sdk_directory/pluginterfaces/base/fstrdefs.h"
sed -i 's/\bSMTG_OS_WINDOWS\b/0/g;s/\bSMTG_OS_LINUX\b/1/g' "$sdk_directory/pluginterfaces/base/ustring.cpp"

# `Windows.h` expects `wchar_t`, and the above defines will cause us to use
# `char16_t` for string literals. This replacement targets a very specific line,
# so if the SDK gets updated, this fails, and we're getting a ton of `wchar_t`
# related compile errors, that's why. The previous sed call will have replaced
# `SMTG_OS_WINDOWS` with a 0 here.
sed -i 's/^	#if 0$/	#if __WINE__/' "$sdk_directory/pluginterfaces/base/fstrdefs.h"

# We'll need some careful replacements in the Linux definitions in `fstring.cpp`
# to use `wchar_t` instead of `char16_t`.
replace_char16() {
  local needle=$1
  local filename=$2

  wchar_version=${needle//char16_t/wchar_t}
  sed -i "s/^$needle$/#ifdef __WINE__\\
    $wchar_version\\
#else\\
    \0\\
#endif/" "$filename"
}

replace_char16 "using ConverterFacet = std::codecvt_utf8_utf16<char16_t>;" "$sdk_directory/base/source/fstring.cpp"
replace_char16 "using Converter = std::wstring_convert<ConverterFacet, char16_t>;" "$sdk_directory/base/source/fstring.cpp"
replace_char16 "using Converter = std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>;" "$sdk_directory/pluginterfaces/base/ustring.cpp"

# The definitions of long doesn't match up between platforms, and the mingw
# version here is trying to do something funky
sed -i 's/\b__MINGW32__\b/__NOPE__/g' "$sdk_directory/pluginterfaces/base/funknown.cpp"

# The string conversion functions in the VST3 SDK itself are not mingw aware and
# will thus use the wrong string types since we're not compiling with MSVC
# TODO: Figure out if this is actually needed
sed -i 's/^#if defined(_MSC_VER) && .\+$/#if __WINE__/' "$sdk_directory/public.sdk/source/vst/utility/stringconvert.cpp"

# Use the proper `<filesystem>` header instead of the experimental one
# TODO: Check if <filesystem> now works with Winelib, or replace with Boost
# sed -i 's/^#if _HAS_CXX17 && defined(_MSC_VER)$/#if 1/' "$sdk_directory/public.sdk/source/vst/hosting/module_win32.cpp"

# Don't try adding `std::u8string` to an `std::vector<std::string>`. MSVC
# probably coerces them, but GCC doesn't
sed -i 's/\bgeneric_u8string\b/generic_string/g' "$sdk_directory/public.sdk/source/vst/hosting/module_win32.cpp"

# Meson requires this program to output something, or else it will error out
# when trying to encode the empty output
echo "Successfully patched '$sdk_directory' for winegcc compatibility"
