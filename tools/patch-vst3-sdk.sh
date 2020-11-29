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
  xargs -0 sed -i 's/^#include <Windows.h>$/#include <windows.h>/'

# Use the string manipulation functions from the C standard library
sed -i 's/\bSMTG_OS_WINDOWS\b/0/g;s/\bSMTG_OS_LINUX\b/1/g' "$sdk_directory/base/source/fstring.cpp"
sed -i 's/\bSMTG_OS_WINDOWS\b/0/g;s/\bSMTG_OS_LINUX\b/1/g' "$sdk_directory/pluginterfaces/base/fstrdefs.h"

# We'll need some careful replacements in the Linux definitions in `fstring.cpp`
# to use `wchar_t` instead of `char16_t`.
sed -i "s/^using ConverterFacet = std::codecvt_utf8_utf16<char16_t>;$/#ifdef __WINE__\\
    using ConverterFacet = std::codecvt_utf8_utf16<wchar_t>;\\
#else\\
    \0\\
#endif/" "$sdk_directory/base/source/fstring.cpp"
sed -i "s/^using Converter = std::wstring_convert<ConverterFacet, char16_t>;$/#ifdef __WINE__\\
    using Converter = std::wstring_convert<ConverterFacet, wchar_t>;\\
#else\\
    \0\\
#endif/" "$sdk_directory/base/source/fstring.cpp"

# `Windows.h` expects `wchar_t`, and the above defines will cause us to use
# `char16_t` for string literals. This replacement targets a very specific line,
# so if the SDK gets updated, this fails, and we're getting a ton of `wchar_t`
# related compile errors, that's why. The previous sed call will have replaced
# `SMTG_OS_WINDOWS` with a 0 here.
sed -i 's/^	#if 0$/	#if __WINE__/' "$sdk_directory/pluginterfaces/base/fstrdefs.h"

# Meson requires this program to output something, or else it will error out
# when trying to encode the empty output
echo "Successfully patched '$sdk_directory' for winegcc compatibility"
