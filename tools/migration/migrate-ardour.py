#!/usr/bin/env python3

import argparse
import os
import re
import textwrap


# Parsing XML with regular expressions, what could go wrong! I'll make the
# assumption that the plugin developers were kind and did not include any
# double quotes in their plugin names, and that the order of attributes saved
# by Ardour is stable. This regexp will find VST3 plugins in a .ardour XML file
# and capture its name and class ID.
ARDOUR_VST3_RE = re.compile(
    r'name="([^"]+)".+type="vst3".+unique-id="([0-9a-zA-Z]{32})"'
)


parser = argparse.ArgumentParser(
    description="Migrate old yabridge VST3 plugin instances in Ardour project files."
)
parser.add_argument("filename", type=str, help="The .ardour project file to migrate.")

# As a safety measure we want to limit the file names we accept
args = parser.parse_args()

filename = args.filename
file_stem, file_extension = os.path.splitext(filename)
if file_extension.lower() != ".ardour":
    print("For safety reasons, only '*.ardour' files are accepted")
    exit(1)
if file_stem.endswith("-migrated"):
    print("This project file has already been migrated to the new format")
    exit(1)

migrated_filename = file_stem + "-migrated" + file_extension
if os.path.exists(migrated_filename):
    print(
        f"'{migrated_filename}' already exists, back it up and move it elsewhere "
        "if you want to redo the migration"
    )
    exit(1)

print(
    "\n".join(
        textwrap.wrap(
            f"This script will go through '{filename}' to migrate old yabridge VST3 plugin instances. "
            f"The output will be saved to '{migrated_filename}', but make sure to still create a backup of the original file in case something does go wrong. "
            f"For every VST3 plugin found you will be prompted with the question if you want to migrate it. "
            f"Answer 'yes' for all old yabridge VST3 plugin instances, and 'no' for any other VST3 plugin."
            f"Make sure to test whether the new project works immediately after migration.",
            width=80,
            break_on_hyphens=False,
        )
    )
)
print()

# We'll search through the original file, and prompt to replace all VST3 class
# IDs we come across. See `WineUID` in yabridge's source code for an
# explanation of this conversion.
with open(filename, "r", encoding="utf-8") as f_input, open(
    migrated_filename, "x", encoding="utf-8"
) as f_output:
    migrated_file = []
    for line in f_input.readlines():
        # Sadly can't use the walrus operator here since old distros might
        # still ship with Python 3.6
        matches = ARDOUR_VST3_RE.search(line)
        if matches is not None:
            plugin_name = matches.group(1)

            wine_uid_start, wine_uid_end = matches.span(2)
            wine_uid = bytearray.fromhex(matches.group(2))
            converted_uid = bytearray.fromhex(matches.group(2))

            converted_uid[0] = wine_uid[3]
            converted_uid[1] = wine_uid[2]
            converted_uid[2] = wine_uid[1]
            converted_uid[3] = wine_uid[0]

            converted_uid[4] = wine_uid[5]
            converted_uid[5] = wine_uid[4]
            converted_uid[6] = wine_uid[7]
            converted_uid[7] = wine_uid[6]

            migrated_line = (
                line[:wine_uid_start]
                + converted_uid.hex().upper()
                + line[wine_uid_end:]
            )

            print(f"Found '{plugin_name}' with class ID '{wine_uid.hex().upper()}'")
            while True:
                answer = input("Should this plugin be migrated? [yes/no] ").lower()
                if answer == "yes":
                    migrated_file.append(migrated_line)
                    break
                elif answer == "no":
                    migrated_file.append(line)
                    break
                else:
                    print("Please answer only 'yes' or 'no'")
        else:
            migrated_file.append(line)

    print(f"\nMigration successful, writing the results to '{migrated_filename}'")
    f_output.writelines(migrated_file)

print(
    "\n".join(
        textwrap.wrap(
            f"You may have to manually clean Ardour's VST3 cache and rescan if it cannot find the plugins after migrating.",
            width=80,
            break_on_hyphens=False,
        )
    )
)
