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

TODO: Document further once we add the actual scripts
