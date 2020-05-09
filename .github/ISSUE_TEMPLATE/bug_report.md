---
name: Bug report
about: Create a report to help us improve
title: ''
labels: ''
assignees: ''

---

Thanks for trying out yabridge! If you're having issues with plugins not working at all or scans timing out then make sure to check out the [troubleshooting common issues](https://github.com/robbert-vdh/yabridge#troubleshooting-common-issues) section of the readme. 

**Problem description**
A short description of what the issue is, and possibly some steps to reproduce it if applicable.

**What did you expect to happen?**
...

**What actually happened?**
...

**System information**
- Operating system: [e.g. Manjaro, or Ubuntu 20.04]
- Wine version: [e.g. Wine Staging 5.7]
- Installation method: [symlinks/copies]
- yabridge version: [e.g. 1.1.0, or commit a29f43a]

**Debug log**

Please also include a debug log if possible:

1. First make sure that there are no left over Wine processes left running in
   the background using `wineserver -k`.
2. Launch your host from a terminal using
   `env YABRIDGE_DEBUG_FILE=/tmp/yabridge.log YABRIDGE_DEBUG_LEVEL=2 <host>`,
   e.g. `env YABRIDGE_DEBUG_FILE=/tmp/yabridge.log YABRIDGE_DEBUG_LEVEL=2 bitwig-studio`.
3. Try to scan or load the plugin that has issues.
4. `/tmp/yabridge.log` should now contain a debug log. You can either attach
   this log directly to the issue by dragging the file onto this text box, or
   you could upload the contents to a website like GitHub gists or Hastebin.
