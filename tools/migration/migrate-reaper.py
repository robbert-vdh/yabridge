#!/usr/bin/env python3

import argparse
import os
import re
import textwrap


# Yes, you shouldn't try to parse these kinds of things with regular
# expressions. But writing a proper parser takes effort, and this approach will
# catch all VST3 plugins just fine even if the developer somehow included
# quotes in their vendor or plugin names. This regexp will find VST3 plugins in
# a .RPP file and capture its name and class ID.
REAPER_VST3_RE = re.compile(rb'<VST "(VST3.+?)" .+\{([0-9a-zA-Z]{32})\}')


parser = argparse.ArgumentParser(
    description="Migrate old yabridge VST3 plugin instances in REAPER project files."
)
parser.add_argument("filename", type=str, help="The .RPP project file to migrate.")

# As a safety measure we want to limit the file names we accept
args = parser.parse_args()

filename = args.filename
file_stem, file_extension = os.path.splitext(filename)
if file_extension.lower() != ".rpp":
    print("For safety reasons, only '*.RPP' files are accepted")
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
        )
    )
)
print()

# We'll search through the original file, and prompt to replace all VST3 class
# IDs we come across. See `WineUID` in yabridge's source code for an
# explanation of this conversion.
with open(filename, "rb") as f_input, open(migrated_filename, "xb") as f_output:
    migrated_file = []
    for line in f_input.readlines():
        # Sadly can't use the walrus operator here since old distros might
        # still ship with Python 3.6
        matches = REAPER_VST3_RE.search(line)
        if matches is not None:
            plugin_name = matches.group(1).decode("utf-8")

            wine_uid_start, wine_uid_end = matches.span(2)
            wine_uid = bytearray.fromhex(matches.group(2).decode("ascii"))
            converted_uid = bytearray.fromhex(matches.group(2).decode("ascii"))

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
                + converted_uid.hex().encode("ascii").upper()
                + line[wine_uid_end:]
            )

            while True:
                answer = input(
                    f"Found '{plugin_name}' with class ID '{wine_uid.hex().upper()}'\nShould this plugin be migrated? [yes/no] "
                ).lower()
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
