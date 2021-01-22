# VST3 plugin class ID migration

As someone pointed out, the development version of yabridge's VST3 support was
not compatible with projects containing VST3 plugins saved on Windows. As it
turned out, the class IDs VST3 plugins use to identify themselves with have a
different format on Windows than they do on Linux. As of commit
[1b804bd5ea4485cb204e40d476ef54801b1ecb38](https://github.com/robbert-vdh/yabridge/commit/1b804bd5ea4485cb204e40d476ef54801b1ecb38)
yabridge converts between those formats for cross-platform compatibilty, but
this does mean that containing VST3 plugins running through yabridge saved
before that commit will no longer be compatible. As a temporary migration path,
there are a few scripts here that you can use to convert old project files to
the new format.

These scripts won't overwrite any existing files, but remember to always make
more backups than you really need!

To use these, download the migration scripts for the DAWs you need to migrate an
old project file for and run them. You'll get on screen instructions with what
to do after that.

## Bitwig Studio

```shell
# First download the script
curl -o migrate-bitwig.py https://raw.githubusercontent.com/robbert-vdh/yabridge/master/tools/migration/migrate-bitwig.py
chmod +x migrate-bitwig.py

# And then run it on any old .bwproject files, the script will guide you through
# the migration process
./migrate-bitwig.py /path/to/some/project/project.bwproject
```

## REAPER

```shell
# First download the script
curl -o migrate-reaper.py https://raw.githubusercontent.com/robbert-vdh/yabridge/master/tools/migration/migrate-reaper.py
chmod +x migrate-reaper.py

# And then run it on any old .RPP files, the script will guide you through the
# migration process
./migrate-reaper.py /path/to/some/project.RPP
```
