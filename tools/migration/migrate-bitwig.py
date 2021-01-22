#!/usr/bin/env python3

import argparse
import glob
import os
import re
import textwrap


# Bitwig's project file format is not documented, bot that's not a problem for
# us! Luckily Bitwig stores the path to the VST3 bundle right next to the VST3
# class ID, so we can just look for those paths. This will capture the path to
# the VST3 bundle as well as the class ID.
BITWIG_VST3_RE = re.compile(
    rb"(/home/[^/]+/.vst3/yabridge/.+\.vst3)\n([0-9a-zA-Z]{32})"
)


parser = argparse.ArgumentParser(
    description="Migrate old yabridge VST3 plugin instances in Bitwig project files."
)
parser.add_argument(
    "filename", type=str, help="The .bwproject project file to migrate."
)

# As a safety measure we want to limit the file names we accept
args = parser.parse_args()

filename = args.filename
file_stem, file_extension = os.path.splitext(filename)
if file_extension.lower() != ".bwproject":
    print("For safety reasons, only '*.bwproject' files are accepted")
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
            "Migrating Bitwig project files is a two stop process. ",
            width=80,
            break_on_hyphens=False,
        )
    )
)
print()

print(
    "\n".join(
        textwrap.wrap(
            "First this script will rewrite the .bwproject file to use thew new plugin IDs. "
            "For every yabridge VST3 plugin found you will be prompted with the question if you want to migrate it. "
            "Answer 'yes' for all old yabridge VST3 plugin instances, and 'no' if this instance should not be migrated (for instance if you have a project file with mixed old and new instances). ",
            width=80,
            break_on_hyphens=False,
        )
    )
)
print()

print(
    "\n".join(
        textwrap.wrap(
            f"After that you will be asked to open the new '{migrated_filename}' project. "
            "During this process you should make that all other Bitwig projects are closed. "
            "When opening the new project you will notice that the migrated plugins will try to load but then fail because they cannot load their preset files. "
            "At this point you should tell this script to continue, and it will rewrite the preset files. "
            "If you then save and reopen the project, everything should work again. "
            "Make sure to test whether the new project works immediately after finishing this migration process.",
            width=80,
            break_on_hyphens=False,
        )
    )
)
print()

# We'll first through the original file, and prompt to replace all VST3 class
# IDs we come across. See `WineUID` in yabridge's source code for an
# explanation of this conversion. After this we'll have to modify the
# compressed `.vstpreset` files contained in the file, so we keep track of the
# UID replacements we'll have to make.
uid_bytes_replacements = {}
with open(filename, "rb") as f_input, open(migrated_filename, "xb") as f_output:
    # Since this is a binary file format, we can't do this on a lien by line
    # basis like we did for REAPER project files
    migrated_file = f_input.read()

    # Bitwig sprinkles these class IDs all over the file, so we cannot just
    # iterate over `BITWIG_VST3_RE.finditer(migrated_file)` and we need to do
    # some mass replacements instead. Luckily every class ID in the file is
    # followed by two null bytes, so that should reduce false positives
    # greatly. We convert the matches to a set first because there will be
    # duplicates.
    yabridge_plugins = set(BITWIG_VST3_RE.findall(migrated_file))

    for (plugin_path, wine_uid) in yabridge_plugins:
        removeme = wine_uid

        plugin_path = plugin_path.decode("utf-8")
        wine_uid = bytearray.fromhex(wine_uid.decode("ascii"))
        converted_uid = wine_uid.copy()

        converted_uid[0] = wine_uid[3]
        converted_uid[1] = wine_uid[2]
        converted_uid[2] = wine_uid[1]
        converted_uid[3] = wine_uid[0]

        converted_uid[4] = wine_uid[5]
        converted_uid[5] = wine_uid[4]
        converted_uid[6] = wine_uid[7]
        converted_uid[7] = wine_uid[6]

        print(f"Found '{plugin_path}' with class ID '{wine_uid.hex().upper()}'")
        while True:
            answer = input("Should this plugin be migrated? [yes/no] ").lower()
            if answer == "yes":
                # As mentioned above the class IDs are sprinkled all over the
                # file. Luckily they're always followed by two null bytes, so
                # that should greatly reduce the number of false positives
                wine_uid_bytes = wine_uid.hex().encode("ascii").upper()
                converted_uid_bytes = converted_uid.hex().encode("ascii").upper()
                migrated_file = migrated_file.replace(
                    wine_uid_bytes + b"\0\0",
                    converted_uid_bytes + b"\0\0",
                )

                # And we'll also have to rename this UID in the `.vstpreset`
                # files Bitwig will extract to `~/.BitwigStudio/plugin-states`
                uid_bytes_replacements[wine_uid_bytes] = converted_uid_bytes
                break
            elif answer == "no":
                break
            else:
                print("Please answer only 'yes' or 'no'")

    print()
    print(
        f""
        "\n".join(
            textwrap.wrap(
                f"First step of the migration process done, writing the migrated project to '{migrated_filename}'",
                width=80,
                break_on_hyphens=False,
            )
        )
    )
    f_output.write(migrated_file)

print()
print(
    "\n".join(
        textwrap.wrap(
            f"Now close any Bitwig project you may still have open, and open '{migrated_filename}' instead. "
            "Check all plugin instances. Migrated old yabridge instances should show up correctly but with an error saying that their plugin state cannot be loaded, that's normal. "
            "Once you have confirmed that this is the case for all plugins, type 'continue' below to finish the migration process, or press Ctrl+C to abort. ",
            width=80,
            break_on_hyphens=False,
        )
    )
)
while True:
    if input("Continue? [continue] ") == "continue":
        break

# Now we'll go through all `.vstpreset` files Bitwig has extracted from the
# project file and apply the same replacements we made to the .bwproject file.
# Sadly I couldn't find an easy way to figure out which state files belong to
# which plugin, so we're going to have to go through all of them.
for preset_filename in glob.glob(
    os.path.expanduser("~/.BitwigStudio/plugin-states/**/*.vstpreset"), recursive=True
):
    with open(preset_filename, "r+b") as f:
        # Luckily this format is clearly defined, so this is much easier than
        # trying to parse the .bwproject files
        # https://steinbergmedia.github.io/vst3_doc/vstinterfaces/vst3loc.html#presetformat
        f.seek(8)
        uid_bytes = f.read(32)
        if uid_bytes in uid_bytes_replacements:
            # If the user has marked the plugin this UID belongs to as one that
            # should be migrated, then we'll overwrite the old UID in these
            # .vstpreset files
            f.seek(8)
            f.write(uid_bytes_replacements[uid_bytes])

print()
print(
    "\n".join(
        textwrap.wrap(
            f"Now save the project, close it, and reopen '{migrated_filename}'. "
            "Everything should now once again be fully functional. ",
            width=80,
            break_on_hyphens=False,
        )
    )
)
