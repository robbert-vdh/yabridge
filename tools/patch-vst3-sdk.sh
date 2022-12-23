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
# patch-vst3-sdk.sh <sdk_directory> [sdk_version]

set -euo pipefail

sdk_directory=$1
version=${2:-}
if [[ ! -d $sdk_directory ]]; then
  echo >&2 "Usage:"
  echo >&2 "patch-vst3-sdk.sh <sdk_directory> [sdk_version]"
  echo >&2
  echo >&2 "The version is parsed from the from the CMakeLists.txt file if omitted."
  exit 1
fi

if [[ -z $version ]]; then
  # `-z` is a quick way to search across multiple lines in case they format
  # future versions differently, and the combination of `-n`, `p`, and replacing
  # everything before and after the version causes this to print nothing if the
  # replacement didn't succeed
  version=$(sed -zn 's/.*project(vstsdk\s*VERSION \([0-9.]\+\).*/\1/ p' "$sdk_directory/CMakeLists.txt")
fi

if [[ -z $version ]]; then
  echo >&2 "Could not parse the VST3 SDK version from '$sdk_directory/CMakeLists.txt'"
  exit 1
fi

patch_file=$(dirname "$0")/vst3-sdk-patches/vst3-sdk-patch-$version.diff
if [[ ! -f $patch_file ]]; then
  echo >&2 "The patch file for this SDK version ('$patch_file') does not yet exist"
  exit 1
fi

# Patch either automatically reverses already applied patches, or throws errors
# when the patch has already been applied and you tell it to not reverse
# patches. So we'll check whether the patch has already been applied first.
if ! patch -d "$sdk_directory" -p1 -f --dry-run --reverse <"$patch_file" >/dev/null 2>&1; then
  patch -d "$sdk_directory" -p1 -f --forward <"$patch_file"
  echo -e "\nSuccessfully patched '$sdk_directory' for Winelib compatibility"
else
  # Meson requires this program to output something, or else it will error out
  # when trying to encode the empty output
  echo "'$sdk_directory' has already been patched for Winelib compatibility, ignoring..."
fi
