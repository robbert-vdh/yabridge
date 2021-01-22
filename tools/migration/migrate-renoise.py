#!/usr/bin/env python3

import argparse
import os
import shutil
import textwrap
from xml.etree import ElementTree
import zipfile


# Renoise project files are zip archives that contain an XML file
RENOISE_XML_FILENAME = "Song.xml"


parser = argparse.ArgumentParser(
    description="Migrate old yabridge VST3 plugin instances in Renoise project files."
)
parser.add_argument("filename", type=str, help="The .xrns project file to migrate.")

# As a safety measure we want to limit the file names we accept
args = parser.parse_args()

filename = args.filename
file_stem, file_extension = os.path.splitext(filename)
if file_extension.lower() != ".xrns":
    print("For safety reasons, only '*.xrns' files are accepted")
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
# explanation of this conversion. In all other migration scripts we could
# simply do this with regular expressions, but Renoise uses XML files where
# every attribute is stored on its own line, so we cannot
with zipfile.ZipFile(filename, "r") as in_archive, zipfile.ZipFile(
    migrated_filename, "w"
) as out_archive:
    # I don't know if Renoise uses this field, probably not
    out_archive.comment = in_archive.comment

    migrated_xml = ElementTree.fromstring(in_archive.read(RENOISE_XML_FILENAME))
    for element in migrated_xml.iter():
        # Renoise uses different tags for plugins, so we'll identify VST3
        # plugins based on the attributes instead
        plugin_name = element.find("PluginDisplayName")
        plugin_type = element.find("PluginType")
        plugin_uid = element.find("PluginIdentifier")
        if (
            plugin_name is not None
            and plugin_type is not None
            and plugin_uid is not None
            and plugin_type.text == "VST3"
            and plugin_uid.text is not None
        ):
            wine_uid = bytearray.fromhex(plugin_uid.text)
            converted_uid = bytearray.fromhex(plugin_uid.text)

            converted_uid[0] = wine_uid[3]
            converted_uid[1] = wine_uid[2]
            converted_uid[2] = wine_uid[1]
            converted_uid[3] = wine_uid[0]

            converted_uid[4] = wine_uid[5]
            converted_uid[5] = wine_uid[4]
            converted_uid[6] = wine_uid[7]
            converted_uid[7] = wine_uid[6]

            print(
                f"Found '{plugin_name.text}' with class ID '{wine_uid.hex().upper()}'"
            )
            while True:
                answer = input("Should this plugin be migrated? [yes/no] ").lower()
                if answer == "yes":
                    plugin_uid.text = converted_uid.hex().upper()
                    break
                elif answer == "no":
                    break
                else:
                    print("Please answer only 'yes' or 'no'")

    print(f"\nMigration successful, writing the results to '{migrated_filename}'")
    out_archive.writestr(RENOISE_XML_FILENAME, ElementTree.tostring(migrated_xml))
